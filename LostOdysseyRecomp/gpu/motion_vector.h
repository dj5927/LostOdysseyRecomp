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
    uint32_t indexBufferAddress = 0;
    uint32_t positionBufferAddress = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t  baseVertex = 0;
    uint32_t primitiveType = 0;

    bool operator==(const DrawHistoryKey& other) const {
        return vsHash == other.vsHash &&
               indexBufferAddress == other.indexBufferAddress &&
               positionBufferAddress == other.positionBufferAddress &&
               firstIndex == other.firstIndex &&
               indexCount == other.indexCount &&
               baseVertex == other.baseVertex &&
               primitiveType == other.primitiveType;
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
        step(k.indexBufferAddress);
        step(k.positionBufferAddress);
        step(k.firstIndex);
        step(k.indexCount);
        step(static_cast<uint32_t>(k.baseVertex));
        step(k.primitiveType);
        return h;
    }
};

// Temporal draw state storing full VS inputs for previous position replay
struct DrawTemporalState {
    DrawHistoryKey key;
    std::array<float, 256 * 4> vsConstants{}; // 256 float4 vector constants (4096 bytes)
    std::array<uint32_t, 8> boolConstants{};
    std::array<uint32_t, 32> loopConstants{};
    uint64_t lastObservedFrame = 0;
    bool isSkinned = false;
    bool valid = false;
};

// Double-buffered draw temporal tracker: cleanly isolates current and previous frames
class DrawTemporalTracker {
public:
    void BeginFrame(uint64_t frameIndex, uint64_t epoch = 0) {
        if (frameIndex == currentFrameIndex_ && epoch == currentEpoch_ && hasBegunFrame_) {
            return;
        }
        if (epoch == currentEpoch_ && frameIndex == currentFrameIndex_ + 1 && hasBegunFrame_) {
            previousDraws_ = std::move(currentDraws_);
        } else {
            previousDraws_.clear();
        }
        currentDraws_.clear();
        currentFrameIndex_ = frameIndex;
        currentEpoch_ = epoch;
        hasBegunFrame_ = true;
        stats_ = DiagnosticsStats{};
        stats_.frame = frameIndex;
    }

    struct DiagnosticsStats {
        uint64_t frame = 0;
        uint32_t sceneDrawCount = 0;
        uint32_t trackedCurrentDraws = 0;
        uint32_t matchedPreviousDraws = 0;
        uint32_t unmatchedDraws = 0;
        uint32_t ambiguousRejectedMatches = 0;
        uint32_t rigidMatches = 0;
        uint32_t skinnedMatches = 0;
    };

    // Stage 1: Collect draw in current frame during rasterization.
    // Immediate recording of the draw state. Collisions within the same frame mark the key invalid.
    const DrawTemporalState* RecordDraw(
        const DrawHistoryKey& key,
        const float* vsFloatConstants, // 256 float4 (1024 floats)
        const uint32_t* boolConstants = nullptr,
        const uint32_t* loopConstants = nullptr,
        bool isSkinned = false)
    {
        stats_.trackedCurrentDraws++;

        // Detect multiple uses of identical key within the current frame (ambiguous instances)
        auto [it, inserted] = currentDraws_.try_emplace(key);
        auto& currentEntry = it->second;
        if (!inserted) {
            // Collision within same frame: mark ambiguous/invalid to prevent wrong matching
            currentEntry.valid = false;
            stats_.ambiguousRejectedMatches++;
            return nullptr;
        }

        currentEntry.key = key;
        currentEntry.lastObservedFrame = currentFrameIndex_;
        currentEntry.isSkinned = isSkinned;
        currentEntry.valid = true;

        if (vsFloatConstants) {
            std::copy_n(vsFloatConstants, 256 * 4, currentEntry.vsConstants.data());
        } else {
            currentEntry.vsConstants.fill(0.0f);
        }

        if (boolConstants) {
            std::copy_n(boolConstants, 8, currentEntry.boolConstants.data());
        } else {
            currentEntry.boolConstants.fill(0);
        }

        if (loopConstants) {
            std::copy_n(loopConstants, 32, currentEntry.loopConstants.data());
        } else {
            currentEntry.loopConstants.fill(0);
        }

        // Look up previous frame state
        auto prevIt = previousDraws_.find(key);
        if (prevIt != previousDraws_.end() && prevIt->second.valid) {
            stats_.matchedPreviousDraws++;
            if (isSkinned) {
                stats_.skinnedMatches++;
            } else {
                stats_.rigidMatches++;
            }
            return &prevIt->second;
        } else {
            stats_.unmatchedDraws++;
            return nullptr;
        }
    }

    // Stage 2: Finalize frame collection and freeze matching state.
    // Invalidate any ambiguous duplicate keys across frame boundaries.
    void FinalizeFrame() {
        stats_.sceneDrawCount = stats_.trackedCurrentDraws;
    }

    const DrawTemporalState* FindPrevious(const DrawHistoryKey& key) const {
        auto it = previousDraws_.find(key);
        if (it != previousDraws_.end() && it->second.valid) {
            return &it->second;
        }
        return nullptr;
    }

