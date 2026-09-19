#include "gpu/taa_live_control.h"
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    using namespace gpu::temporal;
    LiveOptions state;std::string error;
    auto parse=[&](const char* text){std::istringstream input(text);return ReadLiveOptions(input,state,error);};
    auto require=[](bool ok){if(!ok)throw std::runtime_error("live-control assertion failed");};
    if(argc==2&&std::string(argv[1])=="--stationary-color-clip") {
        require(parse("serial=1\n"));require(state.stationary_color_clip==0);
        require(parse("serial=2\nstationary_color_clip=1\n"));require(state.stationary_color_clip==1);
        require(!parse("serial=3\nstationary_color_clip=2\n"));require(state.serial==2&&state.stationary_color_clip==1);
        std::cout<<"PASS: stationary_color_clip parser\n";return 0;
    }
    require(parse("serial=12\nhistory_weight=0.9\nacceptance=2\nsnap_stationary=1\n"));
    require(state.serial==12&&state.history_weight==.9f&&state.acceptance==2&&state.snap_stationary==1);
    for(auto invalid:{"serial=-1", "serial=1\nhistory_weight=nan", "serial=1\nunknown=1", "serial=1\nserial=2",
                     "serial=1\nmotion_min=.5\nmotion_max=.1", "serial=1\ncoverage=2", "serial=1\naa=1.5", "serial=1\nhistory_weight=.99"}) {
        require(!parse(invalid));require(state.serial==12&&state.history_weight==.9f);
    }
    require(parse("serial=13\naa=2\ncoverage=0\n"));require(state.aa==2&&state.coverage==0&&state.history_weight==.85f);
    require(LiveJsonString("a\"b\\c\n")=="\"a\\\"b\\\\c \"");
    std::cout<<"PASS: atomic live control parsing, ranges, serials and JSON escaping\n";
}
