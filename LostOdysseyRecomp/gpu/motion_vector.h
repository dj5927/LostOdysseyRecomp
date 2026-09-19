#pragma once

#include "temporal_math.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace gpu::temporal {

// Backward, unjittered displacement in physical render pixels (+X right, +Y down).
// Invalid is NOT a stationary surface. Keep validity separate from the two floats.
struct MotionVector2D { float x = 0, y = 0; bool valid = false; };
struct MotionVectorPixel { float vx = 0, vy = 0, depth = 0, reactiveMask = 1; };
inline MotionVector2D ReprojectMotionVector(const Sample& sample, const Camera& current, const Camera& previous) {
    const auto r = Reproject(sample, current, previous);
    if (!r) return {};
    return {float(r.previous.x - sample.x), float(r.previous.y - sample.y), true};
}

struct DrawHistoryKey {
    uint64_t vsHash = 0, psHash = 0;
    uint64_t sceneAllocation = 0;
    // Identity of ALL actual bound vertex streams and converted index contents.
    // Arena generation is part of this signature; recycled addresses are not history.
    uint64_t geometrySignature = 0;
    uint32_t indexBufferAddress = 0, positionBufferAddress = 0;
    uint32_t firstIndex = 0, indexCount = 0;
    int32_t baseVertex = 0;
    uint32_t primitiveType = 0;
    bool operator==(const DrawHistoryKey&) const = default;
};
[[nodiscard]] inline uint64_t MotionHashWord(uint64_t h, uint64_t v) {
    // Hash table accelerator, not a replacement for key equality.
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27; v *= 0x94d049bb133111ebULL; v ^= v >> 31;
    return (h ^ v) * 0x100000001b3ULL;
}
struct DrawHistoryKeyHasher {
    size_t operator()(const DrawHistoryKey& k) const noexcept {
        uint64_t h = MotionHashWord(k.vsHash, k.psHash);
        for (uint64_t v : {k.sceneAllocation, k.geometrySignature, uint64_t(k.indexBufferAddress),
             uint64_t(k.positionBufferAddress), uint64_t(k.firstIndex), uint64_t(k.indexCount),
             uint64_t(uint32_t(k.baseVertex)), uint64_t(k.primitiveType)}) h = MotionHashWord(h, v);
        return size_t(h);
    }
};

struct DrawTemporalState {
    DrawHistoryKey key;
    std::array<float, 1024> vsConstants;
    // Exact first 208 bytes of XeShared: bool/loop banks, viewport transform,
    // fixed half pixel, VTE and flags. Fetch offsets remain those of CURRENT geometry.
    std::array<uint32_t, 52> shared;
    uint32_t previousIndex = UINT32_MAX;
    bool usesRelativeConstants = false, valid = false;
    DrawTemporalState() noexcept {} // overwritten by memcpy; do not zero 4 KiB per insertion
};

