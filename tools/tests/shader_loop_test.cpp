// Execute real translated Xenos loops, with a CPU 64-invocation reference.
// The wrapper supplies inputs and observes registers; it does not replace ALUs
// or loop control. No game assets, window or shader cache are required.
#include <gpu/shader/xenos_translator.h>
#include <gpu/shader/xenos_shader_code.h>
#include <gpu/shader/dxc_compiler.h>
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace xenos;

static void Require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
static void Check(HRESULT hr)
{
    Require(SUCCEEDED(hr), "D3D12 failure: " + std::to_string(uint32_t(hr)));
}
static constexpr const char* kExit = "// Inactive loop iterations have no observable effects.";

struct Program
{
    std::vector<ControlFlowInstruction> cf;
    std::vector<std::array<uint32_t, 3>> instructions;
    template<class T> void Add(const T& instruction)
    {
        std::array<uint32_t, 3> words{};
        static_assert(sizeof(T) == 12);
        std::memcpy(words.data(), &instruction, 12);
        instructions.push_back(words);
    }
    std::vector<uint32_t> Encode() const
    {
        size_t cfBytes = ((cf.size() + 1) / 2) * 12;
        std::vector<uint32_t> words(cfBytes / 4 + instructions.size() * 3);
        for (size_t i = 0; i < cf.size(); ++i)
            std::memcpy(reinterpret_cast<char*>(words.data()) + i * 6, &cf[i], 6);
        std::memcpy(reinterpret_cast<char*>(words.data()) + cfBytes,
            instructions.data(), instructions.size() * 12);
        return words;
    }
};

static Program Countdown(bool breakValue, bool enabled = true)
{
    Program p;
    p.cf.resize(4);
    p.cf[0].loopStart.opcode = ControlFlowOpcode::LoopStart;
    p.cf[0].loopStart.address = 3;
    p.cf[1].exec.opcode = ControlFlowOpcode::Exec;
    p.cf[1].exec.address = 2;
    p.cf[1].exec.count = 3;
    p.cf[2].loopEnd.opcode = ControlFlowOpcode::LoopEnd;
    p.cf[2].loopEnd.address = 1;
    p.cf[2].loopEnd.isPredicatedBreak = enabled;
    p.cf[2].loopEnd.condition = breakValue;
    p.cf[3].exec.opcode = ControlFlowOpcode::ExecEnd;
    p.cf[3].exec.address = 5;
    AluInstruction increment{};
    increment.vectorOpcode = AluVectorOpcode::Add;
    increment.vectorDest = 2;
    increment.vectorWriteMask = 1;
    increment.src1Register = 2;
    increment.src2Register = 3;
    increment.src1Select = increment.src2Select = increment.src3Select = 1;
    increment.src1Swizzle = increment.src2Swizzle = increment.src3Swizzle = 0x6C;
    increment.scalarOpcode = AluScalarOpcode::RetainPrev;
    increment.isPredicated = 1;
    increment.predicateCondition = !breakValue;
    p.Add(increment);
    auto decrement = increment;
    decrement.vectorDest = decrement.src1Register = 1;
    decrement.src2Negate = 1;
    p.Add(decrement);
    auto predicate = increment;
    predicate.vectorWriteMask = 0;
    predicate.src3Register = 1;
    predicate.scalarOpcode = breakValue ? AluScalarOpcode::SetpEq : AluScalarOpcode::SetpGt;
    p.Add(predicate);
    return p;
}

static std::string Translate(const Program& p)
{
    auto code = p.Encode();
    auto shader = TranslateShader(code.data(), uint32_t(code.size()), false);
    Require(shader.errors.empty(), shader.errors);
    return shader.hlsl;
}

