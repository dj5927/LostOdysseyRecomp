#pragma once
#include <cstdlib>
#include <string_view>
namespace gpu::temporal {
// Opt-in until real-game coverage and hardware costs are measured. Master OFF
// avoids tracking, shader compilation, allocation, replay and MV consumption.
struct MotionOptions {
    bool enabled = false, replay = false, consume = false, debug = false, log = false, timing = false;
    static MotionOptions Environment() {
        auto on = [](const char* n) { const auto* p = std::getenv(n); return p && std::string_view(p) == "1"; };
        auto notOff = [](const char* n) { const auto* p = std::getenv(n); return !p || std::string_view(p) != "0"; };
        MotionOptions out; out.enabled = on("LO_MV_ENABLE");
        out.replay = out.enabled && notOff("LO_MV_REPLAY");
        out.consume = out.replay && notOff("LO_MV_CONSUME");
        out.debug = out.consume && on("LO_MV_DEBUG"); out.log = on("LO_MV_LOG");
        out.timing = on("LO_MV_TIMING"); // TAA baseline A can be measured with MV disabled.
        return out;
    }
};
}
