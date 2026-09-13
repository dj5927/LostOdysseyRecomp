// Focused Vulkan descriptor/cache and draw-constant check; no WSI or game assets.
#include <gpu/shader/dxc_compiler.h>
#include <gpu/texture_descriptor_cache.h>
#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace plume { std::unique_ptr<RenderInterface> CreateVulkanInterface(); }
namespace {
using namespace plume;
using Cache = gpu::texture_descriptors::BatchCache<RenderTexture, RenderDescriptorSet, 32>;
using Color = std::array<float, 4>;
constexpr uint32_t kDraws = 8;
constexpr RenderFormat kFormat = RenderFormat::R32G32B32A32_FLOAT;

void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct Slot
{
    std::unique_ptr<RenderCommandList> commands;
    std::unique_ptr<RenderCommandFence> fence;
    std::unique_ptr<RenderBuffer> constants, readback;
    std::unique_ptr<RenderTexture> target;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    std::array<Cache, 3> caches;
    std::array<std::vector<std::unique_ptr<RenderDescriptorSet>>, 3> pools;
    std::array<size_t, 3> used{};
    std::array<Color, kDraws> expected{};
    bool submitted = false;
};

void Run(bool bindingCacheOnly)
{
    auto api = CreateVulkanInterface();
    Check(api != nullptr, "Vulkan interface");
    auto device = api->createDevice();
    Check(device != nullptr, "Vulkan device");
    auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
    Check(queue != nullptr, "Vulkan queue");

    const std::string push = R"HLSL(
struct C { uint64_t VertexShaderConstants; uint64_t SharedConstants; uint64_t PixelShaderConstants; };
[[vk::push_constant]] ConstantBuffer<C> constants;
)HLSL";
    const auto vc = xenos::CompileHlsl(push + R"HLSL(
ByteAddressBuffer arena : register(t0, space0);
struct V { float4 pos : SV_Position; nointerpolation float value : TEXCOORD0; };
V main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    V v; v.pos = float4(uv * float2(2, -2) + float2(-1, 1), .5, 1);
    v.value = vk::RawBufferLoad<float4>(constants.VertexShaderConstants).x + asfloat(arena.Load(0));
    return v;
})HLSL", "main", "vs_6_0", xenos::ShaderBinaryFormat::Spirv);
    const auto pc = xenos::CompileHlsl(push + R"HLSL(
Texture2D<float4> t0 : register(t0, space1);
Texture2D<float4> t31 : register(t31, space1);
Texture3D<float4> v0 : register(t0, space2);
Texture3D<float4> v31 : register(t31, space2);
TextureCube<float4> c0 : register(t0, space3);
TextureCube<float4> c31 : register(t31, space3);
SamplerState fixedSampler : register(s0, space4);
float4 main(nointerpolation float value : TEXCOORD0) : SV_Target {
    float4 result = t0.Load(int3(0, 0, 0)) + 2 * t31.Load(int3(0, 0, 0));
    result += 4 * v0.Load(int4(0, 0, 0, 0)) + 8 * v31.Load(int4(0, 0, 0, 0));
    result += 16 * c0.SampleLevel(fixedSampler, float3(1, 0, 0), 0);
    result += 32 * c31.SampleLevel(fixedSampler, float3(1, 0, 0), 0);
    return result + float4(value, vk::RawBufferLoad<float4>(constants.SharedConstants).y,
        vk::RawBufferLoad<float4>(constants.PixelShaderConstants).z, 1);
})HLSL", "main", "ps_6_0", xenos::ShaderBinaryFormat::Spirv);
    if (!vc.ok || !pc.ok) std::fprintf(stderr, "VS: %s\nPS: %s\n", vc.errors.c_str(), pc.errors.c_str());
    Check(vc.ok && pc.ok, "fixture shaders");
    auto vs = device->createShader(vc.bytecode.data(), vc.bytecode.size(), "main", RenderShaderFormat::SPIRV);
    auto ps = device->createShader(pc.bytecode.data(), pc.bytecode.size(), "main", RenderShaderFormat::SPIRV);
    Check(vs && ps, "fixture shader modules");

    std::array<RenderDescriptorSetBuilder, 5> builders;
    for (unsigned bank = 0; bank < builders.size(); ++bank) {
        builders[bank].begin();
        if (bank == 0) builders[bank].addByteAddressBuffer(0);
        else if (bank == 4) builders[bank].addSampler(0);
        else for (unsigned slot = 0; slot < 32; ++slot) builders[bank].addTexture(slot);
        builders[bank].end();
    }
    RenderPipelineLayoutBuilder layoutBuilder;
    layoutBuilder.begin(false, false);
    layoutBuilder.addPushConstant(0, 0, 24, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    for (auto& builder : builders) layoutBuilder.addDescriptorSet(builder);
    layoutBuilder.end();
    auto layout = layoutBuilder.create(device.get());
    Check(layout != nullptr, "fixture pipeline layout");
    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = layout.get(); desc.vertexShader = vs.get(); desc.pixelShader = ps.get();
    desc.renderTargetCount = 1; desc.renderTargetFormat[0] = kFormat;
    desc.renderTargetBlend[0] = RenderBlendDesc::Copy(); desc.cullMode = RenderCullMode::NONE;
    auto pipeline = device->createGraphicsPipeline(desc);
    Check(pipeline != nullptr, "fixture pipeline");

    // B has an incompatible set-zero prefix while retaining identical higher
    // set layouts. Returning B -> A must restore every higher binding even
    // when its descriptor handle is unchanged in the application's cache.
    RenderDescriptorSetBuilder alternateSetBuilder;
    std::unique_ptr<RenderPipelineLayout> alternateLayout;
    std::unique_ptr<RenderPipeline> alternatePipeline;
    if (bindingCacheOnly) {
        alternateSetBuilder.begin();
        alternateSetBuilder.addByteAddressBuffer(0);
        alternateSetBuilder.addByteAddressBuffer(1);
        alternateSetBuilder.end();
        RenderPipelineLayoutBuilder alternateBuilder;
        alternateBuilder.begin(false, false);
        alternateBuilder.addPushConstant(0, 0, 24, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
        alternateBuilder.addDescriptorSet(alternateSetBuilder);
        for (unsigned bank = 1; bank < builders.size(); ++bank)
            alternateBuilder.addDescriptorSet(builders[bank]);
        alternateBuilder.end();
        alternateLayout = alternateBuilder.create(device.get());
        Check(alternateLayout != nullptr, "incompatible-prefix layout");
        desc.pipelineLayout = alternateLayout.get();
        alternatePipeline = device->createGraphicsPipeline(desc);
        Check(alternatePipeline != nullptr, "incompatible-prefix pipeline");
    }

    auto arena = device->createBuffer(RenderBufferDesc::UploadBuffer(256, RenderBufferFlag::STORAGE));
    Check(arena != nullptr, "static vertex arena");
    auto arenaBytes = static_cast<float*>(arena->map());
    Check(arenaBytes != nullptr, "static vertex arena map");
    std::memset(arenaBytes, 0, 256); arenaBytes[0] = 11;
    arena->unmap();
    auto set0 = builders[0].create(device.get());
    auto samplerSet = builders[4].create(device.get());
    RenderSamplerDesc samplerDesc;
    samplerDesc.minFilter = samplerDesc.magFilter = RenderFilter::NEAREST;
    samplerDesc.mipmapMode = RenderMipmapMode::NEAREST;
    auto sampler = device->createSampler(samplerDesc);
    Check(set0 && samplerSet && sampler, "static descriptor sets");
    set0->setBuffer(0, arena.get(), 256);
    samplerSet->setSampler(0, sampler.get()); // Immutable for all recorded batches.
    std::unique_ptr<RenderDescriptorSet> alternateSet0;
    if (bindingCacheOnly) {
        alternateSet0 = alternateSetBuilder.create(device.get());
        Check(alternateSet0 != nullptr, "incompatible-prefix descriptor set");
        alternateSet0->setBuffer(0, arena.get(), 256);
        alternateSet0->setBuffer(1, arena.get(), 256);
    }

    const std::array<Color, 3> colors{{{0, 0, 0, 0}, {1, 2, 3, 4}, {4, 3, 2, 1}}};
    std::array<std::array<std::unique_ptr<RenderTexture>, 3>, 3> textures;
    auto upload = device->createBuffer(RenderBufferDesc::UploadBuffer(24 * 256));
    auto init = queue->createCommandList();
    auto initFence = device->createCommandFence();
    Check(upload && init && initFence, "texture initialization resources");
    auto bytes = static_cast<uint8_t*>(upload->map());
    Check(bytes != nullptr, "texture upload map");
    uint64_t offset = 0;
    init->begin();
    for (unsigned bank = 0; bank < 3; ++bank) {
        for (unsigned color = 0; color < colors.size(); ++color) {
            auto textureDesc = bank == 1 ? RenderTextureDesc::Texture3D(1, 1, 1, 1, kFormat) :
                RenderTextureDesc::Texture2D(1, 1, 1, kFormat);
            if (bank == 2) { textureDesc.arraySize = 6; textureDesc.flags = RenderTextureFlag::CUBE; }
            auto& texture = textures[bank][color];
            texture = device->createTexture(textureDesc);
            Check(texture != nullptr, "source texture");
            init->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.get(), RenderTextureLayout::COPY_DEST));
            for (unsigned face = 0; face < textureDesc.arraySize; ++face) {
                std::memcpy(bytes + offset, colors[color].data(), sizeof(Color));
                init->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.get(), 0, face),
                    RenderTextureCopyLocation::PlacedFootprint(upload.get(), kFormat, 1, 1, 1, 16, offset));
                offset += 256;
            }
            init->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.get(), RenderTextureLayout::SHADER_READ));
        }
    }
    upload->unmap(); init->end();
    const RenderCommandList* initialization[]{init.get()};
    queue->executeCommandLists(initialization, 1, nullptr, 0, nullptr, 0, initFence.get());
    queue->waitForCommandFence(initFence.get());

    std::array<std::unique_ptr<RenderDescriptorSet>, 3> dummySets;
    for (unsigned bank = 0; bank < 3; ++bank) {
        dummySets[bank] = builders[bank + 1].create(device.get());
        Check(dummySets[bank] != nullptr, "dummy descriptor set");
        for (unsigned slot = 0; slot < 32; ++slot)
            dummySets[bank]->setTexture(slot, textures[bank][0].get(), RenderTextureLayout::SHADER_READ);
    }

    std::array<Slot, 2> slots;
    for (auto& slot : slots) {
        slot.commands = queue->createCommandList(); slot.fence = device->createCommandFence();
        slot.constants = device->createBuffer(RenderBufferDesc::UploadBuffer(kDraws * 3 * 256,
            RenderBufferFlag::STORAGE | RenderBufferFlag::DEVICE_ADDRESSABLE));
        slot.readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(256));
        slot.target = device->createTexture(RenderTextureDesc::Texture2D(kDraws, 1, 1, kFormat, RenderTextureFlag::RENDER_TARGET));
        Check(slot.commands && slot.fence && slot.constants && slot.readback && slot.target, "slot resources");
        const RenderTexture* attachments[]{slot.target.get()};
        slot.framebuffer = device->createFramebuffer(RenderFramebufferDesc(attachments, 1));
        Check(slot.framebuffer != nullptr, "slot framebuffer");
    }
    size_t checkedPixels = 0, hits = 0, misses = 0, pooledRewrites = 0;
    auto complete = [&](Slot& slot) {
        if (!slot.submitted) return;
        queue->waitForCommandFence(slot.fence.get()); slot.submitted = false;
        const auto pixels = static_cast<const float*>(slot.readback->map());
        Check(pixels != nullptr, "slot readback map");
        bool equal = true;
        for (unsigned x = 0; x < kDraws; ++x) {
            for (unsigned channel = 0; channel < 4; ++channel)
                equal &= pixels[x * 4 + channel] == slot.expected[x][channel];
            ++checkedPixels;
        }
        slot.readback->unmap();
        Check(equal, "cached textures, dummy slots or combined constants changed output");
    };
    try {
        for (unsigned cycle = 0; cycle < (bindingCacheOnly ? 4u : 6u); ++cycle) {
            const unsigned slotIndex = cycle % 2;
            auto& slot = slots[slotIndex];
            complete(slot); // Only this slot's completed fence permits pool rewrites.
            for (auto& cache : slot.caches) cache.Clear();
            slot.used.fill(0);
            auto constantsBytes = static_cast<uint8_t*>(slot.constants->map());
            Check(constantsBytes != nullptr, "draw constants map");
            for (unsigned draw = 0; draw < kDraws; ++draw) {
                const std::array<Color, 3> values{{{float(cycle * 16 + draw + 1), 0, 0, 0},
                    {0, float(cycle + 1), 0, 0}, {0, 0, float(slotIndex + 1), 0}}};
                for (unsigned bank = 0; bank < 3; ++bank)
                    std::memcpy(constantsBytes + (draw * 3 + bank) * 256, values[bank].data(), sizeof(Color));
            }
            slot.constants->unmap();
            auto* commands = slot.commands.get();
            commands->begin();
            commands->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(slot.target.get(), RenderTextureLayout::COLOR_WRITE));
            commands->setFramebuffer(slot.framebuffer.get());
            commands->setGraphicsPipelineLayout(layout.get()); commands->setPipeline(pipeline.get());
            RenderViewport viewport(0, 0, kDraws, 1); commands->setViewports(&viewport, 1);
            commands->setGraphicsDescriptorSet(set0.get(), 0);
            commands->setGraphicsDescriptorSet(samplerSet.get(), 4);
            const unsigned sequence[kDraws]{0, 1, 0, 2, 1, 0, 3, 2};
            for (unsigned draw = 0; draw < kDraws; ++draw) {
                if (bindingCacheOnly) {
                    const bool alternate = draw == 2 || draw == 5;
                    auto* drawLayout = alternate ? alternateLayout.get() : layout.get();
                    auto* drawPipeline = alternate ? alternatePipeline.get() : pipeline.get();
                    commands->setGraphicsPipelineLayout(drawLayout);
                    commands->setPipeline(drawPipeline);
                    commands->setPipeline(drawPipeline);
                    commands->setGraphicsPipelineLayout(drawLayout);
                    // Bind a high set first after each layout switch. Binding
                    // the incompatible low prefix later must not leave a stale
                    // remembered high set when returning to the other layout.
                    commands->setGraphicsDescriptorSet(samplerSet.get(), 4);
                    commands->setGraphicsDescriptorSet(samplerSet.get(), 4);
                }
                // A/B/A reuses an earlier key. Later cycles refill pool entries
                // in another order, exposing stale high-slot/default contents.
                std::array<std::array<unsigned, 2>, 3> selected{};
                switch ((sequence[draw] + cycle) % 4) {
                case 0: selected[0] = {1, 0}; break;
                case 1: selected = {{{2, 1}, {1, 2}, {2, 1}}}; break;
                case 2: selected[0] = {0, 2}; selected[2] = {0, 2}; break;
                default: break;
                }
                std::array<RenderDescriptorSet*, 3> drawSets{};
                Color expected{float(cycle * 16 + draw + 12), float(cycle + 1), float(slotIndex + 1), 1};
                for (unsigned bank = 0; bank < 3; ++bank) {
                    Cache::Key key; key.fill(textures[bank][0].get());
                    key[0] = textures[bank][selected[bank][0]].get();
                    key[31] = textures[bank][selected[bank][1]].get();
                    auto* set = dummySets[bank].get();
                    if (selected[bank][0] || selected[bank][1]) {
                        bool reused = false;
                        set = slot.caches[bank].Acquire(key, [&]() {
                            const size_t index = slot.used[bank]++;
                            auto& pool = slot.pools[bank];
                            if (index == pool.size()) pool.push_back(builders[bank + 1].create(device.get()));
                            else ++pooledRewrites;
                            auto* fresh = pool[index].get();
                            Check(fresh != nullptr, "pooled descriptor set");
                            for (unsigned element = 0; element < 32; ++element)
                                fresh->setTexture(element, key[element], RenderTextureLayout::SHADER_READ);
                            return fresh;
                        }, reused);
                        hits += reused; misses += !reused;
                    }
                    drawSets[bank] = set;
                    commands->setGraphicsDescriptorSet(set, bank + 1);
                    if (bindingCacheOnly)
                        commands->setGraphicsDescriptorSet(set, bank + 1);
                    for (unsigned endpoint = 0; endpoint < 2; ++endpoint)
                        for (unsigned channel = 0; channel < 4; ++channel)
                            expected[channel] += float(1u << (bank * 2 + endpoint)) * colors[selected[bank][endpoint]][channel];
                }
                if (bindingCacheOnly) {
                    auto* drawSet0 = (draw == 2 || draw == 5) ? alternateSet0.get() : set0.get();
                    commands->setGraphicsDescriptorSet(drawSet0, 0);
                    commands->setGraphicsDescriptorSet(drawSet0, 0);
                    // The low bind can disturb higher sets after an
                    // incompatible layout switch. Reissue the SAME handles
                    // in ascending order before drawing; suppression must
                    // not mistake disturbed bindings for valid duplicates.
                    for (unsigned bank = 0; bank < drawSets.size(); ++bank) {
                        commands->setGraphicsDescriptorSet(drawSets[bank], bank + 1);
                        commands->setGraphicsDescriptorSet(drawSets[bank], bank + 1);
                    }
                    commands->setGraphicsDescriptorSet(samplerSet.get(), 4);
                    commands->setGraphicsDescriptorSet(samplerSet.get(), 4);
                }
                const uint64_t base = slot.constants->getDeviceAddress() + draw * 3 * 256;
                const std::array<uint64_t, 3> addresses{base, base + 256, base + 512};
                commands->setGraphicsPushConstants(0, addresses.data());
                RenderRect scissor(draw, 0, draw + 1, 1); commands->setScissors(&scissor, 1);
                commands->drawInstanced(3, 1, 0, 0);
                slot.expected[draw] = expected;
            }
            commands->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(slot.target.get(), RenderTextureLayout::COPY_SOURCE));
            commands->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(slot.readback.get(), kFormat, kDraws, 1, 1, 16),
                RenderTextureCopyLocation::Subresource(slot.target.get()));
            commands->end();
            const RenderCommandList* lists[]{commands};
            queue->executeCommandLists(lists, 1, nullptr, 0, nullptr, 0, slot.fence.get());
            slot.submitted = true;
        }
        for (auto& slot : slots) complete(slot);
    } catch (...) {
        for (auto& slot : slots) if (slot.submitted) queue->waitForCommandFence(slot.fence.get());
        throw;
    }
    Check(hits && misses && pooledRewrites, "exercise hits, misses and completed-slot rewrites");
    if (bindingCacheOnly) {
        std::printf("Vulkan binding cache: %zu pixels / %zu components passed; duplicate bindings, replacements, incompatible-prefix A/B/A, 2 command lists reused after fences, 4 submissions\n",
            checkedPixels, checkedPixels * 4);
        return;
    }
    std::printf("Vulkan texture reuse: %zu pixels / %zu components passed; hits=%zu misses=%zu pooled_rewrites=%zu; 48 draws, 2 slots, 6 submissions\n",
        checkedPixels, checkedPixels * 4, hits, misses, pooledRewrites);
}
}

int main(int argc, char** argv)
{
    const bool bindingCacheOnly = argc == 2 && std::strcmp(argv[1], "--binding-cache-only") == 0;
    if (argc != 1 && !bindingCacheOnly) {
        std::fprintf(stderr, "Usage: LoVulkanTextureReuseTest [--binding-cache-only]\n");
        return 2;
    }
    try { Run(bindingCacheOnly); return 0; }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Vulkan texture reuse: %s\n", error.what());
        return 1;
    }
}
