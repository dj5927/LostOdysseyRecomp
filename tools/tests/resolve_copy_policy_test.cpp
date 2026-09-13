#include "../../LostOdysseyRecomp/gpu/resolve_copy_policy.h"

#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool condition, const char* name)
    {
        ++checks;
        if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
    }

    constexpr gpu::resolve_copy::Copy captureCopy{1, 2, 3840, 2208, 3840, 2160, 10, 0, 0, 3840, 2160};

    void Record(gpu::resolve_copy::ConsecutiveCopies& copies, const gpu::resolve_copy::Copy& copy)
    {
        copies.BeginResolve();
        copies.Record(copy);
        copies.EndResolve();
    }
}

int main()
{
    using namespace gpu::resolve_copy;
    ConsecutiveCopies copies;
    Check(captureCopy.CanTrack(), "captured full-resolution copy fits padded source");
    Check(!copies.CanReuse(captureCopy), "first resolve must copy");
    Record(copies, captureCopy);
    copies.BeginResolve();
    Check(copies.CanReuse(captureCopy), "identical adjacent resolve can reuse");
    copies.Record(captureCopy);
    copies.EndResolve();
    Check(copies.CanReuse(captureCopy), "reuse can continue through a third identical resolve");

    // Allocation identity, format and all physical copy parameters participate.
    auto changed = captureCopy;
    changed.sourceAllocation = 3;
    Check(!copies.CanReuse(changed), "recreated source cannot reuse stale pixels");
    changed = captureCopy; changed.destinationAllocation = 4;
    Check(!copies.CanReuse(changed), "recreated destination must be initialized by a copy");
    changed = captureCopy; ++changed.format;
    Check(!copies.CanReuse(changed), "different host format cannot reuse");
    changed = captureCopy; ++changed.sourceHeight;
    Check(!copies.CanReuse(changed), "changed source extent cannot reuse");
    changed = captureCopy; ++changed.destinationHeight;
    Check(!copies.CanReuse(changed), "changed destination extent cannot reuse");
    changed = captureCopy; changed.width = 100; changed.height = 100;
    Record(copies, changed);
    auto overlapping = changed; overlapping.x = 50;
    Check(!copies.CanReuse(overlapping), "overlapping regions are not identical copies");
    Record(copies, overlapping);
    Check(!copies.CanReuse(changed), "only the immediately preceding region may reuse");
    auto shifted = overlapping; ++shifted.y;
    Check(!copies.CanReuse(shifted), "vertical offset participates");

    for (const char* boundary : {"draw", "source clear", "destination write", "EDRAM transfer",
                                  "allocation", "flush", "new batch", "external invalidation"})
    {
        Record(copies, captureCopy);
        copies.Invalidate();
        Check(!copies.CanReuse(captureCopy), boundary);
    }
    Record(copies, captureCopy);
    copies.BeginResolve();
    copies.EndResolve();
    Check(!copies.CanReuse(captureCopy), "failed or unsupported resolve breaks adjacency");
    copies.BeginResolve();
    copies.Record(captureCopy);
    copies.Invalidate(); // A post-resolve clear, or a debug dump submitting the batch.
    copies.EndResolve();
    Check(!copies.CanReuse(captureCopy), "EndResolve cannot revive a post-copy invalidation");

    changed = captureCopy; changed.destinationAllocation = changed.sourceAllocation;
    Check(!changed.CanTrack(), "self-copy is never a reuse candidate");
    changed = captureCopy; changed.sourceAllocation = 0;
    Check(!changed.CanTrack(), "unknown source allocation cannot track");
    changed = captureCopy; changed.destinationAllocation = 0;
    Check(!changed.CanTrack(), "unknown destination allocation cannot track");
    changed = captureCopy; changed.width = 0;
    Check(!changed.CanTrack(), "empty copy cannot track");
    changed = captureCopy; changed.x = std::numeric_limits<uint32_t>::max();
    Check(!changed.CanTrack(), "wrapped horizontal end cannot pass bounds checks");
    changed = captureCopy; changed.sourceHeight = 2159;
    Check(!changed.CanTrack(), "source bounds are checked independently");
    changed = captureCopy; changed.destinationHeight = 2159;
    Check(!changed.CanTrack(), "destination bounds are checked independently");
    Record(copies, changed);
    Check(!copies.CanReuse(changed), "recording an invalid copy does not enable reuse");

    std::printf("resolve_copy_policy: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
