#include <gpu/motion_vector.h>
#include <gpu/motion_options.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <new>
#include <stdexcept>
static size_t allocations=0;
void* operator new(size_t n) { ++allocations; if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
using namespace gpu::temporal;
static unsigned checks=0;
static void Require(bool ok,const char* why) {++checks;if(!ok)throw std::runtime_error(why);}
static Matrix Projection() {return {1,0,0,0, 0,2,0,0, 0,0,100./99,1, 0,0,-100./99,0};}
int main() {try {
    const auto hash = MotionHashWord(0xcbf29ce484222325ULL, 7);
    Require(hash != MotionHashWord(0xcbf29ce484222325ULL, 8), "stream data changes geometry identity");
    Require(MotionHashWord(hash, 1) != MotionHashWord(hash, 0x100000001ULL), "full arena generation participates in geometry identity");
    DrawTemporalTracker t(8);DrawHistoryKey k{};k.vsHash=7;k.sceneAllocation=1;k.geometrySignature=2;
    std::array<uint32_t,1024> constants{};std::array<uint32_t,52> shared{};
    constants[0]=0x80000000;constants[1023]=0x7fc12345;shared[7]=0xabcdef01;shared[39]=0x12345678;
    t.BeginFrame(1,10);auto m=t.Collect(k,constants.data(),shared.data(),true);
    Require(!m.previous,"first frame has no history");t.BeginFrame(1,10);Require(t.ActiveDrawCount()==1,"same frame is idempotent");
    Require(t.FinalizeFrame()[m.tag]==0,"first-frame tag invalid");
    t.BeginFrame(2,10);m=t.Collect(k,constants.data(),shared.data(),true);Require(m.previous,"previous full state exists");
    uint32_t bits;std::memcpy(&bits,&m.previous->vsConstants[1023],4);Require(bits==constants[1023],"exact bitwise full c255 snapshot including NaN payload");
    Require(m.previous->shared[7]==shared[7]&&m.previous->shared[39]==shared[39],"bool and loop banks retained");
    auto duplicate=t.Collect(k,constants.data(),shared.data(),true);Require(!duplicate.previous,"duplicate has no provisional match");
    Require(t.FinalizeFrame()[m.tag]==0,"duplicate revokes first tag at finalization");
    Require(t.Stats().matchedPreviousDraws==0,"statistics count final decisions, not provisional matches");
    Require(!t.FindPrevious(k),"duplicate cannot bypass final matching through lookup");
    t.BeginFrame(3,10);Require(!t.Collect(k,constants.data(),shared.data(),true).previous,"previous ambiguous instance cannot become valid");t.FinalizeFrame();
    t.BeginFrame(4,11);Require(!t.Collect(k,constants.data(),shared.data(),true).previous,"epoch reset invalidates all object history");t.FinalizeFrame();
    t.BeginFrame(6,11);Require(!t.Collect(k,constants.data(),shared.data(),true).previous,"frame gap invalidates history");
    t.BeginFrame(7,11);Require(!t.Collect(k,constants.data(),shared.data(),true).previous,"unfinished frame cannot supply history");t.FinalizeFrame();
    t.BeginFrame(8,11);m=t.Collect(k,constants.data(),shared.data(),true);Require(t.FinalizeFrame()[m.tag]==1,"consecutive unique draw accepted");
    Require(t.Stats().skinnedMatches==1,"relative-state diagnostic count after freeze");
    t.Collect(k,constants.data(),shared.data(),true);Require(t.Failed()&&t.FinalizeFrame()[m.tag]==0,"late collection invalidates entire frozen view");
    t.BeginFrame(9,11);Require(!t.Collect(k,constants.data(),shared.data(),true).previous,"failed frame not promoted");t.FinalizeFrame();
    t.BeginFrame(10,11);auto other=k;other.geometrySignature++;Require(!t.Collect(other,constants.data(),shared.data(),false).previous,"changed geometry generation rejects previous");t.FinalizeFrame();
    t.BeginFrame(11,11);Require(!t.Collect(other,nullptr,shared.data(),false).previous,"missing input never marked valid");t.FinalizeFrame();
    DrawTemporalTracker bounded(2);bounded.BeginFrame(1);bounded.Collect(k,constants.data(),shared.data(),false);other.vsHash++;bounded.Collect(other,constants.data(),shared.data(),false);other.vsHash++;
    bounded.Collect(other,constants.data(),shared.data(),false);Require(bounded.Failed()&&bounded.ActiveDrawCount()==2,"capacity overflow bounded, no growth/fake match");
    const auto& validity=bounded.FinalizeFrame();Require(std::all_of(validity.begin(),validity.end(),[](auto x){return x==0;}),"overflow invalidates all tags");
    DrawTemporalTracker steady(128);
    for(uint64_t f=1;f<=3;++f){steady.BeginFrame(f);for(unsigned i=0;i<100;++i){k.vsHash=i;steady.Collect(k,constants.data(),shared.data(),false);}steady.FinalizeFrame();}
    const size_t before=allocations;
    for(uint64_t f=4;f<104;++f){steady.BeginFrame(f);for(unsigned i=0;i<100;++i){k.vsHash=i;steady.Collect(k,constants.data(),shared.data(),false);}steady.FinalizeFrame();}
    Require(allocations==before,"steady-state tracker performs zero heap allocations across 10,000 draws");
    const Viewport vp{0,0,1280,720};auto cam=Camera::Create(Projection(),vp);Require(bool(cam),"reference projection");auto movedMatrix=Projection();movedMatrix[12]=-1;auto previousCam=Camera::Create(movedMatrix,vp);
    auto mv=ReprojectMotionVector({640.5,360.5,1./11},*cam,*previousCam);Require(mv.valid&&std::abs(mv.x+64)<.001,"camera reference correct reverse-Z -64px");
    Require(!ReprojectMotionVector({640.5,360.5,0},*cam,*cam).valid,"invalid depth explicitly distinguishable from stationary zero");
    Require(ReprojectMotionVector({640.5,360.5,1},*cam,*cam).valid,"reverse-Z near endpoint 1 remains valid");
    Require(ReprojectMotionVector({640.5,360.5,.5},*cam,*cam).valid,"valid stationary vector remains valid");
    MotionVectorProducer p({1,1});auto one=Camera::Create(Projection(),{0,0,1,1});float depth=1;std::vector<MotionVectorPixel> grid;
    Require(p.EvaluateGrid(&depth,&*one,&*one,nullptr,grid)&&grid[0].reactiveMask==0,"CPU grid shares depth endpoint contract");
    Require(p.EvaluateGrid(&depth,&*one,nullptr,nullptr,grid)&&grid[0].reactiveMask==1,"no previous camera is invalid, not stationary");
    const auto opts=MotionOptions::Environment();if(!std::getenv("LO_MV_ENABLE"))Require(!opts.enabled&&!opts.replay&&!opts.consume,"master default OFF");
    printf("PASS: %u motion lifecycle/reference checks; zero steady-state tracking allocations\n",checks);return 0;
} catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
