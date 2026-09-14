#include <plume_log.h>

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define TEST_DUP _dup
#define TEST_DUP2 _dup2
#define TEST_FILENO _fileno
#define TEST_CLOSE _close
#else
#include <unistd.h>
#define TEST_DUP dup
#define TEST_DUP2 dup2
#define TEST_FILENO fileno
#define TEST_CLOSE close
#endif

namespace {
    int checks = 0;
    void Require(bool good, const char *message) {
        ++checks;
        if (!good) throw std::runtime_error(message);
    }

    struct StderrCapture {
        FILE *file = nullptr;
        int previous = -1;

        StderrCapture() {
            std::fflush(stderr);
            file = std::tmpfile();
            if (!file) throw std::runtime_error("temporary stderr capture unavailable");
            previous = TEST_DUP(TEST_FILENO(stderr));
            if (previous < 0 || TEST_DUP2(TEST_FILENO(file), TEST_FILENO(stderr)) < 0) {
                if (previous >= 0) TEST_CLOSE(previous);
                std::fclose(file);
                throw std::runtime_error("stderr capture redirection failed");
            }
        }

        ~StderrCapture() {
            plume::SetLogCallback(nullptr);
            std::fflush(stderr);
            TEST_DUP2(previous, TEST_FILENO(stderr));
            TEST_CLOSE(previous);
            std::fclose(file);
        }

        std::string Read() {
            std::fflush(stderr);
            std::fseek(file, 0, SEEK_SET);
            std::string output;
            char buffer[2048];
            while (const size_t count = std::fread(buffer, 1, sizeof(buffer), file)) output.append(buffer, count);
            std::fseek(file, 0, SEEK_END);
            return output;
        }
    };

    struct SavedRecord {
        plume::LogSeverity severity;
        std::string backend;
        std::string api;
        plume::LogErrorDomain domain;
        uint32_t rawCode;
        std::string detail;
        uint32_t occurrence;
    };
    std::vector<SavedRecord> records;
    void Capture(const plume::LogRecord &record) {
        records.push_back({record.severity, record.backend, record.api, record.domain,
            record.rawCode, record.detail, record.occurrence});
    }
    unsigned unusualCalls = 0;
    void Throwing(const plume::LogRecord &) {
        ++unusualCalls;
        throw std::runtime_error("callback exception");
    }
    void Reentrant(const plume::LogRecord &) {
        ++unusualCalls;
        // Changing registration must not bypass the delivery recursion guard.
        plume::SetLogCallback(nullptr);
        PLUME_LOG_ERROR("test", "nested", plume::LogErrorDomain::None, 0, "must be suppressed");
        plume::SetLogCallback(Reentrant);
    }
    std::atomic<unsigned> concurrentCalls{0};
    std::atomic<bool> concurrentCodesGood{true};
    void Concurrent(const plume::LogRecord &record) {
        concurrentCalls.fetch_add(1, std::memory_order_relaxed);
        if (record.rawCode != 0xFFFFFFFEu || record.domain != plume::LogErrorDomain::VkResult)
            concurrentCodesGood.store(false, std::memory_order_relaxed);
    }
}