// Bounded open-addressed tables + reusable contiguous snapshots: no per-draw
// unordered_map nodes, no per-frame move/destruction of thousands of allocations.
// A provisional match MUST only be consumed with the finalized per-tag validity.
class DrawTemporalTracker {
    struct Table {
        std::vector<DrawTemporalState> draws;
        std::vector<uint32_t> slots; // 0 = empty; otherwise index + 1
        void Prepare(size_t limit) {
            if (slots.empty()) {
                size_t n = 1; while (n < limit * 2) n <<= 1;
                slots.resize(n); draws.reserve(limit);
            }
            draws.clear(); std::fill(slots.begin(), slots.end(), 0);
        }
        uint32_t Slot(const DrawHistoryKey& key) const {
            if (slots.empty()) return UINT32_MAX;
            size_t i = DrawHistoryKeyHasher{}(key) & (slots.size() - 1);
            while (slots[i] && !(draws[slots[i] - 1].key == key)) i = (i + 1) & (slots.size() - 1);
            return uint32_t(i);
        }
        uint32_t Find(const DrawHistoryKey& key) const {
            const auto s = Slot(key); return s == UINT32_MAX || !slots[s] ? UINT32_MAX : slots[s] - 1;
        }
    } tables_[2];
    size_t limit_;
    unsigned current_ = 0;
    uint64_t frame_ = 0, epoch_ = 0;
    bool begun_ = false, finalized_ = false, failed_ = false;
    std::vector<uint32_t> validity_;
public:
    static constexpr size_t kMaxDraws = 8192;
    struct DiagnosticsStats {
        uint64_t frame = 0;
        uint32_t sceneDrawCount = 0, trackedCurrentDraws = 0, matchedPreviousDraws = 0;
        uint32_t unmatchedDraws = 0, ambiguousRejectedMatches = 0, directConstantMatches = 0, relativeConstantMatches = 0;
        uint32_t overflowDraws = 0, lateDraws = 0;
        uint64_t snapshotBytes = 0;
    };
    struct Match { uint32_t tag = 0; const DrawTemporalState* previous = nullptr; };
    explicit DrawTemporalTracker(size_t limit = kMaxDraws) : limit_(std::clamp(limit, size_t(1), kMaxDraws)) {}
    void BeginFrame(uint64_t frame, uint64_t epoch = 0) {
        if (begun_ && frame == frame_ && epoch == epoch_) return;
        if (begun_ && frame == frame_ + 1 && epoch == epoch_ && finalized_ && !failed_) current_ ^= 1;
        else { tables_[0].Prepare(limit_); tables_[1].Prepare(limit_); }
        tables_[current_].Prepare(limit_);
        frame_ = frame; epoch_ = epoch; begun_ = true; finalized_ = failed_ = false;
        stats_ = {}; stats_.frame = frame;
        validity_.clear();
        if (validity_.capacity() < limit_ + 1) validity_.reserve(limit_ + 1);
    }
    void Invalidate() { failed_ = true; std::fill(validity_.begin(), validity_.end(), 0); }
    Match Collect(const DrawHistoryKey& key, const void* constants, const void* sharedPrefix, bool relative,
        int restoredSlot = -1, const void* originalVP = nullptr) {
        if (!begun_ || finalized_) { ++stats_.lateDraws; Invalidate(); return {}; }
        ++stats_.trackedCurrentDraws;
        if (failed_) return {};
        auto& cur = tables_[current_]; const auto& prev = tables_[current_ ^ 1];
        const auto slot = cur.Slot(key);
        if (cur.slots[slot]) {
            cur.draws[cur.slots[slot] - 1].valid = false;
            ++stats_.ambiguousRejectedMatches;
            return {cur.slots[slot], nullptr};
        }
        if (cur.draws.size() == limit_) { ++stats_.overflowDraws; Invalidate(); return {}; }
        const uint32_t index = uint32_t(cur.draws.size());
        auto& s = cur.draws.emplace_back();
        s.key = key; s.usesRelativeConstants = relative; s.valid = constants && sharedPrefix &&
            (restoredSlot == -1 || (restoredSlot >= 0 && restoredSlot <= 252 && originalVP));
        s.previousIndex = prev.Find(key);
        if (constants) std::memcpy(s.vsConstants.data(), constants, sizeof(s.vsConstants));
        else s.vsConstants.fill(0);
        // ApplyDrawJitter only modifies this proven 4x4 window. Snapshot the full
        // bank ONCE, then restore these original bits; no bone-window assumption.
        if (s.valid && restoredSlot >= 0) {
            std::memcpy(s.vsConstants.data() + restoredSlot * 4, originalVP, 16 * sizeof(uint32_t));
            stats_.snapshotBytes += 16 * sizeof(uint32_t);
        }
        if (constants) stats_.snapshotBytes += sizeof(s.vsConstants);
        if (sharedPrefix) stats_.snapshotBytes += sizeof(s.shared);
        if (sharedPrefix) std::memcpy(s.shared.data(), sharedPrefix, sizeof(s.shared));
        else s.shared.fill(0);
        cur.slots[slot] = index + 1;
        const auto* p = s.previousIndex != UINT32_MAX && prev.draws[s.previousIndex].valid
            ? &prev.draws[s.previousIndex] : nullptr;
        return {index + 1, s.valid ? p : nullptr};
    }
    // Compatibility entry for CPU fixtures. Production uses Collect with complete shared prefix.
    const DrawTemporalState* RecordDraw(const DrawHistoryKey& key, const float* c,
        const uint32_t* b = nullptr, const uint32_t* l = nullptr, bool relative = false) {
        std::array<uint32_t, 52> shared{};
        if (b) std::memcpy(shared.data(), b, 8 * sizeof(uint32_t));
        if (l) std::memcpy(shared.data() + 8, l, 32 * sizeof(uint32_t));
        return Collect(key, c, shared.data(), relative).previous;
    }
    const std::vector<uint32_t>& FinalizeFrame() {
        if (finalized_) return validity_;
        finalized_ = true;
        const auto& cur = tables_[current_]; const auto& prev = tables_[current_ ^ 1];
        validity_.assign(cur.draws.size() + 1, 0);
        stats_.sceneDrawCount = stats_.trackedCurrentDraws;
        for (size_t i = 0; i < cur.draws.size(); ++i) {
            const auto& s = cur.draws[i];
            const bool accept = !failed_ && s.valid && s.previousIndex != UINT32_MAX && prev.draws[s.previousIndex].valid;
            if (accept) {
                validity_[i + 1] = 1; ++stats_.matchedPreviousDraws;
                if (s.usesRelativeConstants) ++stats_.relativeConstantMatches; else ++stats_.directConstantMatches;
            } else ++stats_.unmatchedDraws;
        }
        return validity_;
    }
    const DrawTemporalState* FindPrevious(const DrawHistoryKey& key) const {
        if (failed_) return nullptr;
        const auto c = tables_[current_].Find(key);
        if (c != UINT32_MAX && !tables_[current_].draws[c].valid) return nullptr;
        const auto& p = tables_[current_ ^ 1]; const auto i = p.Find(key);
        return i != UINT32_MAX && p.draws[i].valid ? &p.draws[i] : nullptr;
    }
    bool Failed() const { return failed_; }
    bool Finalized() const { return finalized_; }
    size_t ActiveDrawCount() const { return tables_[current_].draws.size(); }
    const DiagnosticsStats& Stats() const { return stats_; }
private:
    DiagnosticsStats stats_{};
};

