#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace xenos::preparation
{
    inline size_t HostWorkerCap(unsigned logicalThreads)
    {
        if (const char* env = std::getenv("LO_SHADER_WORKERS")) {
            if (std::strcmp(env, "0") == 0 || std::strcmp(env, "max") == 0 || std::strcmp(env, "all") == 0)
                return logicalThreads;
            int parsed = std::atoi(env);
            if (parsed > 0) return static_cast<size_t>(parsed);
        }
#ifdef _WIN32
        MEMORYSTATUSEX status{sizeof(status)};
        if (GlobalMemoryStatusEx(&status)) {
            // Low-memory / handheld hosts (<= 16 GB physical RAM, e.g. Steam Deck, ROG Ally)
            // cap concurrent DXC workers to 4 to avoid OOM in shared-memory environments.
            if (status.ullTotalPhys <= 18ULL * 1024 * 1024 * 1024) {
                return 4u;
            }
            // High-memory desktop hosts have plenty of headroom for DXC working sets.
            return logicalThreads > 1 ? logicalThreads - 1 : 1u;
        }
#endif
        // Default conservative fallback on unknown platforms or without memory API
        return std::min<size_t>(4u, logicalThreads > 1 ? logicalThreads - 1 : 1u);
    }

    inline size_t WorkerCount(unsigned logicalThreads, size_t jobs, bool forceSerial, unsigned cap = 4u)
    {
        if (const char* env = std::getenv("LO_SHADER_WORKERS")) {
            if (std::strcmp(env, "0") == 0 || std::strcmp(env, "max") == 0 || std::strcmp(env, "all") == 0)
                return std::min<size_t>(logicalThreads, jobs);
            int parsed = std::atoi(env);
            if (parsed > 0) return std::min<size_t>(parsed, jobs);
        }
        if (forceSerial || jobs == 0) return jobs == 0 ? 0u : 1u;
        const unsigned requested = logicalThreads > 1 ? logicalThreads - 1 : 1u;
        const unsigned effective = cap > 0 ? std::min(requested, cap) : requested;
        return std::min<size_t>(effective, jobs);
    }

    struct QueueStats
    {
        size_t startedWorkers = 0;
        size_t startFailures = 0;
        size_t consumed = 0;
        size_t maxQueued = 0;
        bool cancelled = false;
        std::string startError;
    };

    struct ThreadLauncher
    {
        std::jthread operator()(std::function<void()> work) const
        {
            return std::jthread(std::move(work));
        }
    };

    // prepare runs only on worker threads (or the serial fallback); consume runs
    // only on the caller. Returning false from consume cancels outstanding work.
    // The bounded queue and cancellation wakeups keep both sides from waiting on
    // each other during normal completion, early cancellation, or an exception.
    template<typename Result, typename Prepare, typename Consume, typename Idle, typename Launch = ThreadLauncher>
    QueueStats RunBounded(size_t jobs, size_t requestedWorkers, size_t capacity,
        Prepare&& prepare, Consume&& consume, Idle&& idle, Launch&& launch = {})
    {
        QueueStats stats;
        if (!jobs) return stats;
        requestedWorkers = std::min(requestedWorkers, jobs);
        capacity = std::max<size_t>(1, capacity);

        std::atomic<size_t> next{0};
        std::atomic<bool> cancelled{false};
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<Result> ready;
        std::exception_ptr workerFailure;

        auto cancel = [&] {
            { std::lock_guard lock(mutex); cancelled = true; }
            changed.notify_all();
        };
        auto fail = [&](std::exception_ptr failure) {
            std::lock_guard lock(mutex);
            if (!workerFailure) workerFailure = failure;
            cancelled = true;
            changed.notify_all();
        };
        auto worker = [&] {
            try {
                for (;;) {
                    if (cancelled.load()) return;
                    const size_t index = next.fetch_add(1);
                    if (index >= jobs) return;
                    auto result = prepare(index);
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return cancelled.load() || ready.size() < capacity; });
                    if (cancelled.load()) return;
                    ready.emplace_back(std::move(result));
                    stats.maxQueued = std::max(stats.maxQueued, ready.size());
                    lock.unlock();
                    changed.notify_all();
                }
            } catch (...) {
                fail(std::current_exception());
            }
        };

        std::vector<std::jthread> workers;
        struct CancelBeforeWorkersJoin
        {
            std::atomic<bool>& cancelled;
            std::condition_variable& changed;
            std::mutex& mutex;
            bool armed = true;
            ~CancelBeforeWorkersJoin() noexcept
            {
                if (!armed) return;
                { std::lock_guard lock(mutex); cancelled = true; }
                changed.notify_all();
            }
        } cancelBeforeWorkersJoin{cancelled, changed, mutex};
        workers.reserve(requestedWorkers);
        try {
            for (size_t i = 0; i < requestedWorkers; ++i)
                workers.emplace_back(launch(std::function<void()>(worker)));
        } catch (const std::system_error& e) {
            ++stats.startFailures;
            stats.startError = e.what();
        }
        stats.startedWorkers = workers.size();

        try {
            if (workers.empty()) {
                for (size_t i = 0; i < jobs; ++i) {
                    if (!consume(prepare(i))) {
                        stats.cancelled = true;
                        break;
                    }
                    ++stats.consumed;
                }
            } else {
                while (stats.consumed < jobs && !cancelled.load()) {
                    Result result;
                    std::unique_lock lock(mutex);
                    if (!changed.wait_for(lock, std::chrono::milliseconds(10),
                        [&] { return cancelled.load() || !ready.empty(); })) {
                        lock.unlock();
                        idle();
                        continue;
                    }
                    if (ready.empty()) break;
                    result = std::move(ready.front());
                    ready.pop_front();
                    lock.unlock();
                    changed.notify_all();
                    if (!consume(std::move(result))) {
                        stats.cancelled = true;
                        cancel();
                        break;
                    }
                    ++stats.consumed;
                }
            }
        } catch (...) {
            cancel();
            for (auto& thread : workers) thread.join();
            throw;
        }

        for (auto& thread : workers) thread.join();
        cancelBeforeWorkersJoin.armed = false;
        if (workerFailure) std::rethrow_exception(workerFailure);
        return stats;
    }

    enum class CacheWriteStatus { Skipped, Saved, Failed };
    struct CacheWriteResult
    {
        CacheWriteStatus status = CacheWriteStatus::Skipped;
        std::string error;
    };

    // A failed compilation must leave the previous cache path untouched so the
    // shader remains retryable. A successful binary can still be used during
    // this launch when persistence fails; the caller reports that failure.
    inline CacheWriteResult WriteCompiledCache(const std::filesystem::path& path,
        std::span<const uint8_t> binary, bool compilationSucceeded)
    {
        if (!compilationSucceeded) return {};
        if (binary.empty()) return {CacheWriteStatus::Failed, "compiled shader binary is empty"};
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(binary.data()), binary.size());
        out.close();
        if (!out) return {CacheWriteStatus::Failed, "cannot write compiled shader cache"};
        return {CacheWriteStatus::Saved, {}};
    }
}
