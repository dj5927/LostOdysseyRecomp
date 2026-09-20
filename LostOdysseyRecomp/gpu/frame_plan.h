#pragma once

#include "gpu/render_resolution.h"
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>

struct PPCContext;

namespace gpu::frame_plan
{
    struct FramePlan
    {
        uint64_t cpuSerial = 0;
        uint64_t geometryEpoch = 0;
        uint32_t width = 1280;
        uint32_t height = 720;
        bool failed = false;
        bool operator==(const FramePlan&) const = default;
    };
    enum class SurfaceRole : uint32_t { Unknown = 0, Scene = 1, Fixed = 2 };

    inline FramePlan Choose(uint64_t serial, uint64_t epoch, uint32_t mode, uint32_t drawableWidth, uint32_t drawableHeight, bool resolveReadback = false)
    {
        if (resolveReadback) return { serial, epoch, 1280, 720, false };
        const auto size = resolution::ResolveInternalSize(mode, drawableWidth, drawableHeight);
        return { serial, epoch, size.width, size.height, false };
    }
    inline FramePlan Reduced(FramePlan plan, uint32_t fallbackHeight)
    {
        const uint32_t height = std::clamp(fallbackHeight, 720u, plan.height > 720 ? plan.height - 1 : 720u);
        plan.width = uint32_t((uint64_t(height) * plan.width + plan.height / 2) / plan.height);
        plan.height = height;
        return plan;
    }
    struct FailureState
    {
        FramePlan plan{};
        uint64_t requestSignature = 0;
        uint32_t cappedHeight = 0;
    };
    inline uint64_t RequestSignature(uint32_t mode, uint32_t drawableWidth, uint32_t drawableHeight, bool resolveReadback)
    {
        uint64_t signature = mode;
        signature = signature * 0x9E3779B185EBCA87ull + drawableWidth;
        signature = signature * 0x9E3779B185EBCA87ull + drawableHeight;
        return signature * 2 + resolveReadback + 1;
    }
    // The CPU owns retry sizing. A failure only caps the matching request, so a
    // later tick cannot recreate its original oversized plan before the GPU has
    // reported a different request.
    inline FramePlan AdvanceCpuPlan(FailureState& state, uint64_t cpuSerial, uint64_t& geometryEpoch,
        uint64_t reportedFailedEpoch, uint32_t fallbackHeight, uint32_t mode,
        uint32_t drawableWidth, uint32_t drawableHeight, bool resolveReadback)
    {
        const uint64_t signature = RequestSignature(mode, drawableWidth, drawableHeight, resolveReadback);
        if (state.requestSignature != signature)
        {
            state.requestSignature = signature;
            state.plan = {};
            state.cappedHeight = 0;
        }
        FramePlan requested = Choose(cpuSerial, geometryEpoch, mode, drawableWidth, drawableHeight, resolveReadback);
        if (state.cappedHeight && requested.height > state.cappedHeight)
            requested = Reduced(requested, state.cappedHeight);
        const bool failedCurrent = state.plan.cpuSerial && reportedFailedEpoch == state.plan.geometryEpoch;
        if (failedCurrent)
        {
            if (state.plan.height <= 720)
            {
                state.plan.cpuSerial = cpuSerial;
                state.plan.failed = true;
                return state.plan;
            }
            state.cappedHeight = std::min(state.plan.height - 1, std::max(720u, fallbackHeight));
            requested = Reduced(Choose(cpuSerial, geometryEpoch, mode, drawableWidth, drawableHeight, resolveReadback), state.cappedHeight);
        }
        if (!state.plan.cpuSerial || failedCurrent || requested.width != state.plan.width || requested.height != state.plan.height)
            ++geometryEpoch;
        requested.geometryEpoch = geometryEpoch;
        requested.failed = false;
        state.plan = requested;
        return state.plan;
    }

    class CommandTags
    {
    public:
        void Store(uint32_t address, FramePlan plan) { std::lock_guard lock(m_mutex); m_tags[address] = plan; }
        std::optional<FramePlan> Take(uint32_t address)
        {
            std::lock_guard lock(m_mutex);
            const auto found = m_tags.find(address);
            if (found == m_tags.end()) return std::nullopt;
            const auto plan = found->second;
            m_tags.erase(found);
            return plan;
        }
    private:
        std::mutex m_mutex;
        std::unordered_map<uint32_t, FramePlan> m_tags;
    };

    namespace wire
    {
        constexpr uint32_t PlanBase = 0x7F20;
        constexpr uint32_t CatalogBase = 0x7F40;
        constexpr uint32_t Magic = 0x4C4F4650; // "LOFP"
        constexpr uint32_t CatalogMagic = 0x4C4F4341; // "LOCA"
        struct PlanStage
        {
            bool active = false;
            FramePlan plan{};
            std::optional<FramePlan> Write(uint32_t index, uint32_t value)
            {
                if (index == PlanBase) { active = value == Magic; plan = {}; return std::nullopt; }
                if (!active || index < PlanBase || index > PlanBase + 7) return std::nullopt;
                switch (index - PlanBase) {
                case 1: plan.cpuSerial = (plan.cpuSerial & 0xFFFFFFFF00000000ull) | value; break;
                case 2: plan.cpuSerial = (plan.cpuSerial & 0xFFFFFFFFull) | (uint64_t(value) << 32); break;
                case 3: plan.geometryEpoch = (plan.geometryEpoch & 0xFFFFFFFF00000000ull) | value; break;
                case 4: plan.geometryEpoch = (plan.geometryEpoch & 0xFFFFFFFFull) | (uint64_t(value) << 32); break;
                case 5: plan.width = value; break;
                case 6: plan.height = value; break;
                case 7: active = false; if (value == Magic && plan.width && plan.height) return plan; break;
                }
                return std::nullopt;
            }
        };
        struct CatalogStage
        {
            bool active = false;
            SurfaceRole role = SurfaceRole::Unknown;
            uint32_t surfaceInfo = 0, colorInfo = 0;
            bool Write(uint32_t index, uint32_t value, SurfaceRole& committedRole, uint32_t& committedSurface, uint32_t& committedColor);
        };
    }

    void PublishDrawable(uint32_t width, uint32_t height);
    void BeginCpuFrame();
    FramePlan CpuPlan();
    std::optional<FramePlan> CurrentProducerPlan();
    void ReportFailedEpoch(uint64_t geometryEpoch, uint32_t fallbackHeight);
    void TagReservedCommand(uint32_t ring, uint32_t commandAddress);
    bool BeginRenderCommand(uint32_t commandAddress);
    void BeginTaggedRenderCommand(uint32_t commandAddress, uint32_t currentStack);
    void EndRenderCommand();
    std::optional<FramePlan> RenderPlan();
    void DeviceStreamReady(uint32_t device);
    void DeviceStreamReadyFromContext(uint32_t device, const PPCContext& context);
    void DeviceStreamDestroyed();
    void EnsureCurrentPlanQueued(uint32_t device, uint32_t currentStack);
    bool QueuePlanOnDevice(PPCContext& context, uint32_t device, const FramePlan& plan);
    bool QueueCatalogOnDevice(PPCContext& context, uint32_t device, SurfaceRole role, uint32_t surfaceInfo, uint32_t colorInfo);
    bool QueueMainDisplayCatalog(PPCContext& context, uint32_t device);
    // Shared host-private Type-0 writer. It copies a live PPC context, gives
    // the helper a private guest stack/backlink and never keeps host locks
    // while the guest command writer may roll over its ring.
    bool EmitPrivatePacket(uint32_t device, uint32_t registerBase, std::span<const uint32_t> words);
}
