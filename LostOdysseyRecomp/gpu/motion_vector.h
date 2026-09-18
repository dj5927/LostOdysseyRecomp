#pragma once

#include "temporal_math.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>

namespace gpu::temporal {

// Motion vector representation:
// - Direction: Backward (Current Pixel -> Previous Pixel)
// - Coordinate space: Render-pixel displacement (+X right, +Y down)
// - Precision: 16-bit float per component
// - Jitter convention: Geometric motion only (unjittered current -> unjittered previous)
struct MotionVector2D {
    float x = 0.0f; // previous.x - current.x (in pixels)
    float y = 0.0f; // previous.y - current.y (in pixels)
};

struct MotionVectorPixel {
    float vx = 0.0f;
    float vy = 0.0f;
    float depth = 0.0f;       // Host reverse depth [0..1]
    float reactiveMask = 0.0f; // 0.0 = static/normal, 1.0 = reactive / reject history
};

// Calculate camera-reprojected backward motion vector for a single pixel sample.
// Returns {vx, vy} in pixel units, or {0, 0} if invalid or rejected.
inline MotionVector2D ReprojectMotionVector(
    const Sample& sample,
    const Camera& current,
    const Camera& previous)
{
    const auto result = Reproject(sample, current, previous);
    if (!result) {
        return {0.0f, 0.0f};
    }
    return {
        static_cast<float>(result.previous.x - sample.x),
        static_cast<float>(result.previous.y - sample.y)
    };
}

// Draw history key for temporal correspondence tracking across frames
struct DrawHistoryKey {
    uint64_t vsHash = 0;
    uint32_t baseVertex = 0;
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    uint32_t vertexFetchAddress = 0;

    bool operator==(const DrawHistoryKey& other) const {
        return vsHash == other.vsHash &&
               baseVertex == other.baseVertex &&
               startIndex == other.startIndex &&
               indexCount == other.indexCount &&
               vertexFetchAddress == other.vertexFetchAddress;
    }
};

struct DrawHistoryKeyHasher {
    std::size_t operator()(const DrawHistoryKey& k) const noexcept {
        // FNV-1a composite hash
        std::size_t h = 14695981039346656037ULL;
        auto step = [&h](uint64_t val) {
            h ^= val;
            h *= 1099511628211ULL;
        };
        step(k.vsHash);
        step(k.baseVertex);
        step(k.startIndex);
        step(k.indexCount);
        step(k.vertexFetchAddress);
        return h;
    }
};

// Temporal object transform tracking record
struct DrawTemporalState {
    DrawHistoryKey key;
    std::array<float, 16> currentVP{};
    std::array<float, 16> previousVP{};
    std::vector<float> currentBones{};
    std::vector<float> previousBones{};
    uint64_t lastObservedFrame = 0;
    bool validPrevious = false;
};

// Tracker class managing the frame-to-frame ring buffer of observed draws
class DrawTemporalTracker {
public:
    void BeginFrame(uint64_t frameIndex) {
        currentFrameIndex_ = frameIndex;
        // Purge stale draw states older than 2 frames
        for (auto it = drawHistory_.begin(); it != drawHistory_.end(); ) {
            if (currentFrameIndex_ > it->second.lastObservedFrame + 2) {
                it = drawHistory_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Record draw observation and retrieve previous state if available
    const DrawTemporalState* RecordDraw(
        const DrawHistoryKey& key,
        const float* vpMatrix4x4,
        const float* boneFloats = nullptr,
        uint32_t boneFloatCount = 0)
    {
        auto& state = drawHistory_[key];
        state.key = key;

        if (state.lastObservedFrame == currentFrameIndex_ - 1) {
            // Consecutive frame: promote current to previous
            state.previousVP = state.currentVP;
            state.previousBones = std::move(state.currentBones);
            state.validPrevious = true;
        } else if (state.lastObservedFrame != currentFrameIndex_) {
            // Non-consecutive: invalid previous state
            state.validPrevious = false;
            state.previousBones.clear();
        }

        if (vpMatrix4x4) {
            std::copy(vpMatrix4x4, vpMatrix4x4 + 16, state.currentVP.begin());
        } else {
            state.currentVP.fill(0.0f);
        }

        if (boneFloats && boneFloatCount > 0) {
            state.currentBones.assign(boneFloats, boneFloats + boneFloatCount);
        } else {
            state.currentBones.clear();
        }

        state.lastObservedFrame = currentFrameIndex_;
        return &state;
    }

    const DrawTemporalState* Find(const DrawHistoryKey& key) const {
        auto it = drawHistory_.find(key);
        if (it != drawHistory_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    size_t ActiveDrawCount() const {
        return drawHistory_.size();
    }

private:
    uint64_t currentFrameIndex_ = 0;
    std::unordered_map<DrawHistoryKey, DrawTemporalState, DrawHistoryKeyHasher> drawHistory_;
};

} // namespace gpu::temporal
