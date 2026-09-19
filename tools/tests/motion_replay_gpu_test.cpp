#include <gpu/motion_replay_gpu.h>
#include <gpu/temporal_history.h>
#include <gpu/temporal_jitter.h>
#include <gpu/shader/dxc_compiler.h>
#include "motion_replay_fixture.h"
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <cmath>
namespace plume { std::unique_ptr<RenderInterface> CreateVulkanInterface(); }
using namespace plume;
using namespace gpu::temporal;
static unsigned checks=0;
static void Require(bool ok,const std::string& what){++checks;if(!ok)throw std::runtime_error(what);printf("PASS: %s\n",what.c_str());}
static void Near(float value,float expected,const char* what,float tolerance=.025f){Require(std::isfinite(value)&&std::abs(value-expected)<=tolerance,std::string(what)+" value="+std::to_string(value)+" expected="+std::to_string(expected));}
static float Half(uint16_t x) {
    const float sign=(x&0x8000)?-1.f:1.f; int e=(x>>10)&31; int m=x&1023;
    return e==0 ? sign*std::ldexp(float(m),-24) : e==31 ? (m?NAN:sign*INFINITY) : sign*std::ldexp(1.f+float(m)/1024,e-15);
}
class Fixture {
public:
    static constexpr uint32_t W=64,H=64;
    std::unique_ptr<RenderInterface> api;
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<RenderCommandQueue> queue;
    std::unique_ptr<RenderCommandList> cmd;
    std::unique_ptr<RenderCommandFence> fence;
    std::unique_ptr<RenderBuffer> vertices,vsCB,psCB,sharedCB,mvCB,upload,readback;
    std::unique_ptr<RenderTexture> color,depth,sceneDepth;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    RenderDescriptorSetBuilder builders[5];
    std::unique_ptr<RenderDescriptorSet> sets[5];
    std::unique_ptr<RenderPipelineLayout> layout;
    MotionReplayGPU replay;
    DrawTemporalTracker tracker;
    uint64_t token=0;
    std::array<float,1024> previous{},current{},pixel{};
    motion_fixture::Shared shared;
    std::vector<std::unique_ptr<RenderShader>> keepShaders;
    std::vector<std::unique_ptr<RenderPipeline>> keepPipelines;
    Fixture() {
        api=CreateVulkanInterface(); Require(bool(api),"Vulkan API"); device=api->createDevice();Require(bool(device),"Vulkan device");
        printf("Device: %s\n",device->getDescription().name.c_str());
        queue=device->createCommandQueue(RenderCommandListType::DIRECT);cmd=queue->createCommandList();fence=device->createCommandFence();
        auto buffer=[&](size_t n, uint32_t f){return device->createBuffer(RenderBufferDesc::UploadBuffer(n,f));};
        vertices=buffer(256,RenderBufferFlag::STORAGE);vsCB=buffer(4096,RenderBufferFlag::CONSTANT);psCB=buffer(4096,RenderBufferFlag::CONSTANT);
        sharedCB=buffer(1024,RenderBufferFlag::CONSTANT);mvCB=buffer(4352,RenderBufferFlag::CONSTANT);
        upload=buffer(W*H*4,RenderBufferFlag::NONE);readback=device->createBuffer(RenderBufferDesc::ReadbackBuffer(W*H*8));
        std::array<float,12> v={-.9f,-.9f,.5f,1, .9f,-.9f,.5f,1, 0,.9f,.5f,1};Write(vertices.get(),v.data(),sizeof(v));
        color=device->createTexture(RenderTextureDesc::Texture2D(W,H,1,RenderFormat::R8G8B8A8_UNORM,RenderTextureFlag::RENDER_TARGET));
        depth=device->createTexture(RenderTextureDesc::Texture2D(W,H,1,RenderFormat::D32_FLOAT,RenderTextureFlag::DEPTH_TARGET));
        sceneDepth=device->createTexture(RenderTextureDesc::Texture2D(W,H,1,RenderFormat::R32_FLOAT));
        const RenderTexture* a[]={color.get()};framebuffer=device->createFramebuffer(RenderFramebufferDesc(a,1,depth.get()));
        for(unsigned i=0;i<5;++i){builders[i].begin();if(i==0)builders[i].addByteAddressBuffer(0);else if(i==4)builders[i].addSampler(0,64);else builders[i].addTexture(0);builders[i].end();sets[i]=builders[i].create(device.get());}
        sets[0]->setBuffer(0,vertices.get(),256);
        RenderPipelineLayoutBuilder b;b.begin(false,false);b.addPushConstant(0,0,24,RenderShaderStageFlag::VERTEX|RenderShaderStageFlag::PIXEL);
        for(auto& s:builders)b.addDescriptorSet(s);b.end();layout=b.create(device.get());
        Require(replay.Init(device.get(),builders,5),"replay GPU initialization: "+replay.LastError());
        pixel[0]=pixel[1]=pixel[2]=.5f;pixel[3]=1;
    }
    static void Write(RenderBuffer* b,const void* data,size_t n){auto* p=b->map();if(!p)throw std::runtime_error("map failed");std::memcpy(p,data,n);b->unmap();}
    void Submit(){replay.SealTimings(cmd.get());cmd->end();const RenderCommandList* c[]={cmd.get()};queue->executeCommandLists(c,1,nullptr,0,nullptr,0,fence.get());queue->waitForCommandFence(fence.get());replay.ReleaseCompletedThrough(replay.RecordedSerial());}
    std::unique_ptr<RenderShader> Compile(const std::string& source,bool ps) {
        auto c=xenos::CompileHlsl(source,"main",ps?"ps_6_0":"vs_6_0",xenos::ShaderBinaryFormat::Spirv);
        if(!c.ok)throw std::runtime_error(c.errors);
        return device->createShader(c.bytecode.data(),c.bytecode.size(),"main",RenderShaderFormat::SPIRV);
    }
    // Reads real GPU output. All waits are test-only, outside the runtime implementation.
    std::vector<uint8_t> Read(RenderTexture* tex,RenderFormat format,unsigned bytes) {
        const unsigned stride=(W*bytes+255)&~255u;
        cmd->begin();cmd->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(tex,RenderTextureLayout::COPY_SOURCE));
        cmd->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(readback.get(),format,W,H,1,stride/bytes),RenderTextureCopyLocation::Subresource(tex));
        cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(tex,RenderTextureLayout::SHADER_READ));Submit();
        std::vector<uint8_t> out(W*H*bytes);auto* p=static_cast<const uint8_t*>(readback->map());
        for(unsigned y=0;y<H;++y)std::memcpy(out.data()+y*W*bytes,p+y*stride,W*bytes);readback->unmap();return out;
    }
    MotionFrameView Run(bool skin=false,bool duplicate=false,bool alphaReject=false, float jitterX=0, float jitterY=0) {
        const auto vp=motion_fixture::Vertex(skin),pp=motion_fixture::Pixel();auto vh=vp.Host(),ph=pp.Host();auto vg=vp.Guest(),pg=pp.Guest();
        auto tv=xenos::TranslateShader(vh.data(),uint32_t(vh.size()),false),tp=xenos::TranslateShader(ph.data(),uint32_t(ph.size()),true);
        if(!tv.errors.empty()||!tp.errors.empty())throw std::runtime_error(tv.errors+tp.errors);
        auto v=Compile(tv.hlsl,false),p=Compile(tp.hlsl,true);
        RenderGraphicsPipelineDesc d;d.pipelineLayout=layout.get();d.vertexShader=v.get();d.pixelShader=p.get();d.renderTargetCount=1;
        d.renderTargetFormat[0]=RenderFormat::R8G8B8A8_UNORM;d.renderTargetBlend[0]=RenderBlendDesc::Copy();d.depthEnabled=d.depthWriteEnabled=true;
        d.depthFunction=RenderComparisonFunction::GREATER_EQUAL;d.depthTargetFormat=RenderFormat::D32_FLOAT;d.cullMode=RenderCullMode::NONE;
        auto base=device->createGraphicsPipeline(d);Require(bool(base),"original translated pipeline");
        gpu::pipeline_cache::Key k{};k.vs=skin?2:1;k.ps=3;k.depthControl=6;k.prim=4;k.rtFormat=uint32_t(RenderFormat::R8G8B8A8_UNORM);k.depthFormat=uint32_t(RenderFormat::D32_FLOAT);
        auto* motionPipeline=replay.PreparePipeline(k,d,vg.data(),uint32_t(vg.size()),pg.data(),uint32_t(pg.size()));
        Require(motionPipeline!=nullptr,"translated replay pipeline: "+replay.LastError());
        DrawHistoryKey key{};key.vsHash=k.vs;key.psHash=k.ps;key.sceneAllocation=1;key.geometrySignature=1;key.indexCount=3;key.primitiveType=4;
        ++token; tracker.BeginFrame(token,token); // new epoch for isolated fixture, then same epoch next frame
        const auto epoch=token;
        shared.flags=alphaReject?1u:0u;shared.alpha[0]=.5f;shared.alpha[1]=4;pixel[3]=alphaReject?.1f:1.f;
        tracker.Collect(key,previous.data(),&shared,skin);tracker.FinalizeFrame();tracker.BeginFrame(++token,epoch);
        const auto match=tracker.Collect(key,current.data(),&shared,skin);Require(match.previous!=nullptr,"real previous snapshot matched");
        const auto mc=MakeMotionReplayConstants(match,W,H,jitterX,jitterY);
        auto rasterConstants=current;
        if (!skin) { rasterConstants[16]+=2*jitterX/W; rasterConstants[17]-=2*jitterY/H; }
        else for(unsigned i=0;i<3;++i) { rasterConstants[(8+i)*4]+=8*jitterX/W; rasterConstants[(8+i)*4+1]-=8*jitterY/H; }
        Write(vsCB.get(),rasterConstants.data(),4096);Write(psCB.get(),pixel.data(),4096);Write(sharedCB.get(),&shared,sizeof(shared));Write(mvCB.get(),&mc,sizeof(mc));
        std::vector<float> z(W*H,.5f-current[4*4+2]);Write(upload.get(),z.data(),z.size()*4);
        cmd->begin();cmd->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(sceneDepth.get(),RenderTextureLayout::COPY_DEST));
        cmd->copyTextureRegion(RenderTextureCopyLocation::Subresource(sceneDepth.get()),RenderTextureCopyLocation::PlacedFootprint(upload.get(),RenderFormat::R32_FLOAT,W,H,1,W));
        cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(sceneDepth.get(),RenderTextureLayout::SHADER_READ));
        cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(color.get(),RenderTextureLayout::COLOR_WRITE));
        cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(depth.get(),RenderTextureLayout::DEPTH_WRITE));
        cmd->setFramebuffer(framebuffer.get());cmd->clearColor(0,RenderColor(0,0,0,0));cmd->clearDepth(true,0);
        RenderViewport viewport(0,0,W,H);RenderRect scissor(0,0,W,H);cmd->setViewports(&viewport,1);cmd->setScissors(&scissor,1);
        cmd->setGraphicsPipelineLayout(layout.get());cmd->setPipeline(base.get());
        uint64_t addresses[]={vsCB->getDeviceAddress(),sharedCB->getDeviceAddress(),psCB->getDeviceAddress()};cmd->setGraphicsPushConstants(0,addresses);
        for(unsigned i=0;i<5;++i)cmd->setGraphicsDescriptorSet(sets[i].get(),i);cmd->drawInstanced(3,1,0,0);
        replay.BeginFrame(token,epoch);Require(replay.BeginScene(cmd.get(),1,depth.get(),W,H),"motion targets/clear");
        RenderBufferReference cb[]={vsCB.get(),sharedCB.get(),psCB.get(),mvCB.get()};RenderDescriptorSet* bindings[5];for(unsigned i=0;i<5;++i)bindings[i]=sets[i].get();
        Require(replay.Draw(cmd.get(),motionPipeline,cb,bindings,5,viewport,scissor,false,3,0),"actual translated previous-position draw");
        if(duplicate)tracker.Collect(key,current.data(),&shared,skin);
        const auto out=replay.Finish(cmd.get(),sceneDepth.get(),tracker.FinalizeFrame());Require(out.ready,"finalized GPU motion and validity");Submit();
        return out;
    }
    void Fill(RenderTexture* target, const std::vector<uint32_t>& data, RenderFormat format) {
        Write(upload.get(),data.data(),data.size()*4);cmd->begin();
        cmd->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(target,RenderTextureLayout::COPY_DEST));
        cmd->copyTextureRegion(RenderTextureCopyLocation::Subresource(target),RenderTextureCopyLocation::PlacedFootprint(upload.get(),format,W,H,1,W));
        cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(target,RenderTextureLayout::SHADER_READ));Submit();
    }
    void TestTaa(MotionFrameView view,float currentZ=.5f) {
        gpu::TemporalAA taa;taa.EnableGpuTiming(true);Require(taa.Init(device.get()),"production TAA initializes for geometric MV");
        auto make=[&](RenderFormat fmt,bool rt=false){return device->createTexture(RenderTextureDesc::Texture2D(W,H,1,fmt,rt?RenderTextureFlag::RENDER_TARGET:RenderTextureFlag::NONE));};
        auto cur=make(RenderFormat::R8G8B8A8_UNORM),prev=make(RenderFormat::R8G8B8A8_UNORM),oldDepth=make(RenderFormat::R32_FLOAT),output=make(RenderFormat::R8G8B8A8_UNORM,true);
        std::vector<uint32_t> colors(W*H),history(W*H),depths(W*H,std::bit_cast<uint32_t>(.5f));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto c=(x%2)?192u:64u;colors[y*W+x]=c|(c<<8)|(c<<16)|(123u<<24);c=32+4*x;history[y*W+x]=c|(c<<8)|(c<<16)|0xff000000u;}
        Fill(cur.get(),colors,RenderFormat::R8G8B8A8_UNORM);Fill(prev.get(),history,RenderFormat::R8G8B8A8_UNORM);Fill(oldDepth.get(),depths,RenderFormat::R32_FLOAT);
        Matrix identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};auto camera=Camera::Create(identity,{0,0,W,H});
        gpu::TemporalAAInputs in;in.currentColor=cur.get();in.historyColor=prev.get();in.currentDepth=sceneDepth.get();in.historyDepth=oldDepth.get();in.output=output.get();
        in.motionVector=view.velocity;in.motionDepths=view.depths;in.reactiveMask=view.reactive;in.motionVectorValid=true;
        in.width=in.historyWidth=W;in.height=in.historyHeight=H;in.currentCamera=in.previousCamera=&*camera;in.historyValid=true;in.rejectAllHistory=false;in.historyWeight=.5f;
        auto run=[&](){cmd->begin();cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(output.get(),RenderTextureLayout::COLOR_WRITE));
            if(!taa.Resolve(cmd.get(),in))throw std::runtime_error(taa.LastError());Submit();taa.ReleaseCompleted();auto data=Read(output.get(),RenderFormat::R8G8B8A8_UNORM,4);uint32_t p;std::memcpy(&p,data.data()+(32*W+32)*4,4);return p;};
        auto px=run();Near(float(px&255),104,"TAA consumes geometric -4 pixel displacement",1);Require((px>>24)==123,"MV TAA retains current alpha");
        in.currentJitterX=.25;in.previousJitterX=-.25;px=run();Near(float(px&255),103,"raw history applies jitter difference exactly once",1);
        in.stableGrid=true;px=run();Near(float(px&255),104,"stable history does not subtract current jitter twice",1);
        in.stableGrid=false;in.currentJitterX=in.previousJitterX=0;
        if(currentZ==.5f){in.motionVectorValid=false;px=run();Near(float(px&255),112,"MV disabled uses unchanged camera-only TAA",1);in.motionVectorValid=true;}
        depths.assign(W*H,std::bit_cast<uint32_t>(.8f));Fill(oldDepth.get(),depths,RenderFormat::R32_FLOAT);
        px=run();Require((px&255)==64,"geometric previous depth rejects occluded history");
        in.historyValid=false;px=run();Require((px&255)==64,"first/cut frame cannot consume history");
    }

    void JitterCycle() {
        gpu::TemporalAA taa;Require(taa.Init(device.get()),"full-cycle consumer initialization");
        auto make=[&](RenderFormat fmt,bool rt=false){return device->createTexture(RenderTextureDesc::Texture2D(W,H,1,fmt,rt?RenderTextureFlag::RENDER_TARGET:RenderTextureFlag::NONE));};
        auto cur=make(RenderFormat::R8G8B8A8_UNORM),prev=make(RenderFormat::R8G8B8A8_UNORM),old=make(RenderFormat::R32_FLOAT),out=make(RenderFormat::R8G8B8A8_UNORM,true);
        std::vector<uint32_t> pixels(W*H),history(W*H),z(W*H,std::bit_cast<uint32_t>(.5f));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned a=x%2?192:64,b=32+4*x;pixels[y*W+x]=a*0x010101u+0xff000000u;history[y*W+x]=b*0x010101u+0xff000000u;}
        Fill(cur.get(),pixels,RenderFormat::R8G8B8A8_UNORM);Fill(prev.get(),history,RenderFormat::R8G8B8A8_UNORM);Fill(old.get(),z,RenderFormat::R32_FLOAT);
        Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};auto camera=Camera::Create(identity,{0,0,W,H});
        for(bool skin:{false,true}) {
            current.fill(0);previous.fill(0);
            if(skin) for(unsigned i=0;i<4;++i){current[64+i]=previous[64+i]=.25f;current[68+i]=previous[68+i]=.75f;}
            for(unsigned phase=0;phase<32;++phase) {
                const auto j=FrameJitter(phase,W,H),p=FrameJitter((phase+31)%32,W,H);
                auto view=Run(skin,false,false,float(j.pixelX),float(j.pixelY));
                auto mv=Read(view.velocity,RenderFormat::R16G16_FLOAT,4),mask=Read(view.reactive,RenderFormat::R8_UNORM,1);
                uint16_t v[2];std::memcpy(v,mv.data()+(32*W+32)*4,4);
                Require(mask[32*W+32]==0&&std::abs(Half(v[0]))<.002f&&std::abs(Half(v[1]))<.002f,"stationary real program has zero geometric MV through jitter cycle");
                gpu::TemporalAAInputs in;in.currentColor=cur.get();in.historyColor=prev.get();in.currentDepth=sceneDepth.get();in.historyDepth=old.get();in.output=out.get();
                in.motionVector=view.velocity;in.motionDepths=view.depths;in.reactiveMask=view.reactive;in.motionVectorValid=true;
                in.width=in.historyWidth=W;in.height=in.historyHeight=H;in.currentCamera=in.previousCamera=&*camera;in.historyValid=true;in.rejectAllHistory=false;in.historyWeight=.5f;
                in.currentJitterX=j.pixelX;in.currentJitterY=j.pixelY;in.previousJitterX=p.pixelX;in.previousJitterY=p.pixelY;
                for(bool stable:{false,true}) {
                    in.stableGrid=stable;cmd->begin();cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(out.get(),RenderTextureLayout::COLOR_WRITE));
                    if(!taa.Resolve(cmd.get(),in))throw std::runtime_error(taa.LastError());Submit();taa.ReleaseCompleted();
                    auto data=Read(out.get(),RenderFormat::R8G8B8A8_UNORM,4);
                    // Ramp = 32+4*x, current center=64, weight=.5. Stable q is the
                    // stable grid itself; raw q also contains previous-current jitter.
                    float expected=112.f+(stable?0.f:2.f*float(p.pixelX-j.pixelX));
                    Near(float(data[(32*W+32)*4]),expected,stable?"stable grid cycle does not drift":"raw grid cycle compensates jitter once",1.1f);
                }
            }
        }
    }
    void HistorySafety(MotionFrameView validView) {
        const Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        auto frame=[&](HistoryOwner& owner,uint64_t number,bool pattern,const MotionFrameView* motion,double jitter=0,bool expect=true) {
            std::vector<uint32_t> pixels(W*H),z(W*H,std::bit_cast<uint32_t>(.5f));
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned a=pattern?(x%2?192:64):128;pixels[y*W+x]=a*0x010101u+0xff000000u;}
            Fill(color.get(),pixels,RenderFormat::R8G8B8A8_UNORM);Fill(sceneDepth.get(),z,RenderFormat::R32_FLOAT);
            SceneObservation scene;scene.Reset(number);SceneAnchor anchor;anchor.depthAllocation=7;anchor.viewport={0,0,W,H};
            for(unsigned i=0;i<16;++i)anchor.vpBits[i]=std::bit_cast<uint32_t>(float(identity[i]));
            scene.ObserveCamera(anchor);scene.ObserveDepth(7,{number,number*2+1,0x1000,24,W,H,true});
            owner.BeginFrame(number,42);cmd->begin();
            cmd->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(sceneDepth.get(),RenderTextureLayout::COPY_SOURCE));
            Require(owner.CaptureDepth(cmd.get(),sceneDepth.get(),scene),"HistoryOwner records frame-qualified depth");
            cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(sceneDepth.get(),RenderTextureLayout::SHADER_READ));
            scene.ObserveColor({number,number*2+2,0x2000,6,W,H,true});
            cmd->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(color.get(),RenderTextureLayout::COPY_SOURCE));
            auto* result=owner.ResolveColor(cmd.get(),color.get(),scene,jitter,0,true,true,false,motion);
            Require(bool(result)==expect,"HistoryOwner resolve success/failure contract");
            auto serial=owner.RecordedSerial();Submit();owner.ReleaseCompletedThrough(serial);
            return result?Read(result,RenderFormat::R8G8B8A8_UNORM,4)[(32*W+32)*4]:uint8_t(0);
        };
        validView.frame=2;validView.epoch=42;validView.depthAllocation=7;
        for(unsigned mode=0;mode<9;++mode) {
            HistoryOwner owner;Require(owner.Init(device.get()),"HistoryOwner safety initialization");owner.EnableGpuTiming(true);
            frame(owner,1,false,nullptr);
            auto view=validView;const MotionFrameView* supplied=&view;
            switch(mode){case 0:supplied=nullptr;break;case 1:view={};break;case 2:++view.frame;break;case 3:++view.epoch;break;case 4:++view.depthAllocation;break;case 5:--view.width;break;case 6:view.reactive=nullptr;break;case 7:view.ready=false;break;default:break;}
            const auto pixel=frame(owner,2,true,supplied);
            const bool reuse=mode==0||mode==8;
            Require(owner.Reused()==reuse,"readiness failure cannot masquerade as camera-history reuse");
            Require(reuse?pixel>100:pixel==64,"invalid geometric frame preserves current color instead of camera history");
            Require(owner.ResolveTiming().samples==2,"owner resolves timestamps at its own fence serial");
        }
        HistoryOwner reset;Require(reset.Init(device.get()),"history reset fixture");
        frame(reset,1,false,nullptr);frame(reset,2,true,nullptr,.75,false);
        frame(reset,3,true,nullptr);Require(reset.Completed()&&!reset.Reused(),"unused invalid previous jitter cannot poison reset frame");
        frame(reset,4,true,nullptr);Require(reset.Reused(),"normal history resumes after reset");
    }
    void TimingLifecycle() {
        GpuPassTimer<2,2> timer;
        cmd->begin();Require(!timer.Begin(device.get(),cmd.get())&&timer.Stats().queryPoolAllocations==0,"disabled GPU timers allocate nothing");Submit();
        timer.Enable(true);
        for(unsigned i=1;i<=2;++i){cmd->begin();Require(timer.Begin(device.get(),cmd.get()),"bounded timer begins interval");timer.End(cmd.get(),i);timer.Seal(cmd.get());Submit();}
        Require(timer.PendingCount()==2&&timer.Stats().samples==0,"timestamps retained until explicit completion callback");
        cmd->begin();Require(!timer.Begin(device.get(),cmd.get()),"in-flight query bound reports unavailable rather than reallocating");Submit();
        timer.ReleaseCompletedThrough(1);Require(timer.PendingCount()==1&&timer.Stats().samples==1,"only completed timestamp prefix is read");
        timer.ReleaseCompletedThrough(2);Require(timer.PendingCount()==0&&timer.Stats().samples==2,"later timestamp remains until its own completion");
        const auto allocations=timer.Stats().queryPoolAllocations;
        for(unsigned i=3;i<20;++i){cmd->begin();Require(timer.Begin(device.get(),cmd.get()),"reused timer interval");timer.End(cmd.get(),i);timer.Seal(cmd.get());Submit();timer.ReleaseCompletedThrough(i);}
        Require(timer.Stats().queryPoolAllocations==allocations&&timer.Stats().samples==19,"native timestamp pools reused after fence without stale samples");
    }

    void Stress(unsigned frames) {
        for(unsigned i=0;i<frames;++i){cmd->begin();replay.BeginFrame(++token,1000);if(!replay.BeginScene(cmd.get(),1,depth.get(),W,H))throw std::runtime_error("stress BeginScene");
            auto view=replay.Finish(cmd.get(),sceneDepth.get(),std::vector<uint32_t>{0,1});if(!view.ready)throw std::runtime_error("stress Finish");Submit();
            if(replay.PendingCount()!=0||replay.BatchCount()>2)throw std::runtime_error("unbounded GPU resources");}
        Require(true,"1200 frame GPU resource lifetime/descriptor reuse remains bounded");
        // Resize with an unreclaimed (but completed) mask batch: old framebuffer
        // must be destroyed before its retired attachments.
        cmd->begin();replay.BeginFrame(++token,1000);replay.BeginScene(cmd.get(),1,depth.get(),W,H);replay.Finish(cmd.get(),sceneDepth.get(),{0,1});
        cmd->end();const RenderCommandList* lists[]={cmd.get()};queue->executeCommandLists(lists,1,nullptr,0,nullptr,0,fence.get());queue->waitForCommandFence(fence.get());
        cmd->begin();replay.BeginFrame(++token,1000);Require(replay.BeginScene(cmd.get(),1,depth.get(),W/2,H/2),"resize after unreclaimed completed batch");
        auto resized=replay.Finish(cmd.get(),sceneDepth.get(),{0,1});Require(resized.ready&&resized.width==W/2,"resized motion contract");Submit();
        Require(replay.PendingCount()==0,"resize releases old descriptor/framebuffer generations safely");
    }
};
int main(int argc, char** argv) {
 try {
    Require(xenos::DxcAvailable(),"pinned DXC available");
    for(bool skin:{false,true}) {
        const auto p=motion_fixture::Vertex(skin),ps=motion_fixture::Pixel();auto h=p.Host(),hp=ps.Host();
        auto t=xenos::TranslateShader(h.data(),uint32_t(h.size()),false);auto tp=xenos::TranslateShader(hp.data(),uint32_t(hp.size()),true);
        for(auto format:{xenos::ShaderBinaryFormat::Dxil,xenos::ShaderBinaryFormat::Spirv})for(bool pixel:{false,true}) {
            auto source=pixel?xenos::motion_replay::Pixel(&tp):xenos::motion_replay::Vertex(t);
            auto c=xenos::CompileHlsl(source,"main",pixel?"ps_6_0":"vs_6_0",format);
            if(!c.ok){std::ofstream("failed-motion.hlsl")<<source;throw std::runtime_error(c.errors);}
            Require(c.ok,"original-program replay DXIL/SPIR-V compile");
        }
    }
    if (argc == 2 && std::string(argv[1]) == "--compile-only") {
        printf("PASS: %u DXIL/SPIR-V compilation checks; no device execution requested\n", checks); return 0;
    }
    Fixture f;f.replay.EnableGpuTiming(true);f.current[16]=.2f;auto view=f.Run();auto data=f.Read(view.velocity,RenderFormat::R16G16_FLOAT,4);auto mask=f.Read(view.reactive,RenderFormat::R8_UNORM,1);
    const auto at=32*64+38;uint16_t xy[2];std::memcpy(xy,data.data()+at*4,4);
    Near(Half(xy[0]),-6.4f,"GPU rigid backward displacement");Near(Half(xy[1]),0,"GPU rigid Y");Require(mask[at]==0,"rigid interior valid");Require(mask[0]==255,"unwritten pixels invalid");
    view=f.Run(false,true);mask=f.Read(view.reactive,RenderFormat::R8_UNORM,1);Require(mask[at]==255,"late duplicate revokes the FIRST recorded GPU draw too");
    view=f.Run(false,false,true);mask=f.Read(view.reactive,RenderFormat::R8_UNORM,1);Require(mask[at]==255,"original PS alpha discard retained");
    f.current.fill(0);f.previous.fill(0);for(int i=0;i<4;++i){f.current[16*4+i]=f.previous[16*4+i]=.25f;f.current[17*4+i]=f.previous[17*4+i]=.75f;}
    f.current[8*4]=.1f;f.current[9*4]=.3f;f.current[10*4]=.2f;
    f.current[20*4]=.2f;f.current[21*4]=.1f;f.current[22*4]=.4f;
    view=f.Run(true);data=f.Read(view.velocity,RenderFormat::R16G16_FLOAT,4);mask=f.Read(view.reactive,RenderFormat::R8_UNORM,1);
    uint16_t left[2],right[2];std::memcpy(left,data.data()+(40*64+27)*4,4);std::memcpy(right,data.data()+(40*64+42)*4,4);
    Require(mask[40*64+27]==0 && mask[40*64+42]==0,"weighted relative-palette geometry has valid interior");
    Require(Half(left[0])<-.1f && Half(right[0])<-.1f && std::abs(Half(left[0])-Half(right[0]))>.1f,"GPU vertex-dependent weighted previous-pose motion, not camera/body-center motion");
    f.current.fill(0);f.previous.fill(0);f.current[16]=.125f;
    view=f.Run(false,false,false,.25f,-.125f);data=f.Read(view.velocity,RenderFormat::R16G16_FLOAT,4);std::memcpy(xy,data.data()+at*4,4);
    Near(Half(xy[0]),-4,"producer removes jitter once from actual rasterized motion");Near(Half(xy[1]),0,"producer unjittered Y");f.TestTaa(view);
    f.current[18]=.2f;view=f.Run();f.TestTaa(view,.3f);
    f.current.fill(0);f.previous.fill(0);view=f.Run();f.HistorySafety(view);
    f.JitterCycle();f.TimingLifecycle();
    Require(f.replay.DrawTiming().samples>0&&f.replay.MaskTiming().samples>0,"production replay draw and validity timestamps completed");
    const auto batches=f.replay.MaskBatchAllocations();
    Require(f.replay.PendingCount()==0,"GPU completion releases all in-flight mask resources");f.Stress(1200);
    Require(f.replay.MaskBatchAllocations()<=batches+2,"1200 frames plus resize do not allocate mask batches linearly");
    printf("PASS: %u motion replay GPU/translation checks\n",checks);return 0;
 } catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
