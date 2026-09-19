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

    std::printf("PASS: %d motion vector producer & draw tracker checks\n", checks);
    return 0;
}