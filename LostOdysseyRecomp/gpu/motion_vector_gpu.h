#pragma once

#include "temporal_math.h"
#include <array>
#include <vector>
#include <memory>
#include <string>
#include <cmath>

#ifdef LO_GPU_PLUME
#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>
#include "shader/dxc_compiler.h"

namespace gpu::temporal {

// MotionVectorGPU compiles and executes a GPU fullscreen pass to evaluate backward
// pixel displacement motion vectors (previous - current, in pixels) and stores them
// into an R16G16_FLOAT texture for consumption by TemporalAA.
class MotionVectorGPU {
    using Device = plume::RenderDevice;
    using CommandList = plume::RenderCommandList;

    std::unique_ptr<plume::RenderPipelineLayout> layout_;
    std::unique_ptr<plume::RenderShader> vs_, ps_;
    std::unique_ptr<plume::RenderPipeline> pipeline_;
    std::unique_ptr<plume::RenderSampler> sampler_;

    struct PendingResources {
        uint64_t serial = 0;
        std::unique_ptr<plume::RenderBuffer> constants;
        std::unique_ptr<plume::RenderDescriptorSet> set;
        std::unique_ptr<plume::RenderFramebuffer> framebuffer;
    };
    std::vector<PendingResources> pending_;
    uint64_t recordedSerial_ = 0;
    Device* device_ = nullptr;
    bool vulkan_ = false;
    bool ready_ = false;

    struct PassConstants {
        float transform[16];
        float previousScaleBias[4];
        float resolution[4]; // width, height, prevWidth, prevHeight
        float jitter[4];     // curJx, curJy, prevJx, prevJy
        float policy[4];     // depthTolerance, maxVelocity, unused, unused
    };

    static constexpr const char* kShaderSource = R"(
Texture2D<float> currentDepth : register(t0);
Texture2D<float> previousDepth : register(t1);

#ifdef __spirv__
[[vk::binding(2,0)]]
#endif
SamplerState pointClamp : register(s0);

#ifdef __spirv__
[[vk::binding(3,0)]]
#endif
cbuffer Parameters : register(b0) {
    row_major float4x4 transform;
    float4 previousScaleBias;
    float4 resolution;
    float4 jitter;
    float4 policy; // depthTolerance, maxVelocity, unused, unused
};

float4 vertex(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float2 pixel(float4 p : SV_Position) : SV_Target {
    int3 pixelPos = int3(p.xy, 0);
    float curDepth = currentDepth.Load(pixelPos);

    // Reverse-Z convention: 0.0 is background/far/clear
    if (curDepth <= 0.0 || curDepth >= 1.0) {
        return float2(0.0, 0.0);
    }

    // Reproject current pixel and depth using precomputed raster-to-previous-clip transform
    float4 prevClip = mul(float4(p.xy, curDepth, 1.0), transform);
    if (prevClip.w <= 0.0) {
        return float2(0.0, 0.0);
    }

    float invW = 1.0 / prevClip.w;
    float2 prevRaster = prevClip.xy * invW * previousScaleBias.xy + previousScaleBias.zw;
    float prevPredictedDepth = prevClip.z * invW;

    // Backward motion vector in pixel units: previous - current
    float2 mv = prevRaster - p.xy;

    // Reject extreme velocities
    if (length(mv) > policy.y) {
        return float2(0.0, 0.0);
    }

    // Depth continuity check against previous depth buffer
    int2 prevPixelInt = int2(round(prevRaster));
    if (prevPixelInt.x >= 0 && prevPixelInt.x < (int)resolution.z &&
        prevPixelInt.y >= 0 && prevPixelInt.y < (int)resolution.w) {
        float prevSampledDepth = previousDepth.Load(int3(prevPixelInt, 0));
        if (prevSampledDepth > 0.0 && prevSampledDepth < 1.0) {
            if (abs(prevSampledDepth - prevPredictedDepth) > policy.x) {
                // Occlusion or disocclusion detected
                return float2(0.0, 0.0);
            }
        }
    }

    return mv;
}
)";

