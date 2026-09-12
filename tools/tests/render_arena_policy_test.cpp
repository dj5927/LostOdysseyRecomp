#include "../../LostOdysseyRecomp/gpu/render_arena_policy.h"

#include <cstdio>

namespace {
unsigned checks = 0;
unsigned failures = 0;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}
}

int main() {
    using gpu::render_arena::EvaluateWrap;
    using gpu::render_arena::SlotBase;
    using gpu::render_arena::SlotLow;
    using gpu::render_arena::WrapAction;
    using gpu::render_arena::kArenaHeadroom;
    using gpu::render_arena::kGpuSlots;
    using gpu::render_arena::kSlotArenaSize;
    using gpu::render_arena::kVertexArenaSize;

    Check(kGpuSlots == 2, "two GPU slots");
    Check(kSlotArenaSize * kGpuSlots == kVertexArenaSize, "halves cover the arena");
    Check(SlotBase(0) == 0, "slot 0 base");
    Check(SlotBase(1) == kSlotArenaSize, "slot 1 base");
    Check(!SlotLow(0), "empty slot is not low");
    Check(!SlotLow(kSlotArenaSize - kArenaHeadroom), "exactly at headroom is not low");
    Check(SlotLow(kSlotArenaSize - kArenaHeadroom + 1), "one byte past headroom is low");

    const auto idle = EvaluateWrap(0, 0, 0);
    Check(idle.action == WrapAction::None, "room left does not wrap");
    Check(idle.resetIncoming == false, "idle wrap does not reset");
    Check(idle.incomingSlot == 1, "incoming is the other slot");

    const uint64_t low = kSlotArenaSize - kArenaHeadroom + 1;
    const auto wrapKeep = EvaluateWrap(0, low, 0);
    Check(wrapKeep.action == WrapAction::FlushAndRecycleIncoming, "current low wraps");
    Check(wrapKeep.resetIncoming == false, "incoming with room is not reset");
    Check(wrapKeep.incomingSlot == 1, "wrap recycles slot 1 from slot 0");

    const auto wrapReset = EvaluateWrap(1, low, low);
    Check(wrapReset.action == WrapAction::FlushAndRecycleIncoming, "both low still wraps one slot");
    Check(wrapReset.resetIncoming == true, "incoming low is reset after recycle");
    Check(wrapReset.incomingSlot == 0, "wrap from slot 1 recycles slot 0");
    Check(wrapReset.incomingSlot != 1, "never recycle the current slot as incoming");

    std::printf("render_arena_policy: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