    const DiagnosticsStats& Stats() const {
        return stats_;
    }

    size_t ActiveDrawCount() const {
        return currentDraws_.size();
    }

private:
    uint64_t currentFrameIndex_ = 0;
    uint64_t currentEpoch_ = 0;
    bool hasBegunFrame_ = false;
    std::unordered_map<DrawHistoryKey, DrawTemporalState, DrawHistoryKeyHasher> currentDraws_;
    std::unordered_map<DrawHistoryKey, DrawTemporalState, DrawHistoryKeyHasher> previousDraws_;
    DiagnosticsStats stats_{};
};

// Motion Vector Producer context and evaluation structure.
// Produces per-pixel motion vectors with:
// 1. Static camera reprojection fallback (from depth buffer + camera matrices)
// 2. Object/geometry motion vector injection from matched draws
// 3. Reactive mask computation:
//    - Depth discontinuities / disocclusion
//    - Extreme motion vector magnitude / out-of-bounds rejection
//    - Alpha/transparency/particle tagging
struct MotionVectorProducerDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    float maxValidVelocityPixels = 128.0f; // Displacements exceeding this trigger reactive history rejection
    float disocclusionDepthThreshold = 0.05f; // Relative depth mismatch threshold
};

class MotionVectorProducer {
public:
    explicit MotionVectorProducer(const MotionVectorProducerDesc& desc = {})
        : desc_(desc) {}

    void SetDesc(const MotionVectorProducerDesc& desc) {
        desc_ = desc;
    }

    const MotionVectorProducerDesc& Desc() const {
        return desc_;
    }

    struct ProducerStats {
        uint32_t totalPixels = 0;
        uint32_t cameraReprojectedPixels = 0;
        uint32_t rejectedPixels = 0;
        uint32_t reactivePixels = 0;
        float maxDisplacementObserved = 0.0f;
    };

    // CPU-side reference & validation evaluator:
    // Generates a grid of MotionVectorPixel from current depth buffer, current camera, previous camera, and draw tracker.
    // Follows the identical mathematical contract as the GPU temporal resolve pass.
    bool EvaluateGrid(
        const float* currentDepthBuffer, // row-major, size = width * height, [0..1]
        const Camera* currentCamera,
        const Camera* previousCamera,
        const DrawTemporalTracker* drawTracker,
        std::vector<MotionVectorPixel>& outGrid,
        ProducerStats* outStats = nullptr) const
    {
        if (!currentDepthBuffer || !currentCamera || desc_.width == 0 || desc_.height == 0) {
            return false;
        }

        const size_t count = static_cast<size_t>(desc_.width) * desc_.height;
        outGrid.resize(count);

        ProducerStats stats{};
        stats.totalPixels = static_cast<uint32_t>(count);

        const Viewport& curRaster = currentCamera->Raster();

        for (uint32_t y = 0; y < desc_.height; ++y) {
            for (uint32_t x = 0; x < desc_.width; ++x) {
                const size_t idx = y * desc_.width + x;
                const float depth = currentDepthBuffer[idx];
                MotionVectorPixel& pixel = outGrid[idx];
                pixel.depth = depth;
                pixel.reactiveMask = 0.0f;
                pixel.vx = 0.0f;
                pixel.vy = 0.0f;

                // Clear/invalid depth (1.0 or nonfinite or <= 0.0) -> reactive/skybox
                if (!std::isfinite(depth) || depth <= 0.0f || depth >= 1.0f) {
                    pixel.reactiveMask = 1.0f;
                    stats.rejectedPixels++;
                    stats.reactivePixels++;
                    continue;
                }

                if (!previousCamera) {
                    // No history available: flag pixel as reactive (reject history)
                    pixel.reactiveMask = 1.0f;
                    stats.reactivePixels++;
                    continue;
                }

                // Sample coordinate at pixel center
                const Sample currentSample{
                    curRaster.x + x + 0.5,
                    curRaster.y + y + 0.5,
                    static_cast<double>(depth)
                };

                const auto reprojected = Reproject(currentSample, *currentCamera, *previousCamera);
                if (!reprojected) {
                    // Reprojection failure (out of bounds, behind camera, invalid depth)
                    pixel.reactiveMask = 1.0f;
                    stats.rejectedPixels++;
                    stats.reactivePixels++;
                    continue;
                }

                // Compute backward pixel displacement: previous - current
                const float vx = static_cast<float>(reprojected.previous.x - currentSample.x);
                const float vy = static_cast<float>(reprojected.previous.y - currentSample.y);
                const float speed = std::sqrt(vx * vx + vy * vy);

                if (speed > stats.maxDisplacementObserved) {
                    stats.maxDisplacementObserved = speed;
                }

                // Check maximum displacement threshold
                if (speed > desc_.maxValidVelocityPixels) {
                    pixel.reactiveMask = 1.0f;
                    stats.reactivePixels++;
                }

                pixel.vx = vx;
                pixel.vy = vy;
                stats.cameraReprojectedPixels++;
            }
        }

        if (outStats) {
            *outStats = stats;
        }

        return true;
    }

private:
    MotionVectorProducerDesc desc_{};
};

} // namespace gpu::temporal
