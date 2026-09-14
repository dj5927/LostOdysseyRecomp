// Compile the production transport exactly once through this fixture. Native
// WinHTTP calls are deterministic substitutes; no network or window is opened.
#include <windows.h>
#include <winhttp.h>
#include <updater/update.h>
#include <updater/progress.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <utility>

namespace
{
enum class Fault { None, CrackUrl, Open, Timeouts, Connect, OpenRequest, SetOption, Send, Receive, QueryHeaders, Read };
Fault fault = Fault::None;
DWORD failureCode = 0;
DWORD responseStatus = 200;
INTERNET_SCHEME responseScheme = INTERNET_SCHEME_HTTPS;
int closeCalls = 0, readCalls = 0, sendCalls = 0, receiveCalls = 0;
int checks = 0;
constexpr const char* responseBody = "metadata";

void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void Reset(Fault selected)
{
    fault = selected;
    failureCode = 0xD100u + DWORD(selected);
    responseStatus = 200;
    responseScheme = INTERNET_SCHEME_HTTPS;
    closeCalls = readCalls = sendCalls = receiveCalls = 0;
    SetLastError(ERROR_SUCCESS);
}

BOOL NativeResult(Fault operation)
{
    SetLastError(operation == fault ? failureCode : ERROR_SUCCESS);
    return operation == fault ? FALSE : TRUE;
}

BOOL TestWinHttpCrackUrl(LPCWSTR, DWORD, DWORD, URL_COMPONENTSW* components)
{
    if (!NativeResult(Fault::CrackUrl)) return FALSE;
    components->lpszHostName = const_cast<wchar_t*>(L"example.invalid");
    components->dwHostNameLength = 15;
    components->lpszUrlPath = const_cast<wchar_t*>(L"/latest");
    components->dwUrlPathLength = 7;
    components->dwExtraInfoLength = 0;
    components->nScheme = responseScheme;
    components->nPort = 443;
    return TRUE;
}

HINTERNET TestWinHttpOpen(LPCWSTR, DWORD access, LPCWSTR, LPCWSTR, DWORD flags)
{
    Check(access == WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY && flags == 0, "proxy/session contract changed");
    return NativeResult(Fault::Open) ? reinterpret_cast<HINTERNET>(1) : nullptr;
}

BOOL TestWinHttpSetTimeouts(HINTERNET, int resolve, int connect, int send, int receive)
{
    Check(resolve == 1500 && connect == 1500 && send == 3000 && receive == 5000, "request timeout contract changed");
    return NativeResult(Fault::Timeouts);
}

HINTERNET TestWinHttpConnect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD)
{
    return NativeResult(Fault::Connect) ? reinterpret_cast<HINTERNET>(2) : nullptr;
}

HINTERNET TestWinHttpOpenRequest(HINTERNET, LPCWSTR verb, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD flags)
{
    Check(std::wcscmp(verb, L"GET") == 0 && flags == WINHTTP_FLAG_SECURE, "request method/TLS contract changed");
    return NativeResult(Fault::OpenRequest) ? reinterpret_cast<HINTERNET>(3) : nullptr;
}

BOOL TestWinHttpSetOption(HINTERNET, DWORD option, void* value, DWORD size)
{
    Check(option == WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS && size == sizeof(DWORD) &&
          *static_cast<DWORD*>(value) == 3, "redirect limit contract changed");
    return NativeResult(Fault::SetOption);
}

BOOL TestWinHttpSendRequest(HINTERNET, LPCWSTR, DWORD, void*, DWORD, DWORD, DWORD_PTR)
{
    ++sendCalls;
    return NativeResult(Fault::Send);
}

BOOL TestWinHttpReceiveResponse(HINTERNET, void*)
{
    ++receiveCalls;
    return NativeResult(Fault::Receive);
}

BOOL TestWinHttpQueryHeaders(HINTERNET, DWORD, LPCWSTR, void* value, DWORD*, DWORD*)
{
    // Leave the output at zero on failure to catch misleading HTTP 0 reports.
    if (!NativeResult(Fault::QueryHeaders)) return FALSE;
    *static_cast<DWORD*>(value) = responseStatus;
    return TRUE;
}

BOOL TestWinHttpReadData(HINTERNET, void* buffer, DWORD size, DWORD* read)
{
    ++readCalls;
    if (!NativeResult(Fault::Read)) return FALSE;
    *read = readCalls == 1 ? DWORD(std::strlen(responseBody)) : 0;
    Check(*read <= size, "fixture response exceeds buffer");
    std::memcpy(buffer, responseBody, *read);
    return TRUE;
}

BOOL TestWinHttpCloseHandle(HINTERNET)
{
    ++closeCalls;
    SetLastError(ERROR_INVALID_HANDLE);
    return TRUE;
}
}

namespace updater
{
// Isolate the transport from updater UI; the fixture also exercises the real
// download read path without constructing ProgressWindow's native window.
class HttpFailureTestProgressWindow
{
public:
    explicit HttpFailureTestProgressWindow(uint32_t) {}
    bool Cancelled() { return false; }
    void SetDownloadProgress(uint64_t, uint64_t) {}
    void SetPhase(ProgressPhase) {}
};
}