    static temporal::Matrix Multiply(const temporal::Matrix& a, const temporal::Matrix& b) {
        temporal::Matrix out{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                for (int k = 0; k < 4; ++k) {
                    out[4 * i + j] += a[4 * i + k] * b[4 * k + j];
                }
            }
        }
        return out;
    }

public:
    bool Init(Device* device) {
        if (!device) return false;
        device_ = device;
        vulkan_ = (device->getCapabilities().shaderFormat == plume::RenderShaderFormat::SPIRV);

        auto v = xenos::CompileCachedHlsl(kShaderSource, "vertex", "vs_6_0", vulkan_ ? xenos::ShaderBinaryFormat::Spirv : xenos::ShaderBinaryFormat::Dxil);
        auto p = xenos::CompileCachedHlsl(kShaderSource, "pixel", "ps_6_0", vulkan_ ? xenos::ShaderBinaryFormat::Spirv : xenos::ShaderBinaryFormat::Dxil);
        if (!v.ok || !p.ok) return false;

        const auto format = vulkan_ ? plume::RenderShaderFormat::SPIRV : plume::RenderShaderFormat::DXIL;
        vs_ = device_->createShader(v.bytecode.data(), v.bytecode.size(), "vertex", format);
        ps_ = device_->createShader(p.bytecode.data(), p.bytecode.size(), "pixel", format);

        plume::RenderDescriptorSetBuilder sb;
        sb.begin();
        sb.addTexture(0);
        sb.addTexture(1);
        sb.addSampler(vulkan_ ? 2 : 0);
        if (vulkan_) sb.addConstantBuffer(3);
        sb.end();

        plume::RenderPipelineLayoutBuilder lb;
        lb.begin(false, false);
        if (!vulkan_) lb.addPushConstant(0, 0, sizeof(PassConstants), plume::RenderShaderStageFlag::PIXEL);
        lb.addDescriptorSet(sb);
        lb.end();
        layout_ = lb.create(device_);

        if (!vs_ || !ps_ || !layout_) return false;

        plume::RenderGraphicsPipelineDesc desc{};
        desc.pipelineLayout = layout_.get();
        desc.vertexShader = vs_.get();
        desc.pixelShader = ps_.get();
        desc.renderTargetCount = 1;
        desc.renderTargetFormat[0] = plume::RenderFormat::R16G16_FLOAT;
        desc.renderTargetBlend[0] = plume::RenderBlendDesc::Copy();
        desc.cullMode = plume::RenderCullMode::NONE;

        pipeline_ = device_->createGraphicsPipeline(desc);
        plume::RenderSamplerDesc sdesc{};
        sdesc.minFilter = plume::RenderFilter::NEAREST;
        sdesc.magFilter = plume::RenderFilter::NEAREST;
        sdesc.addressU = plume::RenderTextureAddressMode::CLAMP;
        sdesc.addressV = plume::RenderTextureAddressMode::CLAMP;
        sdesc.addressW = plume::RenderTextureAddressMode::CLAMP;
        sampler_ = device_->createSampler(sdesc);

        ready_ = (pipeline_ && sampler_);
        return ready_;
    }

    bool Ready() const { return ready_; }

