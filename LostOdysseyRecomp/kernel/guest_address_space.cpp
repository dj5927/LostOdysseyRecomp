#include "guest_address_space.h"
#include <cstddef>
#include <cerrno>

#ifdef _WIN32
#include <windows.h>
#include <memoryapi.h>
#else
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace GuestAddressSpace
{
static constexpr size_t kSize = 0x100000000ull;
static constexpr size_t kBackingSize = 0xC0001000ull;
static constexpr size_t kStarts[] = {0, 0xA0000000, 0xC0000000, 0xE0000000};
static constexpr size_t kSizes[] = {0xA0000000, 0x20000000, 0x20000000, 0x20000000};
static constexpr size_t kOffsets[] = {0, 0xA0000000, 0xA0000000, 0xA0001000};
static constinit FailureInfo failure{};

FailureInfo GetFailureInfo() { return failure; }

const char* FailureOperationName(FailureOperation operation)
{
    switch (operation)
    {
    case FailureOperation::None: return "none";
    case FailureOperation::ReservePreferred: return "reserve preferred address";
    case FailureOperation::ReserveAny: return "reserve any address";
    case FailureOperation::SplitReservation: return "split reservation placeholder";
    case FailureOperation::CreateBacking: return "create shared backing";
    case FailureOperation::ResizeBacking: return "resize shared backing";
    case FailureOperation::MapView: return "map alias view";
    case FailureOperation::ProtectNull: return "protect null page";
    }
    return "unknown";
}

const char* FailureApiName(FailureOperation operation)
{
    switch (operation)
    {
    case FailureOperation::None: return "none";
#ifdef _WIN32
    case FailureOperation::ReservePreferred:
    case FailureOperation::ReserveAny: return "VirtualAlloc2";
    case FailureOperation::SplitReservation: return "VirtualFree";
    case FailureOperation::CreateBacking: return "CreateFileMappingW";
    case FailureOperation::MapView: return "MapViewOfFile3";
    case FailureOperation::ProtectNull: return "VirtualProtect";
#else
    case FailureOperation::ReservePreferred:
    case FailureOperation::ReserveAny:
    case FailureOperation::MapView: return "mmap";
    case FailureOperation::CreateBacking: return "memfd_create";
    case FailureOperation::ResizeBacking: return "ftruncate";
    case FailureOperation::ProtectNull: return "mprotect";
#endif
    default: return "unknown";
    }
}

static void RecordFailure(FailureOperation operation, uint32_t error, int32_t viewIndex,
                          const void* address, size_t size, size_t offset = 0,
                          uint32_t preferredReservationError = 0, uintptr_t backingHandle = 0)
{
    failure = {operation, error, preferredReservationError, viewIndex,
               reinterpret_cast<uintptr_t>(address), size, offset};
#ifdef _WIN32
    // This path also runs during global Memory construction. Keep it allocation
    // free, and retain the caller's original error before any diagnostic API.
    FILETIME timestamp{};
    GetSystemTimeAsFileTime(&timestamp);
    failure.utcFileTime = (uint64_t(timestamp.dwHighDateTime) << 32) | timestamp.dwLowDateTime;
    failure.uptimeMilliseconds = GetTickCount64();
    failure.threadId = GetCurrentThreadId();
    failure.backingHandle = backingHandle;
    switch (operation)
    {
    case FailureOperation::ReservePreferred:
    case FailureOperation::ReserveAny:
        failure.flags = MEM_RESERVE | MEM_RESERVE_PLACEHOLDER;
        failure.protection = PAGE_NOACCESS;
        break;
    case FailureOperation::SplitReservation:
        failure.flags = MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER;
        break;
    case FailureOperation::CreateBacking:
        failure.protection = PAGE_READWRITE;
        failure.backingHandle = reinterpret_cast<uintptr_t>(INVALID_HANDLE_VALUE);
        break;
    case FailureOperation::MapView:
        failure.flags = MEM_REPLACE_PLACEHOLDER;
        failure.protection = PAGE_READWRITE;
        break;
    case FailureOperation::ProtectNull:
        failure.protection = PAGE_NOACCESS;
        break;
    default: break;
    }
    // VirtualAlloc2 and MapViewOfFile3 both receive a null process handle,
    // which explicitly selects the calling process.
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        failure.memory = {1, 0, memory.dwMemoryLoad,
            memory.ullTotalPhys, memory.ullAvailPhys,
            memory.ullTotalPageFile, memory.ullAvailPageFile,
            memory.ullTotalVirtual, memory.ullAvailVirtual};
    }
    else
    {
        failure.memory.error = GetLastError();
    }
#endif
}