static std::string ComputeSource(const Program& p, bool breakValue, uint32_t count)
{
    auto source = Translate(p);
    size_t begin = source.find("void main("), body = source.find('{', begin);
    Require(begin != std::string::npos && body != std::string::npos, "Missing shader entry");
    std::string entry = "RWByteAddressBuffer results : register(u0);\n"
        "[numthreads(64,1,1)] void main(uint3 tid : SV_DispatchThreadID)\n{\n"
        "uint xeVertexId = 0u; uint observedIterations = 0u; float4 oPos;\n";
    for (int i = 0; i < 16; ++i) entry += "float4 o" + std::to_string(i) + ";\n";
    source.replace(begin, body - begin + 1, entry);
    size_t epilogue = source.find("\tif ((xeFlags & 8u) == 0u)");
    Require(epilogue != std::string::npos, "Missing vertex epilogue");
    source.resize(epilogue);
    source += "results.Store4(tid.x * 16u, asuint(float4(r2.x, r1.x, p0 ? 1.0 : 0.0, float(observedIterations))));\n}\n";
    size_t loop = source.find("for (uint xeLoop0");
    Require(loop != std::string::npos, "Missing translated structured loop");
    source.insert(loop, "r1.x = float(tid.x % 10u); r2.x = 0.0; r3.x = 1.0; p0 = " +
        std::string(breakValue ? "r1.x == 0.0;\n" : "r1.x > 0.0;\n"));
    loop = source.find("for (uint xeLoop0");
    source.insert(source.find('{', loop) + 1, "\n++observedIterations;\n");
    const std::string loopConstant = "XeLoopConst(0u)";
    for (size_t pos = 0; (pos = source.find(loopConstant, pos)) != std::string::npos;)
    {
        const std::string replacement = std::to_string(count) + "u";
        source.replace(pos, loopConstant.size(), replacement);
        pos += replacement.size();
    }
    return source;
}

// Deliberately group-wide: a lane whose predicate is already false continues
// participating until all 64 lanes reach the break predicate or count expires.
static std::array<std::array<float, 3>, 64> Reference(uint32_t count, bool breakValue, bool enabled)
{
    std::array<std::array<float, 3>, 64> lanes{};
    for (uint32_t i = 0; i < 64; ++i)
        lanes[i] = {0.0f, float(i % 10), float(breakValue ? i % 10 == 0 : i % 10 != 0)};
    for (uint32_t iteration = 0; iteration < count; ++iteration)
    {
        bool allBreak = true;
        for (auto& lane : lanes)
        {
            if (bool(lane[2]) != breakValue)
            {
                lane[0] += 1.0f;
                lane[1] -= 1.0f;
                lane[2] = float(breakValue ? lane[1] == 0.0f : lane[1] > 0.0f);
            }
            allBreak &= bool(lane[2]) == breakValue;
        }
        if (enabled && allBreak) break;
    }
    return lanes;
}

class Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> root;
public:
    Gpu()
    {
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rd{};
        rd.NumParameters = 1;
        rd.pParameters = &parameter;
        ComPtr<ID3DBlob> signature, errors;
        Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)));
    }
    std::array<std::array<float, 4>, 64> Run(const std::string& source)
    {
        auto shader = CompileHlsl(source, "main", "cs_6_0");
        Require(shader.ok, shader.errors);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root.Get();
        pd.CS = {shader.bytecode.data(), shader.bytecode.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline)));
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = 64 * 16;
        buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buffer.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> output, readback;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)));
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        buffer.Flags = D3D12_RESOURCE_FLAG_NONE;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands)));
        commands->SetComputeRootSignature(root.Get());
        commands->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
        commands->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
        commands->ResourceBarrier(1, &barrier);
        commands->CopyResource(readback.Get(), output.Get());
        Check(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(ready != nullptr, "CreateEvent failed");
        Check(fence->SetEventOnCompletion(1, ready));
        Check(queue->Signal(fence.Get(), 1));
        DWORD waited = WaitForSingleObject(ready, 10000);
        CloseHandle(ready);
        Require(waited == WAIT_OBJECT_0, "GPU readback timed out");
        void* values = nullptr;
        D3D12_RANGE range{0, 64 * 16};
        Check(readback->Map(0, &range, &values));
        std::array<std::array<float, 4>, 64> result;
        std::memcpy(result.data(), values, sizeof(result));
        D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
        return result;
    }
};

