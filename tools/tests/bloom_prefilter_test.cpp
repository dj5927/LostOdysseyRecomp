#include <gpu/bloom_prefilter.h>
#include <gpu/shader/dxc_compiler.h>
#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace plume { std::unique_ptr<RenderInterface> CreateD3D12Interface(); std::unique_ptr<RenderInterface> CreateVulkanInterface(); }
namespace {
using Color = std::array<float, 4>;
constexpr unsigned W = 1280, H = 720;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); std::printf("PASS: %s\n", message); }
uint16_t Half(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint16_t sign = uint16_t((bits >> 16) & 0x8000);
    const unsigned exponent = (bits >> 23) & 255;
    const uint32_t fraction = bits & 0x7fffff;
    if (exponent == 255) return uint16_t(sign | 0x7c00 | (fraction ? 0x200 : 0));
    if (exponent > 142) return uint16_t(sign | 0x7c00);
    if (exponent < 102) return sign;
    if (exponent < 113) {
        const uint32_t significand = fraction | 0x800000;
        const unsigned shift = 126 - exponent;
        uint32_t rounded = significand >> shift;
        const uint32_t remainder = significand & ((1u << shift) - 1);
        const uint32_t halfway = 1u << (shift - 1);
        rounded += remainder > halfway || (remainder == halfway && (rounded & 1));
        return uint16_t(sign | rounded);
    }
    uint32_t rounded = ((exponent - 112) << 10) | (fraction >> 13);
    const uint32_t remainder = fraction & 8191;
    rounded += remainder > 4096 || (remainder == 4096 && (rounded & 1));
    return uint16_t(sign | rounded);
}
float Float(uint16_t bits) {
    const float sign = bits & 0x8000 ? -1.f : 1.f;
    const unsigned exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    if (exponent == 31) return std::bit_cast<float>((uint32_t(bits & 0x8000) << 16) | 0x7f800000u | (mantissa << 13));
    return sign * (exponent ? std::ldexp(1.f + mantissa / 1024.f, int(exponent) - 15) : std::ldexp(float(mantissa), -24));
}
bool Close(const Color& actual, const Color& expected) {
    for (unsigned c = 0; c < 4; ++c) if (!std::isfinite(actual[c]) || std::abs(actual[c] - expected[c]) > .002f * std::max(1.f, std::abs(expected[c]))) return false;
    return true;
}
}
int main(int argc, char** argv) {
    try {
        using namespace plume;
        const bool vulkan = argc > 1 && std::string(argv[1]) == "--vulkan";
        const int option = vulkan ? 2 : 1;
        const bool linearOnly = argc > option && std::string(argv[option]) == "--linear-only";
        auto api = vulkan ? CreateVulkanInterface() : CreateD3D12Interface(); Require(bool(api), "render interface");
        auto device = api->createDevice(); Require(bool(device), "render device");
        std::printf("Backend: %s on %s\n", vulkan ? "Vulkan" : "D3D12", device->getDescription().name.c_str());
        const auto binaryFormat = vulkan ? xenos::ShaderBinaryFormat::Spirv : xenos::ShaderBinaryFormat::Dxil;
        const auto shaderFormat = vulkan ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL;
        const char* vertex = "float4 main(uint id:SV_VertexID):SV_Position { float2 uv=float2((id<<1)&2,id&2); return float4(uv*float2(2,-2)+float2(-1,1),0,1); }";
        auto vb = xenos::CompileCachedHlsl(vertex, "main", "vs_6_0", binaryFormat);
        // Each output column is a 1/64-texel UV step across the edge of the
        // nine-tap footprint. Rows exercise horizontal, vertical and diagonal motion.
        const char* linearShader = R"(
Texture2D<float4> source : register(t0, space1);
SamplerState linearSampler : register(s0, space2);
float4 main(float4 p : SV_Position) : SV_Target {
    float phase = min(floor(p.x), 64.0) / 64.0;
    uint row = (uint)p.y % 3;
    float2 shift = row == 0 ? float2(1 + phase, 0) :
                   row == 1 ? float2(0, 1 + phase) : float2(1 + phase, 1 + phase);
    float2 uv = (float2(640.5, 360.5) + shift) / float2(1280, 720);
    float4 sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            sum += source.SampleLevel(linearSampler, uv + float2(x, y) / float2(1280, 720), 0);
    return sum / 9;
})";
        auto pb = xenos::CompileCachedHlsl(linearOnly ? linearShader : gpu::bloom_prefilter::PixelShader, "main", "ps_6_0", binaryFormat);
        if (!vb.ok || !pb.ok) throw std::runtime_error(vb.errors + pb.errors);
        auto vs = device->createShader(vb.bytecode.data(), vb.bytecode.size(), "main", shaderFormat);
        auto ps = device->createShader(pb.bytecode.data(), pb.bytecode.size(), "main", shaderFormat);
        RenderDescriptorSetBuilder empty, textures; empty.begin(); empty.end(); textures.begin(); textures.addTexture(0); textures.end();
        RenderSamplerDesc samplerDesc;
        samplerDesc.minFilter = samplerDesc.magFilter = RenderFilter::LINEAR;
        samplerDesc.mipmapMode = RenderMipmapMode::NEAREST;
        samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RenderTextureAddressMode::CLAMP;
        auto sampler = linearOnly ? device->createSampler(samplerDesc) : nullptr;
        RenderDescriptorSetBuilder samplers;
        if (linearOnly) { samplers.begin(); samplers.addSampler(0); samplers.end(); }
        RenderPipelineLayoutBuilder lb; lb.begin(false, false); lb.addDescriptorSet(empty); lb.addDescriptorSet(textures);
        if (linearOnly) lb.addDescriptorSet(samplers);
        lb.end();
        auto layout = lb.create(device.get());
        auto samplerSet = linearOnly ? samplers.create(device.get()) : nullptr;
        if (linearOnly) samplerSet->setSampler(0, sampler.get());
        constexpr auto format = RenderFormat::R16G16B16A16_FLOAT;
        RenderGraphicsPipelineDesc desc; desc.pipelineLayout = layout.get(); desc.vertexShader = vs.get(); desc.pixelShader = ps.get();
        desc.renderTargetCount = 1; desc.renderTargetFormat[0] = format; desc.renderTargetBlend[0] = RenderBlendDesc::Copy(); desc.cullMode = RenderCullMode::NONE;
        auto pipeline = device->createGraphicsPipeline(desc); Require(bool(pipeline), linearOnly ? "linear nine-tap HDR16 pipeline" : "production prefilter HLSL HDR16 pipeline");
        auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
        auto commands = queue->createCommandList(); auto fence = device->createCommandFence();
        auto submit = [&] { commands->end(); const RenderCommandList* lists[] = {commands.get()}; queue->executeCommandLists(lists, 1, nullptr, 0, nullptr, 0, fence.get()); queue->waitForCommandFence(fence.get()); };
        auto run = [&](unsigned width, unsigned height, const std::vector<Color>& input) {
            auto source = device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, format));
            auto output = device->createTexture(RenderTextureDesc::Texture2D(W, H, 1, format, RenderTextureFlag::RENDER_TARGET));
            const unsigned pitch = (width * 8 + 255) & ~255u, outPitch = W * 8;
            auto upload = device->createBuffer(RenderBufferDesc::UploadBuffer(size_t(pitch) * height));
            auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(size_t(outPitch) * H));
            auto* bytes = static_cast<uint8_t*>(upload->map());
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) for (unsigned c = 0; c < 4; ++c) {
                const uint16_t value = Half(input[size_t(y) * width + x][c]); std::memcpy(bytes + size_t(y) * pitch + x * 8 + c * 2, &value, 2);
            }
            upload->unmap();
            auto set = textures.create(device.get()); set->setTexture(0, source.get(), RenderTextureLayout::SHADER_READ);
            const RenderTexture* attachments[] = {output.get()}; auto framebuffer = device->createFramebuffer(RenderFramebufferDesc(attachments, 1));
            commands->begin();
            commands->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(source.get(), RenderTextureLayout::COPY_DEST));
            commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(source.get()), RenderTextureCopyLocation::PlacedFootprint(upload.get(), format, width, height, 1, pitch / 8));
            commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(source.get(), RenderTextureLayout::SHADER_READ));
            commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(output.get(), RenderTextureLayout::COLOR_WRITE));
            commands->setFramebuffer(framebuffer.get()); RenderViewport viewport(0, 0, float(W), float(H)); RenderRect scissor(0, 0, W, H);
            commands->setViewports(&viewport, 1); commands->setScissors(&scissor, 1); commands->setGraphicsPipelineLayout(layout.get()); commands->setPipeline(pipeline.get());
            commands->setGraphicsDescriptorSet(set.get(), 1);
            if (linearOnly) commands->setGraphicsDescriptorSet(samplerSet.get(), 2);
            commands->drawInstanced(3, 1, 0, 0);
            commands->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(output.get(), RenderTextureLayout::COPY_SOURCE));
            commands->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(readback.get(), format, W, H, 1, outPitch / 8), RenderTextureCopyLocation::Subresource(output.get()));
            submit(); // All descriptors, attachments and transfer buffers stay alive through completion.
            std::vector<Color> result(W * H); const auto* values = static_cast<const uint16_t*>(readback->map());
            for (size_t i = 0; i < result.size(); ++i) for (unsigned c = 0; c < 4; ++c) result[i][c] = Float(values[i * 4 + c]);
            readback->unmap(); return result;
        };
        if (linearOnly) {
            if (argc != option + 1) throw std::runtime_error("Usage: LoBloomPrefilterTest [--vulkan] --linear-only");
            std::vector<Color> input(W * H, Color{0.f, 0.f, 0.f, .375f});
            input[360 * W + 640] = Color{18.f, -9.f, 4.5f, .375f};
            const auto result = run(W, H, input);
            bool analytic = true, continuous = true, intermediate = true;
            float maxStep = 0.f;
            for (unsigned row = 0; row < 3; ++row) for (unsigned phase = 0; phase <= 64; ++phase) {
                float weight = 1.f - float(phase) / 64.f;
                if (row == 2) weight *= weight;
                const auto& value = result[row * W + phase];
                const Color expected{2.f * weight, -weight, .5f * weight, .375f};
                // Texture units quantize bilinear weights; unlike the area
                // shader's float arithmetic, allow one 1/256 weight step per
                // axis plus HDR16 rounding. This remains below one UV step.
                bool matches = true;
                for (unsigned c = 0; c < 4; ++c) {
                    const float bound = c == 3 ? .002f : std::abs(Color{2.f, -1.f, .5f, 0.f}[c]) * (2.f / 256.f + .001f);
                    matches &= std::isfinite(value[c]) && std::abs(value[c] - expected[c]) <= bound;
                }
                if (!matches && analytic) std::fprintf(stderr, "First mismatch row=%u phase=%u actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n", row, phase, value[0], value[1], value[2], value[3], expected[0], expected[1], expected[2], expected[3]);
                analytic &= matches;
                if (phase > 0) {
                    const float step = std::abs(value[0] - result[row * W + phase - 1][0]);
                    maxStep = std::max(maxStep, step);
                    // Analytic diagonal maximum 4/64 plus the two samples'
                    // independent bilinear-weight and HDR16 error bounds.
                    continuous &= step <= 4.f / 64.f + 4.f * (2.f / 256.f + .001f);
                }
                if (phase > 0 && phase < 64) {
                    // A diagonal tail can round to zero below the texture
                    // unit's weight precision. Horizontal/vertical phases and
                    // every diagonal sample above that floor must contribute.
                    if (row != 2 || expected[0] > 2.f * (2.f / 256.f + .001f)) intermediate &= value[0] > 0.f;
                    intermediate &= value[0] < 2.f;
                }
            }
            Require(analytic, "nine linear taps match independent bilinear impulse response, retaining HDR, negative RGB and alpha");
            std::printf("Maximum adjacent red step: %g (full impulse contribution: 2); intermediate phases above quantization floor: %s\n", maxStep, intermediate ? "all nonzero/nonfull" : "FAIL");
            Require(continuous && intermediate, "65 horizontal, vertical and diagonal UV phases vary continuously without nearest-sample zero/full jumps");
            return 0;
        }
        if (argc > option && std::string(argv[option]) == "--filter") {
            if (argc != option + 5) throw std::runtime_error("Usage: LoBloomPrefilterTest [--vulkan] --filter INPUT.bin WIDTH HEIGHT OUTPUT.f32");
            const unsigned width = unsigned(std::stoul(argv[option + 2])), height = unsigned(std::stoul(argv[option + 3]));
            if (!width || !height || width > 16384 || height > 16384) throw std::runtime_error("Invalid input extent");
            const size_t count = size_t(width) * height;
            std::ifstream file(argv[option + 1], std::ios::binary | std::ios::ate);
            if (!file || file.tellg() != std::streamoff(count * 8)) throw std::runtime_error("Input must contain exactly WIDTH*HEIGHT packed little-endian HDR16 RGBA pixels");
            file.seekg(0); std::vector<uint16_t> packed(count * 4);
            if (!file.read(reinterpret_cast<char*>(packed.data()), std::streamsize(count * 8))) throw std::runtime_error("Input read failed");
            std::vector<Color> input(count);
            for (size_t i = 0; i < count; ++i) for (unsigned c = 0; c < 4; ++c) input[i][c] = Float(packed[i * 4 + c]);
            const auto result = run(width, height, input);
            static_assert(sizeof(Color) == 4 * sizeof(float));
            std::ofstream output(argv[option + 4], std::ios::binary);
            if (!output.write(reinterpret_cast<const char*>(result.data()), std::streamsize(result.size() * sizeof(Color)))) throw std::runtime_error("Output write failed");
            output.close(); if (!output) throw std::runtime_error("Output close failed");
            std::printf("Wrote %ux%u packed little-endian float32 RGBA (GPU HDR16 result): %s\n", W, H, argv[option + 4]);
            return 0;
        }
        for (unsigned width : {1920u, 3840u}) {
            const unsigned height = width * 9 / 16;
            const Color constant{8.f, -.5f, 2.f, .375f};
            std::vector<Color> input(size_t(width) * height, constant);
            auto result = run(width, height, input);
            Require(std::all_of(result.begin(), result.end(), [&](const Color& c) {return Close(c, constant);}), "HDR values above one, negative RGB and alpha preserved over all pixels including corners");
            // Independent analytic integral of a repeating piecewise-constant signal.
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) input[size_t(y) * width + x] = Color{float(x % 7), float(y % 5) - 2.f, 4.f, float((x + y) % 4) / 4.f};
            result = run(width, height, input);
            bool correct = true;
            const double scale = double(width) / W;
            for (unsigned oy : {0u, 1u, 101u, H - 1}) for (unsigned ox : {0u, 1u, 103u, W - 1}) {
                Color expected{}; const double left = ox * scale, top = oy * scale;
                for (unsigned y = unsigned(top); y < unsigned(std::ceil(top + scale)); ++y) for (unsigned x = unsigned(left); x < unsigned(std::ceil(left + scale)); ++x) {
                    const double area = (std::min(left + scale, double(x + 1)) - std::max(left, double(x))) * (std::min(top + scale, double(y + 1)) - std::max(top, double(y))) / (scale * scale);
                    for (unsigned c = 0; c < 4; ++c) expected[c] += float(input[size_t(y) * width + x][c] * area);
                }
                correct &= Close(result[oy * W + ox], expected);
            }
            Require(correct, width == 1920 ? "1.5x exact fractional area integral including four corners" : "3x exact integer area integral including four corners");
            if (width == 3840) {
                std::fill(input.begin(), input.end(), Color{});
                for (unsigned phase = 0; phase < 9; ++phase) {
                    const unsigned ox = 100 + phase * 10, oy = 100;
                    input[size_t(oy * 3 + phase / 3) * width + ox * 3 + phase % 3] = Color{9.f, 0.f, 0.f, 0.f};
                }
                result = run(width, height, input);
                bool covered = true; unsigned pointMisses = 0;
                for (unsigned phase = 0; phase < 9; ++phase) {
                    const unsigned ox = 100 + phase * 10, oy = 100;
                    covered &= Close(result[oy * W + ox], Color{1.f, 0.f, 0.f, 0.f});
                    // The original nine taps sample a 3x3 guest-pixel neighborhood;
                    // at 3x resolution each guest tap sees only its center subpixel.
                    float oldNineTap = 0;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                        oldNineTap += input[size_t(int(oy * 3 + 1) + dy * 3) * width + int(ox * 3 + 1) + dx * 3][0] / 9.f;
                    pointMisses += oldNineTap == 0;
                }
                Require(covered && pointMisses == 8, "all nine bright-point phases contribute equally; nine point taps miss eight phases");
            }
        }
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
