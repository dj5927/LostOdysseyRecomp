#pragma once

#include <cstdint>

namespace gpu::render_arena
{
    inline constexpr uint32_t kGpuSlots = 2;
    inline constexpr uint64_t kVertexArenaSize = 256ull << 20;
    inline constexpr uint64_t kSlotArenaSize = kVertexArenaSize / kGpuSlots;
    inline constexpr uint64_t kArenaHeadroom = 32ull << 20;

    inline uint64_t SlotBase(uint32_t slot)
    {
        return uint64_t(slot) * kSlotArenaSize;
    }

    inline bool SlotLow(uint64_t localOffset)
    {
        return localOffset + kArenaHeadroom > kSlotArenaSize;
    }

    enum class WrapAction : uint8_t { None, FlushAndRecycleIncoming };

    struct WrapDecision
    {
        WrapAction action = WrapAction::None;
        bool resetIncoming = false;
        uint32_t incomingSlot = 0;
    };

    // Current slot wrapping submits that slot and waits only the incoming one.
    // Reset the incoming half only when it is also low. Never drain both slots.
    inline WrapDecision EvaluateWrap(uint32_t currentSlot, uint64_t currentLocalOffset, uint64_t incomingLocalOffset)
    {
        WrapDecision decision;
        decision.incomingSlot = (currentSlot + 1u) % kGpuSlots;
        if (!SlotLow(currentLocalOffset))
            return decision;
        decision.action = WrapAction::FlushAndRecycleIncoming;
        decision.resetIncoming = SlotLow(incomingLocalOffset);
        return decision;
    }
}