#define WinHttpCrackUrl TestWinHttpCrackUrl
#define WinHttpOpen TestWinHttpOpen
#define WinHttpSetTimeouts TestWinHttpSetTimeouts
#define WinHttpConnect TestWinHttpConnect
#define WinHttpOpenRequest TestWinHttpOpenRequest
#define WinHttpSetOption TestWinHttpSetOption
#define WinHttpSendRequest TestWinHttpSendRequest
#define WinHttpReceiveResponse TestWinHttpReceiveResponse
#define WinHttpQueryHeaders TestWinHttpQueryHeaders
#define WinHttpReadData TestWinHttpReadData
#define WinHttpCloseHandle TestWinHttpCloseHandle
#define ProgressWindow HttpFailureTestProgressWindow
#include <updater/windows_http.cpp>
#undef WinHttpCrackUrl
#undef WinHttpOpen
#undef WinHttpSetTimeouts
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpSetOption
#undef WinHttpSendRequest
#undef WinHttpReceiveResponse
#undef WinHttpQueryHeaders
#undef WinHttpReadData
#undef WinHttpCloseHandle
#undef ProgressWindow

int main()
{
    constexpr const char* url = "https://example.invalid/latest";
    constexpr std::array cases = {
        std::pair{Fault::CrackUrl, "WinHttpCrackUrl"},
        std::pair{Fault::Open, "WinHttpOpen"},
        std::pair{Fault::Connect, "WinHttpConnect"},
        std::pair{Fault::OpenRequest, "WinHttpOpenRequest"},
        std::pair{Fault::Send, "WinHttpSendRequest"},
        std::pair{Fault::Receive, "WinHttpReceiveResponse"},
        std::pair{Fault::QueryHeaders, "WinHttpQueryHeaders(STATUS_CODE)"},
        std::pair{Fault::Read, "WinHttpReadData(update metadata)"}
    };
    for (const auto& [selected, operation] : cases)
    {
        Reset(selected);
        std::string body, error;
        const bool success = updater::ReadResponse(url, 1024, body, error);
        const DWORD errorAfterCleanup = GetLastError();
        Check(!success, "injected transport failure was ignored");
        Check(error == std::string(operation) + " failed with Win32 error " + std::to_string(failureCode),
              "native operation/error missing or overwritten during handle cleanup");
        Check(error.find("HTTP 0") == std::string::npos, "API failure mislabeled as an HTTP status");
        if (closeCalls) Check(errorAfterCleanup == ERROR_INVALID_HANDLE, "cleanup overwrite negative control missing");
        if (selected == Fault::Send) Check(receiveCalls == 0, "failed send continued to receive");
    }

    // Preserve the updater's existing nonfatal option-setting policy.
    for (Fault selected : {Fault::Timeouts, Fault::SetOption})
    {
        Reset(selected);
        std::string body, error;
        Check(updater::ReadResponse(url, 1024, body, error) && body == responseBody && sendCalls == 1,
              "nonfatal option-setting failure policy changed");
    }

    Reset(Fault::None);
    responseStatus = 503;
    std::string body, error;
    Check(!updater::ReadResponse(url, 1024, body, error) && error == "update server returned HTTP 503" && readCalls == 0,
          "HTTP status failure not kept separate from native error");

    Reset(Fault::None);
    error = "stale failure";
    Check(updater::ReadResponse(url, 1024, body, error) && body == responseBody && error.empty() && closeCalls == 3,
          "successful response/cleanup changed or retained stale error");

    Reset(Fault::None);
    body.clear();
    Check(!updater::ReadResponse("\xFF", 1024, body, error) &&
          error == "MultiByteToWideChar(update URL) failed with Win32 error " + std::to_string(ERROR_NO_UNICODE_TRANSLATION),
          "UTF-8 conversion lost its native error");

    Reset(Fault::None);
    responseScheme = INTERNET_SCHEME_FTP;
    Check(!updater::ReadResponse(url, 1024, body, error) && error == "update URL uses an unsupported scheme" && closeCalls == 0,
          "unsupported URL scheme not kept separate from native error");

    // This is the only on-disk fixture artifact, created in a unique temp
    // directory and removed before exit. There is no real transfer or UI.
    const auto outputRoot = std::filesystem::temp_directory_path() /
        ("lo-http-failure-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    Check(std::filesystem::create_directory(outputRoot), "could not create isolated download fixture");
    updater::HttpFailureTestProgressWindow progress(0);
    bool cancelled = false;
    Reset(Fault::Read);
    Check(!updater::Download(url, outputRoot / "download.tmp", std::strlen(responseBody), progress, error, cancelled) &&
          error == "WinHttpReadData(update download) failed with Win32 error " + std::to_string(failureCode) && !cancelled,
          "download read failure lost its native operation/error");
    Reset(Fault::None);
    Check(updater::Download(url, outputRoot / "download.tmp", std::strlen(responseBody), progress, error, cancelled) &&
          std::filesystem::file_size(outputRoot / "download.tmp") == std::strlen(responseBody),
          "successful bounded download changed");
    std::filesystem::remove(outputRoot / "download.tmp");
    std::filesystem::remove(outputRoot);
    std::printf("PASS: %d checks; 8 WinHTTP failure APIs, cleanup error preservation, HTTP status/UTF-8 distinction, unchanged option policy, successful metadata/download paths; no network or UI.\n", checks);
}
