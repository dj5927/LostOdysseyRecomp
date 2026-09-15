#include "apply_mode.h"

#ifdef _WIN32
#include "settings/restart.h"
#include "updater/update.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>
#include <string>
#include <string_view>

namespace updater
{
namespace
{
int Fail(const std::wstring &message)
{
    wchar_t silent[2]{};
    if (!GetEnvironmentVariableW(L"LO_UPDATER_SILENT", silent, DWORD(std::size(silent))))
        MessageBoxExW(nullptr, message.c_str(), L"Lost Odyssey update", MB_OK | MB_ICONERROR,
                      MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    return 1;
}

bool SamePath(const std::filesystem::path &left, const std::filesystem::path &right)
{
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error)) return true;
    const auto a = std::filesystem::absolute(left, error).lexically_normal();
    if (error) return false;
    const auto b = std::filesystem::absolute(right, error).lexically_normal();
    return !error && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool IsWithin(const std::filesystem::path &child, const std::filesystem::path &parent)
{
    const auto normalizedChild = child.lexically_normal();
    const auto normalizedParent = parent.lexically_normal();
    auto childIt = normalizedChild.begin();
    auto parentIt = normalizedParent.begin();
    for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt)
    {
        if (childIt == normalizedChild.end() ||
            CompareStringOrdinal(childIt->c_str(), -1, parentIt->c_str(), -1, TRUE) != CSTR_EQUAL)
            return false;
    }
    return true;
}

bool ParseApplyArguments(std::filesystem::path &planPath, DWORD &parentId, std::wstring &readyEvent,
                         bool &applyRequested)
{
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool malformed = arguments == nullptr;
    for (int i = 1; arguments && i < count; ++i)
    {
        const std::wstring_view argument(arguments[i]);
        if (argument == L"--apply-plan" && planPath.empty())
        {
            applyRequested = true;
            if (i + 1 < count) planPath = arguments[++i];
            else malformed = true;
        }
        else if (argument == L"--wait-process" && i + 1 < count && !parentId)
        {
            wchar_t *end = nullptr;
            const auto parsed = std::wcstoul(arguments[++i], &end, 10);
            if (!parsed || !end || *end) malformed = true;
            else parentId = DWORD(parsed);
        }
        else if (argument == L"--restart-ready" && i + 1 < count && readyEvent.empty())
        {
            readyEvent = arguments[++i];
        }
        else
        {
            malformed = true;
        }
    }
    if (arguments) LocalFree(arguments);
    return !malformed;
}

bool Launch(const ApplyPlan &plan, std::string &error)
{
    std::wstring command = settings::restart::QuoteArgument(plan.executable.wstring());
    for (const auto &argument : plan.launchArguments)
        command += L" " + settings::restart::QuoteArgument(argument);
    command.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(plan.executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &process))
    {
        const auto win32Error = GetLastError();
        error = "could not launch updated game: Win32 error " + std::to_string(win32Error);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

int Apply(const std::filesystem::path &planPath, DWORD parentId, const std::wstring &readyEvent)
{
    std::string error;
    auto plan = ReadApplyPlan(planPath, error);
    const auto self = CurrentExecutablePath();
    if (!plan || !ValidateApplyPlan(*plan, error) || self.empty() || SamePath(self, plan->executable) ||
        !IsWithin(planPath, plan->installRoot / ".update") ||
        !SamePath(planPath.parent_path(), plan->stageRoot.parent_path()))
    {
        if (error.empty()) error = "apply plan does not describe a staged operation";
        return Fail(L"The staged update is invalid. The existing installation was not changed.\n\n" +
                    std::filesystem::path(error).wstring());
    }

    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentId);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyEvent.c_str());
    if (!parent || !ready)
    {
        if (ready) CloseHandle(ready);
        if (parent) CloseHandle(parent);
        return Fail(L"The update helper could not establish the restart handshake. The existing installation was not changed.");
    }
    if (!SetEvent(ready))
    {
        CloseHandle(ready);
        CloseHandle(parent);
        return Fail(L"The update helper could not confirm readiness. The existing installation was not changed.");
    }
    CloseHandle(ready);
    if (WaitForSingleObject(parent, INFINITE) != WAIT_OBJECT_0)
    {
        CloseHandle(parent);
        return Fail(L"The update helper could not wait for the game to exit. The existing installation was not changed.");
    }
    CloseHandle(parent);

    if (!ApplyWithRollback(*plan, {}, error))
    {
        std::ofstream result(plan->installRoot / ".update" / "last-result.txt", std::ios::trunc);
        result << "failed=" << error << "\n";
        return Fail(L"The update could not be installed. Previous files were restored where possible.\n\n" +
                    std::filesystem::path(error).wstring());
    }
    std::ofstream result(plan->installRoot / ".update" / "last-result.txt", std::ios::trunc);
    result << "updated=" << plan->version << "\n";
    result.close();

    std::error_code filesystemError;
    const auto operationRoot = plan->stageRoot.parent_path();
    std::filesystem::remove(operationRoot / "download.zip", filesystemError);
    std::filesystem::remove(operationRoot / "apply-plan.json", filesystemError);
    std::filesystem::remove_all(operationRoot / "stage", filesystemError);
    std::filesystem::remove_all(operationRoot / "rollback", filesystemError);
    wchar_t module[32768]{};
    if (GetModuleFileNameW(nullptr, module, DWORD(std::size(module))))
        MoveFileExW(module, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    wchar_t silent[2]{};
    const bool ask = !plan->launchAfterApply &&
                     !GetEnvironmentVariableW(L"LO_UPDATER_SILENT", silent, DWORD(std::size(silent)));
    if (plan->launchAfterApply || (ask && MessageBoxExW(nullptr, L"The update is complete. Open Lost Odyssey now?",
                             L"Lost Odyssey Updater", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2,
                              MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == IDYES))
    {
        if (!Launch(*plan, error))
            return Fail(L"The update was installed, but the game could not be opened.\n\n" +
                        std::filesystem::path(error).wstring());
    }
    return 0;
}
} // namespace

std::optional<int> TryRunApplyMode()
{
    std::filesystem::path planPath;
    DWORD parentId = 0;
    std::wstring readyEvent;
    bool applyRequested = false;
    const bool valid = ParseApplyArguments(planPath, parentId, readyEvent, applyRequested);
    if (!applyRequested) return std::nullopt;
    if (!valid || planPath.empty() || !parentId || readyEvent.empty())
        return Fail(L"The update helper received an invalid startup request. The existing installation was not changed.");
    std::error_code error;
    planPath = std::filesystem::absolute(planPath, error).lexically_normal();
    if (error) return Fail(L"The update helper received an invalid startup request. The existing installation was not changed.");
    return Apply(planPath, parentId, readyEvent);
}
} // namespace updater
#endif
