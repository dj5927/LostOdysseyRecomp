#include <gpu/motion_vector.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>

using namespace gpu::temporal;

static int checks = 0;
static void Require(bool ok, const char* message)
{
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void Near(float a, float b, const char* message, float tolerance = 1e-4f)
{
    Require(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}

static Matrix Projection(double farPlane = 100.0)
{
    return {1,0,0,0, 0,2,0,0, 0,0,farPlane/(farPlane-1),1, 0,0,-farPlane/(farPlane-1),0};
}

static Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix c{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) {
                c[4 * i + j] += a[4 * i + k] * b[4 * k + j];
            }
        }
    }
    return c;
}

int main()
{
    // 1. Verify DrawTemporalTracker double-buffering and identity matching
    {
        DrawTemporalTracker tracker;
        DrawHistoryKey keyA{};
        keyA.vsHash = 0x12345678ULL;
        keyA.indexBufferAddress = 0x80000000;
        keyA.firstIndex = 0;
        keyA.indexCount = 36;
        keyA.baseVertex = 0;
        keyA.primitiveType = 4; // Triangles
        keyA.positionBufferAddress = 0x82000000;

        std::array<float, 1024> vsConstantsCurrent{};
        vsConstantsCurrent[0] = 1.0f; // c0.x
        vsConstantsCurrent[4] = 2.0f; // c1.x

        std::array<uint32_t, 8> boolConst{};
        boolConst[0] = 1;
        std::array<uint32_t, 32> loopConst{};
        tracker.BeginFrame(1);
        tracker.RecordDraw(keyA, vsConstantsCurrent.data(), boolConst.data(), loopConst.data(), false);
        Require(tracker.ActiveDrawCount() == 1, "Draw recorded in frame 1");

        // Idempotent BeginFrame test: calling BeginFrame with same frame & epoch must be a no-op
        tracker.BeginFrame(1, 0);
        Require(tracker.ActiveDrawCount() == 1, "BeginFrame is idempotent for same frame index and epoch");

        const DrawTemporalState* prevA = tracker.FindPrevious(keyA);
        Require(prevA == nullptr, "No previous state in first frame");

        // Advance to frame 2
        tracker.BeginFrame(2);
        Require(tracker.ActiveDrawCount() == 0, "Current draws cleared on frame start");

        // Previous draw state should now be accessible
        prevA = tracker.FindPrevious(keyA);
        Require(prevA != nullptr, "Previous state available in frame 2");
        Near(prevA->vsConstants[0], 1.0f, "Preserved c0.x in previous state");
        Near(prevA->vsConstants[4], 2.0f, "Preserved c1.x in previous state");

        // Record updated draw in frame 2
        std::array<float, 1024> vsConstantsNext{};
        vsConstantsNext[0] = 1.5f;
        tracker.RecordDraw(keyA, vsConstantsNext.data(), boolConst.data(), loopConst.data(), false);
        Require(tracker.ActiveDrawCount() == 1, "Draw recorded in frame 2");
        Require(tracker.Stats().matchedPreviousDraws == 1, "Draw matched previous frame identity");

        // Duplicate key within same frame: collision detection must invalidate both / reject ambiguous matches
        DrawHistoryKey keyDup = keyA;
        const DrawTemporalState* dupRes = tracker.RecordDraw(keyDup, vsConstantsNext.data(), boolConst.data(), loopConst.data(), false);
        Require(dupRes == nullptr, "Collision in same frame rejects match");
        Require(tracker.Stats().ambiguousRejectedMatches >= 1, "Collision registered as ambiguous rejected match");
    }

    // 2. Verify MotionVectorProducer CPU evaluation with static camera
    {
        const uint32_t width = 64;
        const uint32_t height = 36;
        const Viewport raster{0, 0, static_cast<double>(width), static_cast<double>(height), 1, 1.0 / width, -1.0 / height};
        auto camCur = Camera::Create(Projection(), raster);
        Require(camCur.has_value(), "Valid current camera");

        MotionVectorProducerDesc desc;
        desc.width = width;
        desc.height = height;
        MotionVectorProducer producer(desc);

        // Fill test depth buffer: valid depth 0.5 in center, 1.0 (sky/clear) around edges
        std::vector<float> depthBuffer(width * height, 0.5f);
        depthBuffer[0] = 1.0f; // Skybox / clear pixel

        std::vector<MotionVectorPixel> grid;
        MotionVectorProducer::ProducerStats stats{};

        // First frame: no previous camera -> all reactive
        bool ok = producer.EvaluateGrid(depthBuffer.data(), &(*camCur), nullptr, nullptr, grid, &stats);
        Require(ok, "First frame evaluation succeeded");
        Require(stats.reactivePixels == width * height, "All pixels reactive on first frame without history");

        // Static camera: previous == current -> motion vector must be {0, 0} for valid pixels
        ok = producer.EvaluateGrid(depthBuffer.data(), &(*camCur), &(*camCur), nullptr, grid, &stats);
        Require(ok, "Static camera evaluation succeeded");
        Require(grid[0].reactiveMask == 1.0f, "Clear depth pixel flagged reactive");

        const size_t centerIdx = (height / 2) * width + (width / 2);
        Near(grid[centerIdx].vx, 0.0f, "Static camera center pixel vx is zero");
        Near(grid[centerIdx].vy, 0.0f, "Static camera center pixel vy is zero");
        Require(grid[centerIdx].reactiveMask == 0.0f, "Valid static depth has zero reactive mask");
    }

    // 3. Verify Camera Translation Motion Vector (camera moved right -> scene pixels move left in backward vector)
    {
        const uint32_t width = 1280;
        const uint32_t height = 720;
        const Viewport raster{0, 0, static_cast<double>(width), static_cast<double>(height), 1, 1.0 / width, -1.0 / height};
        auto camCur = Camera::Create(Projection(), raster);
        Require(camCur.has_value(), "Valid current camera");

        // Previous camera translated +1 in X
        Matrix translation{1,0,0,0, 0,1,0,0, 0,0,1,0, -1,0,0,1};
        auto camPrev = Camera::Create(Multiply(translation, Projection()), raster);
        Require(camPrev.has_value(), "Valid previous camera");

        // Center pixel at depth corresponding to world z = 10:
        // Analytic pinhole depth = (100 / 10 - 1) / 99 = 9 / 99 = 1 / 11 ~ 0.09090909f
        const float depthZ10 = (100.0f / 10.0f - 1.0f) / 99.0f;
        std::vector<float> depthBuffer(width * height, depthZ10);

        MotionVectorProducerDesc desc;
        desc.width = width;
        desc.height = height;
        MotionVectorProducer producer(desc);

        std::vector<MotionVectorPixel> grid;
        MotionVectorProducer::ProducerStats stats{};
        bool ok = producer.EvaluateGrid(depthBuffer.data(), &(*camCur), &(*camPrev), nullptr, grid, &stats);
        Require(ok, "Translating camera evaluation succeeded");

        const size_t centerIdx = (height / 2) * width + (width / 2);
        // In temporal_math_test: static world point moves left by 640/z = 64 pixels:
        // backward displacement = previous - current = -64 pixels
        Near(grid[centerIdx].vx, -64.0f, "Translating camera backward vx displacement matches pinhole analytic", 0.05f);
        Near(grid[centerIdx].vy, 0.0f, "Translating camera backward vy displacement is zero", 0.05f);
        Require(grid[centerIdx].reactiveMask == 0.0f, "Valid displacement within threshold is not reactive");
    }

    // 4. Verify M3: Rigid Object Motion vs Static Camera (Bell stand / rigid mesh replay)
    // Bell stand (vsHash 0xb030ab4e17a20783, slot 4). Object moves in world space while camera is static.
    {
        DrawTemporalTracker tracker;
        DrawHistoryKey bellKey{};
        bellKey.vsHash = 0xb030ab4e17a20783ULL;
        bellKey.indexBufferAddress = 0x81000000;
        bellKey.firstIndex = 0;
        bellKey.indexCount = 144;
        bellKey.baseVertex = 0;
        bellKey.primitiveType = 4;
        bellKey.positionBufferAddress = 0x83000000;

        std::array<uint32_t, 8> boolConst{};
        std::array<uint32_t, 32> loopConst{};

        // Frame 1: Bell object at origin (world pos = (0, 0, 10))
        // World matrix in c0..c3 (row-major or column-major float4):
        // c0=(1,0,0,0), c1=(0,1,0,0), c2=(0,0,1,10), c3=(0,0,0,1)
        std::array<float, 1024> bellVSPrev{};
        bellVSPrev[0] = 1.0f; bellVSPrev[5] = 1.0f; bellVSPrev[10] = 1.0f; bellVSPrev[11] = 10.0f; bellVSPrev[15] = 1.0f;
        // VP matrix at slot 4 (c4..c7):
        bellVSPrev[16] = 1.0f; bellVSPrev[21] = 2.0f; bellVSPrev[26] = 100.0f / 99.0f; bellVSPrev[27] = 1.0f;
        bellVSPrev[30] = -100.0f / 99.0f;

        tracker.BeginFrame(1);
        tracker.RecordDraw(bellKey, bellVSPrev.data(), boolConst.data(), loopConst.data(), false);

        // Frame 2: Static camera, but bell object moves +0.5 in world X (world pos = (0.5, 0, 10))
        std::array<float, 1024> bellVSCur = bellVSPrev;
        bellVSCur[3] = 0.5f; // translation X +0.5

        tracker.BeginFrame(2);
        const DrawTemporalState* prevMatched = tracker.RecordDraw(bellKey, bellVSCur.data(), boolConst.data(), loopConst.data(), false);
        Require(prevMatched != nullptr, "Bell draw matched previous frame rigid state");
        Require(tracker.Stats().matchedPreviousDraws == 1, "Matched rigid previous draw count incremented");

        // Compute backward motion vector for vertex at (0, 0, 0) local:
        // Previous world pos = (0, 0, 10), current world pos = (0.5, 0, 10).
        // Backward world displacement = previous - current = (-0.5, 0, 0).
        // Projected on static 1280x720 camera at z=10:
        // dx = -0.5 * (1280 / 2) / 10 = -32.0 pixels.
        float prevWorldX = prevMatched->vsConstants[3];
        float curWorldX = bellVSCur[3];
        float backwardDeltaX = prevWorldX - curWorldX;
        Near(backwardDeltaX, -0.5f, "Rigid object backward world X displacement is -0.5");
        float pixelMvX = backwardDeltaX * (1280.0f * 0.5f) / 10.0f;
        Near(pixelMvX, -32.0f, "Rigid object backward pixel MV is -32.0 pixels");
    }

    // 5. Verify M3: Skinned Object Bone Transform Caching & Replay (Battle Skinned Mesh, slot 233)
    // Skinned character (vsHash 0x0eb223d33f8e8e0cULL, slot 233, usesRelativeConstants = true)
    {
        DrawTemporalTracker tracker;
        DrawHistoryKey skinKey{};
        skinKey.vsHash = 0x0eb223d33f8e8e0cULL;
        skinKey.indexBufferAddress = 0x84000000;
        skinKey.firstIndex = 0;
        skinKey.indexCount = 600;
        skinKey.baseVertex = 0;
        skinKey.primitiveType = 4;
        skinKey.positionBufferAddress = 0x85000000;

        std::array<uint32_t, 8> boolConst{};
        std::array<uint32_t, 32> loopConst{};

        // Frame 1: Bone matrix in relative constant register window c64..c67
        std::array<float, 1024> skinVSPrev{};
        skinVSPrev[64 * 4 + 0] = 1.0f; // bone 0 matrix
        skinVSPrev[64 * 4 + 3] = 2.0f; // bone 0 translation X = 2.0

        tracker.BeginFrame(1);
        tracker.RecordDraw(skinKey, skinVSPrev.data(), boolConst.data(), loopConst.data(), true);

        // Frame 2: Bone transforms rotated/translated (bone 0 translation X moves from 2.0 to 2.2)
        std::array<float, 1024> skinVSCur = skinVSPrev;
        skinVSCur[64 * 4 + 3] = 2.2f;

        tracker.BeginFrame(2);
        const DrawTemporalState* prevMatchedSkin = tracker.RecordDraw(skinKey, skinVSCur.data(), boolConst.data(), loopConst.data(), true);
        Require(prevMatchedSkin != nullptr, "Skinned mesh draw matched previous frame state");
        Require(prevMatchedSkin->isSkinned, "Skinned state preserved flag isSkinned");

        // Verify that full 256 float4 constant window preserved bone matrix
        Near(prevMatchedSkin->vsConstants[64 * 4 + 3], 2.0f, "Previous bone matrix translation preserved in 256 float4 buffer");
        Near(skinVSCur[64 * 4 + 3], 2.2f, "Current bone matrix translation correctly updated");

        float boneBackwardDeltaX = prevMatchedSkin->vsConstants[64 * 4 + 3] - skinVSCur[64 * 4 + 3];
        Near(boneBackwardDeltaX, -0.2f, "Skinned bone backward displacement is -0.2", 1e-4f);
    }

    std::printf("PASS: %d motion vector producer & draw tracker checks\n", checks);
    return 0;
}