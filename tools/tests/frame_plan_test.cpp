#include <gpu/frame_plan.h>
#include <gpu/movie_clear.h>
#include <cstdio>
#include <cstdlib>

namespace {
int checks = 0;
void Require(bool value, const char* what) { ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
}

int main()
{
    using namespace gpu::frame_plan;
    Require(Choose(3, 7, 0, 2560, 1080) == FramePlan{3, 7, 2560, 1080}, "2560x1080 plan retains native raster");
    Require(Choose(4, 8, 0, 3440, 1440) == FramePlan{4, 8, 3440, 1440}, "3440x1440 plan retains native raster");
    Require(Choose(5, 9, 1080, 3440, 1440) == FramePlan{5, 9, 2580, 1080}, "manual height preserves wide aspect with integer rounding");
    Require(Choose(6, 10, 0, 3440, 1440, true) == FramePlan{6, 10, 1280, 720}, "readback is native before plan publication");
    Require(Reduced({7, 11, 3440, 1440}, 1080) == FramePlan{7, 11, 2580, 1080}, "failure retry preserves plan aspect");
    Require(Reduced({8, 12, 1720, 720}, 720) == FramePlan{8, 12, 1720, 720}, "lowest retry height does not loop below supported limit");
    FailureState failures;
    uint64_t retryEpoch = 0;
    auto retry = AdvanceCpuPlan(failures, 1, retryEpoch, ~0ull, 720, 0, 3440, 1440, false);
    Require(retry.geometryEpoch == 1 && retry.width == 3440 && retry.height == 1440, "first valid plan has nonzero epoch");
    retry = AdvanceCpuPlan(failures, 2, retryEpoch, retry.geometryEpoch, 1080, 0, 3440, 1440, false);
    Require(retry.geometryEpoch == 2 && retry == FramePlan{2, 2, 2580, 1080}, "first failure reduces the original request");
    retry = AdvanceCpuPlan(failures, 3, retryEpoch, retry.geometryEpoch, 810, 0, 3440, 1440, false);
    Require(retry.geometryEpoch == 3 && retry == FramePlan{3, 3, 1935, 810}, "repeated failure keeps decreasing the request cap");
    retry = AdvanceCpuPlan(failures, 4, retryEpoch, retry.geometryEpoch, 720, 0, 3440, 1440, false);
    Require(retry.geometryEpoch == 4 && retry == FramePlan{4, 4, 1720, 720}, "failure reaches supported minimum once");
    retry = AdvanceCpuPlan(failures, 5, retryEpoch, retry.geometryEpoch, 720, 0, 3440, 1440, false);
    Require(retry.failed && retry.geometryEpoch == 4 && retry.width == 1720 && retry.height == 720, "lowest failure remains terminal for its request");
    retry = AdvanceCpuPlan(failures, 6, retryEpoch, retry.geometryEpoch, 720, 0, 2560, 1080, false);
    Require(!retry.failed && retry.geometryEpoch == 5 && retry == FramePlan{6, 5, 2560, 1080}, "request change clears terminal failure and cap");
    Require(gpu::movie_clear::ScaleBoundary(163.63636f, 3440, 1280) == 440, "movie bar boundary is float-scaled then rounded once");

    CommandTags tags;
    tags.Store(0x1000, {11, 12, 2560, 1080});
    Require(tags.Take(0x1000) == FramePlan{11, 12, 2560, 1080}, "tag follows its reserved command");
    Require(!tags.Take(0x1000), "tag is consumed exactly once");
    tags.Store(0x8336A7A4, {12, 13, 2560, 1080});
    Require(tags.Take(0x8336A7A4)->cpuSerial == 12, "production ring object address tags normally");

    wire::PlanStage stage;
    Require(!stage.Write(wire::PlanBase, wire::Magic), "plan begin stages split packet");
    stage.Write(wire::PlanBase + 1, 13); stage.Write(wire::PlanBase + 2, 0);
    stage.Write(wire::PlanBase + 3, 14); stage.Write(wire::PlanBase + 4, 0);
    stage.Write(wire::PlanBase + 5, 3440); stage.Write(wire::PlanBase + 6, 1440);
    Require(stage.Write(wire::PlanBase + 7, wire::Magic) == FramePlan{13, 14, 3440, 1440}, "private registers commit after split writes");
    std::printf("frame plan: %d checks passed\n", checks);
}