static void GuardTests()
{
    auto expect = [](const Program& p, bool lowered, const char* name)
    {
        Require((Translate(p).find(kExit) != std::string::npos) == lowered, std::string("Guard failed: ") + name);
        std::printf("PASS guard: %s\n", name);
    };
    auto mutateAlu = [](Program& p, size_t index, auto mutation)
    {
        AluInstruction a{};
        std::memcpy(&a, p.instructions[index].data(), 12);
        mutation(a);
        std::memcpy(p.instructions[index].data(), &a, 12);
    };
    expect(Countdown(false), true, "break false");
    expect(Countdown(true), true, "break true");
    expect(Countdown(false, false), false, "predicated break disabled");
    auto p = Countdown(false);
    mutateAlu(p, 0, [](auto& a) { a.isPredicated = 0; });
    expect(p, false, "unpredicated ALU remains observable");
    p = Countdown(false);
    mutateAlu(p, 0, [](auto& a) { a.predicateCondition = 0; });
    expect(p, false, "opposite body predicate remains observable");
    p = Countdown(false);
    mutateAlu(p, 0, [](auto& a) { a.src1Select = 0; a.const0Relative = 1; });
    expect(p, false, "loop-relative constant in loop");
    p = Countdown(false);
    AluInstruction relative{};
    relative.vectorOpcode = AluVectorOpcode::Max;
    relative.vectorDest = 4;
    relative.vectorWriteMask = 1;
    relative.const0Relative = 1;
    relative.scalarOpcode = AluScalarOpcode::RetainPrev;
    p.Add(relative);
    p.cf[3].exec.count = 1;
    expect(p, false, "loop-relative constant after loop");
    p = Countdown(false);
    TextureFetchInstruction fetch{};
    fetch.opcode = FetchOpcode::TextureFetch;
    fetch.srcRegister = 4;
    fetch.dstRegister = 5;
    fetch.dstSwizzle = 0x688;
    fetch.dimension = TextureDimension::Texture2D;
    fetch.isPredicated = fetch.predCondition = 1;
    fetch.useCompLod = 1;
    std::memcpy(p.instructions[0].data(), &fetch, 12);
    p.cf[1].exec.sequence = 1;
    expect(p, false, "implicit texture LOD needs quad participation");
    fetch.useCompLod = 0;
    std::memcpy(p.instructions[0].data(), &fetch, 12);
    expect(p, true, "explicit texture LOD permits lane exit");
    p = Countdown(false);
    p.cf.insert(p.cf.begin() + 1, p.cf[0]);
    p.cf.insert(p.cf.begin() + 4, p.cf[3]);
    p.cf[0].loopStart.address = 5;
    p.cf[1].loopStart.address = 4;
    p.cf[1].loopStart.loopId = 1;
    p.cf[2].exec.address = 3;
    p.cf[3].loopEnd.address = 2;
    p.cf[3].loopEnd.loopId = 1;
    p.cf[4].loopEnd.address = 1;
    p.cf[5].exec.address = 6;
    expect(p, false, "nested loops excluded");
    p = Countdown(false);
    ControlFlowInstruction jump{};
    jump.condJmp.opcode = ControlFlowOpcode::CondJmp;
    jump.condJmp.address = 3;
    jump.condJmp.isUnconditional = 1;
    p.cf.insert(p.cf.begin() + 1, jump);
    p.cf.push_back({});
    p.cf[0].loopStart.address = 4;
    p.cf[2].exec.address = 3;
    p.cf[3].loopEnd.address = 1;
    p.cf[4].exec.address = 6;
    expect(p, false, "control-flow jump excluded");
}

int main()
{
    try
    {
        GuardTests();
        Gpu gpu;
        for (bool breakValue : {false, true})
            for (bool enabled : {false, true})
                for (uint32_t count : {0u, 1u, 255u})
                {
                    auto actual = gpu.Run(ComputeSource(Countdown(breakValue, enabled), breakValue, count));
                    auto expected = Reference(count, breakValue, enabled);
                    for (uint32_t lane = 0; lane < 64; ++lane)
                    {
                        for (size_t component = 0; component < 3; ++component)
                            Require(actual[lane][component] == expected[lane][component], "GPU output differs from group64 reference at lane " + std::to_string(lane));
                        uint32_t iterations = enabled ? std::min(count, std::max(1u, lane % 10u)) : count;
                        Require(actual[lane][3] == float(iterations), "GPU did not execute expected iteration count at lane " + std::to_string(lane));
                    }
                    std::printf("PASS GPU: break=%u enabled=%u count=%u, 64 mixed lanes\n", breakValue, enabled, count);
                }
        std::puts("PASS: LoopEnd guards, group64 output equivalence, and executed iteration counts");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
