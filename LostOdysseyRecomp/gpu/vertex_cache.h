#pragma once

#include "geometry_prepare.h"
#include <ankerl/unordered_dense.h>
#include <algorithm>

namespace gpu::geometry_prepare
{
    struct VertexEntry
    {
        uint64_t offset;
        ExactContent content;
        uint64_t lastFrame;
        uint8_t slot = 0;
    };

    // This is lookup metadata, not ownership of arena bytes. Discarding an
    // entry makes a future fetch upload again; recorded GPU offsets stay valid.
    // Reserve a bounded working set once, so city exploration cannot trigger a
    // whole-table rehash (262K -> 524K buckets cost 42 ms in the city capture).
    class VertexCache
    {
        using Map = ankerl::unordered_dense::map<uint64_t, VertexEntry>;
        Map entries;
        size_t capacity;
        size_t byteCapacity;
        size_t capturedBytes = 0;
        size_t evictionCursor = 0;
        uint64_t evictions = 0;

    public:
        static constexpr size_t kCapacity = 65536;
        static constexpr size_t kEvictionCandidates = 16;
        static constexpr size_t kByteCapacity = 256ull << 20;
        explicit VertexCache(size_t limit = kCapacity, size_t bytes = kByteCapacity)
            : capacity(std::max(size_t(1), limit)), byteCapacity(bytes)
        {
            entries.reserve(capacity);
        }
        auto begin() { return entries.begin(); }
        auto end() { return entries.end(); }
        auto find(uint64_t key) { return entries.find(key); }
        auto erase(Map::iterator it) {
            capturedBytes -= it->second.content.Size();
            return entries.erase(it);
        }
        size_t CapturedBytes() const { return capturedBytes; }
        size_t size() const { return entries.size(); }
        size_t bucket_count() const { return entries.bucket_count(); }
        uint64_t Evictions() const { return evictions; }

        void emplace(uint64_t key, VertexEntry&& entry)
        {
            // Caller already checked/removed this key; no iterators or entry
            // references survive insertion. Sample a rotating bounded window,
            // keeping recently used buffers without a per-hit LRU list update.
            const size_t bytes = entry.content.Size();
            // Oversized buffers remain usable for this draw, but are not cached.
            if (bytes > byteCapacity) return;
            while (!entries.empty() &&
                (entries.size() == capacity || capturedBytes > byteCapacity - bytes))
            {
                size_t victim = evictionCursor % entries.size();
                const size_t count = std::min(kEvictionCandidates, entries.size());
                for (size_t i = 1; i < count; ++i)
                {
                    const size_t candidate = (evictionCursor + i) % entries.size();
                    if ((entries.begin() + candidate)->second.lastFrame <
                        (entries.begin() + victim)->second.lastFrame)
                        victim = candidate;
                }
                evictionCursor = (evictionCursor + count) % entries.size();
                erase(entries.begin() + victim);
                ++evictions;
            }
            const auto [it, inserted] = entries.emplace(key, std::move(entry));
            if (inserted) capturedBytes += bytes;
        }
    };
}
