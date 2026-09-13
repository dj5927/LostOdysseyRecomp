#pragma once

#include <cstdint>

namespace gpu::resolve_copy
{
    // Same-format color resolves copy mip/layer zero, with identical source and
    // destination coordinates. Allocation IDs must be nonzero and never reused.
    struct Copy
    {
        uint64_t sourceAllocation = 0, destinationAllocation = 0;
        uint32_t sourceWidth = 0, sourceHeight = 0;
        uint32_t destinationWidth = 0, destinationHeight = 0;
        uint32_t format = 0;
        uint32_t x = 0, y = 0, width = 0, height = 0;

        bool operator==(const Copy&) const = default;

        bool CanTrack() const
        {
            // Never infer safety for a self-copy, empty or out-of-bounds region.
            // Subtraction also avoids accepting wrapped x + width / y + height.
            return sourceAllocation && destinationAllocation &&
                sourceAllocation != destinationAllocation && width && height &&
                x < sourceWidth && y < sourceHeight &&
                x < destinationWidth && y < destinationHeight &&
                width <= sourceWidth - x && height <= sourceHeight - y &&
                width <= destinationWidth - x && height <= destinationHeight - y;
        }
    };

    // This is a record of adjacent resolve requests, not a texture-content cache.
    // The caller must invalidate before every possible intervening texture write,
    // allocation change, command-batch boundary or external resource access.
    class ConsecutiveCopies
    {
        Copy previous{};
        bool valid = false;
        bool recordedThisResolve = false;

    public:
        void BeginResolve() { recordedThisResolve = false; }

        bool CanReuse(const Copy& copy) const
        {
            return valid && copy.CanTrack() && copy == previous;
        }

        // Call only after recording the copy (or proving the identical copy is
        // reusable). Resolve metadata and post-copy clears remain the caller's job.
        void Record(const Copy& copy)
        {
            previous = copy;
            valid = copy.CanTrack();
            recordedThisResolve = true;
        }

        void Invalidate()
        {
            valid = false;
            recordedThisResolve = false;
        }

        void EndResolve()
        {
            // Failed, empty, depth, readback and converted resolves break adjacency.
            if (!recordedThisResolve) Invalidate();
        }
    };
}