int main() {
    try {
        {
            StderrCapture stderrCapture;
            plume::SetLogCallback(nullptr);
            PLUME_LOG_ERROR("D3D12", "Map", plume::LogErrorDomain::HResult, -2147024809,
                "size=%llu heap=%u", 1073741824ull, 1u);
            const std::string fallback = stderrCapture.Read();
            Require(fallback.find("backend=D3D12 api=Map domain=HRESULT code=0x80070057") != std::string::npos,
                "fallback lost native HRESULT bits or operation");
            Require(fallback.find("size=1073741824 heap=1") != std::string::npos, "fallback lost resource context");
            Require(fallback.find('\n') == fallback.size() - 1, "fallback duplicated or split a record");

            plume::SetLogCallback(Capture);
            PLUME_LOG_ERROR("Vulkan", "vmaCreateBuffer", plume::LogErrorDomain::VkResult, -2,
                "size=%llu\nheap=1", 1073741824ull);
            Require(records.size() == 1, "callback should receive exactly one event");
            Require(records[0].rawCode == 0xFFFFFFFEu && records[0].domain == plume::LogErrorDomain::VkResult,
                "callback lost negative VkResult bits or domain");
            Require(records[0].backend == "Vulkan" && records[0].api == "vmaCreateBuffer", "callback lost source identity");
            Require(records[0].detail == "size=1073741824 heap=1", "record detail was not normalized to one line");
            Require(stderrCapture.Read() == fallback, "registered callback also wrote stderr");

            plume::LogSite boundedSite;
            const std::string longDetail(5000, 'x');
            plume::LogMessage(boundedSite, plume::LogSeverity::Warning, "test", "bounded",
                plume::LogErrorDomain::None, 0, "%s", longDetail.c_str());
            Require(records.back().detail.size() == 1023, "detail buffer was not bounded");
            Require(records.back().detail.ends_with(" [truncated]"), "truncated detail lacks a marker");
            Require(records.back().severity == plume::LogSeverity::Warning, "warning severity changed");

            records.clear();
            plume::LogSite repeatedSite;
            for (unsigned i = 0; i < 65; ++i)
                plume::LogMessage(repeatedSite, plume::LogSeverity::Error, "Vulkan", "repeat",
                    plume::LogErrorDomain::VkResult, 0xFFFFFFFEu, "persistent failure");
            Require(records.size() == 11, "repeated errors were not bounded to first eight and powers of two");
            Require(records[7].occurrence == 8 && records[8].occurrence == 16 && records[10].occurrence == 64,
                "rate-limited events lost their occurrence counts");
            repeatedSite.count.store((std::numeric_limits<uint32_t>::max)(), std::memory_order_relaxed);
            plume::LogMessage(repeatedSite, plume::LogSeverity::Error, "test", "saturated",
                plume::LogErrorDomain::None, 0, "must remain suppressed");
            Require(records.size() == 11 && repeatedSite.count.load() == (std::numeric_limits<uint32_t>::max)(),
                "rate-limit counter wrapped and re-enabled flood output");

            plume::SetLogCallback(Throwing);
            PLUME_LOG_ERROR("test", "throwing", plume::LogErrorDomain::Win32, 6, "callback throws");
            Require(unusualCalls == 1, "throwing callback was retried");
            plume::SetLogCallback(Reentrant);
            PLUME_LOG_ERROR("test", "reentrant", plume::LogErrorDomain::None, 0, "callback recurses");
            Require(unusualCalls == 2, "callback recursion was not suppressed");
            Require(stderrCapture.Read() == fallback, "callback exception or recursion escaped into stderr");
            plume::SetLogCallback(Capture);
            PLUME_LOG_ERROR("test", "after_callback_failure", plume::LogErrorDomain::Win32, 6, "recovery");
            Require(records.back().api == "after_callback_failure" && records.back().rawCode == 6,
                "delivery guard did not recover after callback exception or recursion");

            plume::SetLogCallback(Concurrent);
            plume::LogSite concurrentSite;
            std::vector<std::thread> workers;
            for (unsigned t = 0; t < 4; ++t) workers.emplace_back([&] {
                for (unsigned i = 0; i < 64; ++i)
                    plume::LogMessage(concurrentSite, plume::LogSeverity::Error, "Vulkan", "concurrent",
                        plume::LogErrorDomain::VkResult, 0xFFFFFFFEu, "worker failure");
            });
            for (auto &worker : workers) worker.join();
            Require(concurrentSite.count.load() == 256 && concurrentCalls.load() == 13,
                "concurrent rate limiting lost or duplicated emissions");
            Require(concurrentCodesGood.load(), "concurrent callback data changed");
            Require(stderrCapture.Read() == fallback, "concurrent callback delivery leaked to stderr");

            plume::SetLogCallback(nullptr);
            PLUME_LOG_WARNING("Vulkan", "SDL_Vulkan_CreateSurface", plume::LogErrorDomain::SDL, 0, "surface unavailable");
            const std::string restored = stderrCapture.Read();
            Require(restored.starts_with(fallback) && restored.find("severity=warning backend=Vulkan") != std::string::npos,
                "unregistered callback did not restore stderr fallback");
            Require(restored.find("surface unavailable") != std::string::npos, "SDL fallback detail lost");
        }
        std::printf("PASS: %d Plume error routing, native-code, reentry, exception, bounded-output and concurrency checks.\n", checks);
        return 0;
    }
    catch (const std::exception &error) {
        std::fprintf(stderr, "Plume log fixture failed: %s\n", error.what());
        return 1;
    }
}