    bool Render(
        CommandList* commands,
        plume::RenderTexture* currentDepth,
        plume::RenderTexture* previousDepth,
        plume::RenderTexture* outputMotionVector,
        uint32_t width,
        uint32_t height,
        uint32_t prevWidth,
        uint32_t prevHeight,
        const Camera& currentCamera,
        const Camera& previousCamera,
        float curJx, float curJy,
        float prevJx, float prevJy
    ) {
        if (!ready_ || !commands || !currentDepth || !previousDepth || !outputMotionVector) {
            return false;
        }

        PassConstants c{};
        const auto& curRaster = currentCamera.Raster();
        const auto& prevRaster = previousCamera.Raster();

        temporal::Matrix rasterToNdc{};
        rasterToNdc[0] = 2.0f / curRaster.width;
        rasterToNdc[5] = -2.0f / (curRaster.height * curRaster.ndcYSign);
        rasterToNdc[10] = 1.0f;
        rasterToNdc[15] = 1.0f;
        rasterToNdc[12] = -1.0f - curRaster.halfPixelNdcX - 2.0f * curJx / curRaster.width;
        rasterToNdc[13] = (1.0f - curRaster.halfPixelNdcY + 2.0f * curJy / curRaster.height) / curRaster.ndcYSign;

        const auto combined = Multiply(rasterToNdc, Multiply(currentCamera.InverseVP(), previousCamera.VP()));
        for (int i = 0; i < 16; ++i) {
            c.transform[i] = static_cast<float>(combined[i]);
            if (!std::isfinite(c.transform[i])) return false;
        }

        c.previousScaleBias[0] = static_cast<float>(prevRaster.width * 0.5f);
        c.previousScaleBias[1] = static_cast<float>(-prevRaster.height * 0.5f * prevRaster.ndcYSign);
        c.previousScaleBias[2] = static_cast<float>(prevRaster.width * 0.5f * (1.0f + prevRaster.halfPixelNdcX) + prevJx);
        c.previousScaleBias[3] = static_cast<float>(prevRaster.height * 0.5f * (1.0f - prevRaster.halfPixelNdcY) + prevJy);

        c.resolution[0] = static_cast<float>(width);
        c.resolution[1] = static_cast<float>(height);
        c.resolution[2] = static_cast<float>(prevWidth);
        c.resolution[3] = static_cast<float>(prevHeight);

        c.jitter[0] = curJx;
        c.jitter[1] = curJy;
        c.jitter[2] = prevJx;
        c.jitter[3] = prevJy;

        c.policy[0] = 0.05f;   // depth continuity tolerance
        c.policy[1] = 128.0f;  // max pixel velocity
        c.policy[2] = 0.0f;
        c.policy[3] = 0.0f;

        plume::RenderDescriptorSetBuilder sb;
        sb.begin();
        sb.addTexture(0);
        sb.addTexture(1);
        sb.addSampler(vulkan_ ? 2 : 0);
        if (vulkan_) sb.addConstantBuffer(3);
        sb.end();

        PendingResources pending;
        pending.set = sb.create(device_);
        const plume::RenderTexture* attachments[] = { outputMotionVector };
        pending.framebuffer = device_->createFramebuffer(plume::RenderFramebufferDesc(attachments, 1));
        if (!pending.set || !pending.framebuffer) return false;

        pending.set->setTexture(0, currentDepth, plume::RenderTextureLayout::SHADER_READ);
        pending.set->setTexture(1, previousDepth, plume::RenderTextureLayout::SHADER_READ);
        pending.set->setSampler(vulkan_ ? 2 : 0, sampler_.get());

        if (vulkan_) {
            pending.constants = device_->createBuffer(plume::RenderBufferDesc::UploadBuffer(sizeof(PassConstants), plume::RenderBufferFlag::CONSTANT));
            if (!pending.constants) return false;
            auto* mapped = pending.constants->map();
            memcpy(mapped, &c, sizeof(c));
            pending.constants->unmap();
            pending.set->setBuffer(3, pending.constants.get(), sizeof(c));
        }

        pending.serial = recordedSerial_ + 1;
        pending_.push_back(std::move(pending));
        ++recordedSerial_;
        auto& res = pending_.back();

        commands->setFramebuffer(res.framebuffer.get());
        plume::RenderViewport vp(0, 0, static_cast<float>(width), static_cast<float>(height));
        plume::RenderRect scissor(0, 0, width, height);
        commands->setViewports(&vp, 1);
        commands->setScissors(&scissor, 1);

        commands->setGraphicsPipelineLayout(layout_.get());
        commands->setPipeline(pipeline_.get());

        if (!vulkan_) {
            commands->setGraphicsPushConstants(0, &c);
        }
        commands->setGraphicsDescriptorSet(res.set.get(), 0);
        commands->drawInstanced(3, 1, 0, 0);

        return true;
    }

    void ReleaseCompletedThrough(uint64_t serial) {
        std::erase_if(pending_, [serial](const PendingResources& p) { return p.serial <= serial; });
    }

    void ReleaseCompleted() {
        pending_.clear();
    }
};

} // namespace gpu::temporal
#else
namespace gpu::temporal {
class MotionVectorGPU {
public:
    bool Init(void*) { return false; }
    bool Ready() const { return false; }
    void ReleaseCompleted() {}
    void ReleaseCompletedThrough(uint64_t) {}
};
} // namespace gpu::temporal
#endif