// CPU camera-only oracle. This is deliberately NOT an object-MV implementation.
struct MotionVectorProducerDesc {
    uint32_t width = 0, height = 0;
    float maxValidVelocityPixels = std::numeric_limits<float>::infinity();
};
class MotionVectorProducer {
    MotionVectorProducerDesc desc_;
public:
    explicit MotionVectorProducer(MotionVectorProducerDesc d = {}) : desc_(d) {}
    void SetDesc(MotionVectorProducerDesc d) { desc_ = d; }
    const MotionVectorProducerDesc& Desc() const { return desc_; }
    struct ProducerStats { uint32_t totalPixels = 0, cameraReprojectedPixels = 0, rejectedPixels = 0, reactivePixels = 0; float maxDisplacementObserved = 0; };
    bool EvaluateGrid(const float* depth, const Camera* current, const Camera* previous,
        const DrawTemporalTracker*, std::vector<MotionVectorPixel>& out, ProducerStats* statsOut = nullptr) const {
        if (!depth || !current || !desc_.width || !desc_.height || desc_.width > 16384 || desc_.height > 16384 ||
            current->Raster().width != desc_.width || current->Raster().height != desc_.height) return false;
        const size_t n = size_t(desc_.width) * desc_.height;
        out.assign(n, {}); ProducerStats st{}; st.totalPixels = uint32_t(n);
        for (uint32_t y = 0; y < desc_.height; ++y) for (uint32_t x = 0; x < desc_.width; ++x) {
            auto& p = out[size_t(y) * desc_.width + x]; p.depth = depth[size_t(y) * desc_.width + x];
            if (!previous) { ++st.reactivePixels; continue; }
            const auto mv = ReprojectMotionVector({current->Raster().x + x + .5, current->Raster().y + y + .5, p.depth}, *current, *previous);
            if (!mv.valid) { ++st.rejectedPixels; ++st.reactivePixels; continue; }
            p.vx = mv.x; p.vy = mv.y;
            const float length = std::hypot(mv.x, mv.y);
            st.maxDisplacementObserved = std::max(st.maxDisplacementObserved, length);
            p.reactiveMask = length > desc_.maxValidVelocityPixels ? 1 : 0;
            st.reactivePixels += p.reactiveMask > 0; ++st.cameraReprojectedPixels;
        }
        if (statsOut) *statsOut = st;
        return true;
    }
};
}