uint8_t* Allocate()
{
    failure = {};
#ifdef _WIN32
    uint32_t preferredError = 0;
    auto* base = static_cast<uint8_t*>(VirtualAlloc2(nullptr,
        reinterpret_cast<void*>(0x100000000ull), kSize,
        MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));
    if (!base)
    {
        preferredError = GetLastError();
        base = static_cast<uint8_t*>(VirtualAlloc2(nullptr, nullptr, kSize,
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));
    }
    if (!base)
    {
        RecordFailure(FailureOperation::ReserveAny, GetLastError(), -1, nullptr, kSize, 0, preferredError);
        return nullptr;
    }

    // Split the reservation before replacing any placeholder. Keeping the
    // address range reserved prevents another thread from stealing a view.
    size_t split = 0;
    for (; split < 3; ++split)
    {
        if (!VirtualFree(base + kStarts[split], kSizes[split],
            MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
        {
            RecordFailure(FailureOperation::SplitReservation, GetLastError(), int32_t(split),
                          base + kStarts[split], kSizes[split], 0, preferredError);
            for (size_t i = 0; i <= split; ++i)
                VirtualFree(base + kStarts[i], 0, MEM_RELEASE);
            return nullptr;
        }
    }

    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, DWORD(kBackingSize >> 32), DWORD(kBackingSize), nullptr);
    if (!section)
        RecordFailure(FailureOperation::CreateBacking, GetLastError(), -1, nullptr, kBackingSize, 0, preferredError);
    size_t mapped = 0;
    if (section)
    {
        for (; mapped < 4; ++mapped)
        {
            if (!MapViewOfFile3(section, nullptr, base + kStarts[mapped],
                kOffsets[mapped], kSizes[mapped], MEM_REPLACE_PLACEHOLDER,
                PAGE_READWRITE, nullptr, 0))
            {
                RecordFailure(FailureOperation::MapView, GetLastError(), int32_t(mapped),
                              base + kStarts[mapped], kSizes[mapped], kOffsets[mapped], preferredError,
                              reinterpret_cast<uintptr_t>(section));
                break;
            }
        }
        CloseHandle(section);
    }
    if (mapped != 4)
    {
        for (size_t i = 0; i < mapped; ++i)
            UnmapViewOfFile(base + kStarts[i]);
        for (size_t i = mapped; i < 4; ++i)
            VirtualFree(base + kStarts[i], 0, MEM_RELEASE);
        return nullptr;
    }
    DWORD oldProtect;
    if (!VirtualProtect(base, 4096, PAGE_NOACCESS, &oldProtect))
    {
        RecordFailure(FailureOperation::ProtectNull, GetLastError(), -1, base, 4096, 0, preferredError);
        Release(base);
        return nullptr;
    }
#else
    auto* base = static_cast<uint8_t*>(mmap(reinterpret_cast<void*>(0x100000000ull),
        kSize, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
    if (base == MAP_FAILED)
    {
        RecordFailure(FailureOperation::ReservePreferred, uint32_t(errno), -1,
                      reinterpret_cast<void*>(0x100000000ull), kSize);
        return nullptr;
    }
    int section = static_cast<int>(syscall(SYS_memfd_create, "lo-guest-memory", 0));
    if (section < 0)
    {
        RecordFailure(FailureOperation::CreateBacking, uint32_t(errno), -1, nullptr, kBackingSize);
        munmap(base, kSize);
        return nullptr;
    }
    bool success = ftruncate(section, kBackingSize) == 0;
    if (!success)
        RecordFailure(FailureOperation::ResizeBacking, uint32_t(errno), -1, nullptr, kBackingSize);
    for (size_t i = 0; success && i < 4; ++i)
    {
        success = mmap(base + kStarts[i], kSizes[i], PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_FIXED, section, kOffsets[i]) != MAP_FAILED;
        if (!success)
            RecordFailure(FailureOperation::MapView, uint32_t(errno), int32_t(i),
                          base + kStarts[i], kSizes[i], kOffsets[i]);
    }
    close(section);
    if (success && mprotect(base, 4096, PROT_NONE) != 0)
    {
        RecordFailure(FailureOperation::ProtectNull, uint32_t(errno), -1, base, 4096);
        success = false;
    }
    if (!success)
    {
        munmap(base, kSize);
        return nullptr;
    }
#endif
    return base;
}

void Release(uint8_t* base)
{
    if (!base)
        return;
#ifdef _WIN32
    for (size_t start : kStarts)
        UnmapViewOfFile(base + start);
#else
    munmap(base, kSize);
#endif
}
}
