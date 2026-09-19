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
#include <limits>
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
        vertices=buffer(256,RenderBufferFlag::STORAGE);
        const auto addressableConstant=RenderBufferFlag::CONSTANT|RenderBufferFlag::DEVICE_ADDRESSABLE;
        vsCB=buffer(4096,addressableConstant);psCB=buffer(4096,addressableConstant);
        sharedCB=buffer(1024,addressableConstant);mvCB=buffer(4352,addressableConstant);
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
        auto* motionPipeline=replay.PreparePipeline(k,d,vg.data(),uint32_t(vg.size()),pg.data(),uint32_t(pg.size()),true);
        Require(motionPipeline!=nullptr,"translated replay pipeline: "+replay.LastError());
        DrawHistoryKey key{};key.vsHash=k.vs;key.psHash=k.ps;key.sceneAllocation=1;key.geometrySignature=1;key.indexCount=3;key.primitiveType=4;
        ++token; tracker.BeginFrame(token,token); // new epoch for isolated fixture, then same epoch next frame
        const auto epoch=token;
        shared.flags=alphaReject?1u:0u;shared.alpha[0]=.5f;shared.alpha[1]=4;pixel[3]=alphaReject?.1f:1.f;
        const MotionRasterContract raster{W,H,{0,0,float(W),float(H),0,1},true};
        tracker.Collect(key,previous.data(),&shared,skin,-1,nullptr,&raster);tracker.FinalizeFrame();tracker.BeginFrame(++token,epoch);
        const auto match=tracker.Collect(key,current.data(),&shared,skin,-1,nullptr,&raster);Require(match.previous!=nullptr,"real previous snapshot matched");
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
        in.previousJitterX=0;in.diagnosticAcceptance=true;
        depths.assign(W*H,std::bit_cast<uint32_t>(.5f));depths[32*W+29]=std::bit_cast<uint32_t>(.25f);Fill(oldDepth.get(),depths,RenderFormat::R32_FLOAT);
        px=run();Require(px==0xff0000ffu,"motion silhouette accepts one secondary depth surface");
        depths[32*W+26]=std::bit_cast<uint32_t>(.8f);Fill(oldDepth.get(),depths,RenderFormat::R32_FLOAT);
        px=run();Require(px==0xff000000u,"motion silhouette rejects a third depth surface");
        in.diagnosticRejectionReasons=true;Require(run()==0xffff00ffu,"optional geometric reasons identify third depth surface");in.diagnosticRejectionReasons=false;
        in.diagnosticAcceptance=false;depths.assign(W*H,std::bit_cast<uint32_t>(.5f));Fill(oldDepth.get(),depths,RenderFormat::R32_FLOAT);
        in.previousJitterX=-.25;
        in.stableGrid=true;px=run();Near(float(px&255),104,"stable history does not subtract current jitter twice",1);
        in.stableGrid=false;in.currentJitterX=in.previousJitterX=0;
        if(currentZ==.5f){in.motionVectorValid=false;px=run();Near(float(px&255),112,"MV disabled uses unchanged camera-only TAA",1);in.motionVectorValid=true;}
        depths.assign(W*H,std::bit_cast<uint32_t>(.8f));Fill(oldDepth.get(),depths,RenderFormat::R32_FLOAT);
        px=run();Require((px&255)==64,"geometric previous depth rejects occluded history");
        in.historyValid=false;px=run();Require((px&255)==64,"first/cut frame cannot consume history");
    }

    void ExactStationary() {
        DrawTemporalTracker proof;
        DrawHistoryKey key{}; key.geometrySignature=1;
        MotionRasterContract raster{W,H,{0,0,float(W),float(H),0,1},true};
        current.fill(0);
        proof.BeginFrame(1);proof.Collect(key,current.data(),&shared,false,-1,nullptr,&raster);proof.FinalizeFrame();
        proof.BeginFrame(2);auto match=proof.Collect(key,current.data(),&shared,false,-1,nullptr,&raster);
        Require(MakeMotionReplayConstants(match,W,H,0,0).metadata[3]==1,"proven identical input reaches GPU stationary flag");
        Require(MakeMotionReplayConstants(match,W*2,H,0,0).metadata[3]==0,"output extent mismatch clears GPU stationary flag");
        proof.FinalizeFrame();current[16]=std::bit_cast<float>(1u);
        proof.BeginFrame(3);match=proof.Collect(key,current.data(),&shared,false,-1,nullptr,&raster);
        Require(MakeMotionReplayConstants(match,W,H,0,0).metadata[3]==0,"single position bit change never sets GPU stationary flag");
        proof.FinalizeFrame();
        for(bool skin:{false,true}) {
            current.fill(0);previous.fill(0);
            if(skin) for(unsigned i=0;i<4;++i){current[64+i]=previous[64+i]=.25f;current[68+i]=previous[68+i]=.75f;}
            for(unsigned phase=0;phase<32;++phase) {
                const auto j=FrameJitter(phase,W,H);
                auto view=Run(skin,false,false,float(j.pixelX),float(j.pixelY));
                auto mv=Read(view.velocity,RenderFormat::R16G16_FLOAT,4),mask=Read(view.reactive,RenderFormat::R8_UNORM,1);
                unsigned valid=0; bool exact=true;
                for(unsigned i=0;i<W*H;++i) if(mask[i]==0) {
                    ++valid; uint32_t bits;std::memcpy(&bits,mv.data()+i*4,4);exact &= bits==0;
                }
                Require(valid>0&&exact,"all valid stationary pixels are literal +0 across jitter phases");
                Require(mask[0]==255,"uncovered pixels stay invalid with exact stationary proof");
            }
            auto rejected=Run(skin,false,true,.25f,-.25f);
            auto mask=Read(rejected.reactive,RenderFormat::R8_UNORM,1);
            Require(mask[32*W+32]==255,"alpha-rejected stationary coverage remains invalid");
        }
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
    void StationaryAccumulation() {
        current.fill(0);previous.fill(0);auto view=Run();
        gpu::TemporalAA taa;Require(taa.Init(device.get()),"stationary accumulation GPU initialization");
        auto make=[&](RenderFormat fmt,bool rt=false){return device->createTexture(RenderTextureDesc::Texture2D(W,H,1,fmt,rt?RenderTextureFlag::RENDER_TARGET:RenderTextureFlag::NONE));};
        auto cur=make(RenderFormat::R8G8B8A8_UNORM),prev=make(RenderFormat::R8G8B8A8_UNORM,true),out=make(RenderFormat::R8G8B8A8_UNORM,true),old=make(RenderFormat::R32_FLOAT),moving=make(RenderFormat::R16G16_FLOAT);
        std::vector<uint32_t> pixels(W*H),z(W*H,std::bit_cast<uint32_t>(.5f));
        Fill(old.get(),z,RenderFormat::R32_FLOAT);
        // Binary16 +.125 px X, zero Y: the upper endpoint of the stationary fade.
        Fill(moving.get(),std::vector<uint32_t>(W*H,0x3000u),RenderFormat::R16G16_FLOAT);
        Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};auto camera=Camera::Create(identity,{0,0,W,H});
        gpu::TemporalAAInputs in;in.currentColor=cur.get();in.currentDepth=sceneDepth.get();in.historyDepth=old.get();
        in.motionVector=view.velocity;in.motionDepths=view.depths;in.reactiveMask=view.reactive;in.motionVectorValid=true;
        in.width=in.historyWidth=W;in.height=in.historyHeight=H;in.currentCamera=in.previousCamera=&*camera;
        in.historyValid=true;in.rejectAllHistory=false;in.stableGrid=true;in.historyWeight=.85f;
        auto resolve=[&](){in.historyColor=prev.get();in.output=out.get();cmd->begin();
            cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(out.get(),RenderTextureLayout::COLOR_WRITE));
            if(!taa.Resolve(cmd.get(),in))throw std::runtime_error(taa.LastError());Submit();taa.ReleaseCompleted();
            auto data=Read(out.get(),RenderFormat::R8G8B8A8_UNORM,4);return data[(32*W+32)*4];};
        auto cycle=[&](bool enabled){
            in.stabilizeStationaryGeometry=enabled;Fill(prev.get(),std::vector<uint32_t>(W*H,0x7b808080u),RenderFormat::R8G8B8A8_UNORM);
            float low=255,high=0,sum=0;
            for(unsigned frame=0;frame<128;++frame){
                const auto j=FrameJitter(frame%32,W,H),p=FrameJitter((frame+31)%32,W,H);
                in.currentJitterX=j.pixelX;in.currentJitterY=j.pixelY;in.previousJitterX=p.pixelX;in.previousJitterY=p.pixelY;
                // A subpixel stationary edge changes coverage with the real Halton
                // phase. Neighbor extrema retain both surfaces for RGB clamping.
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned c=((x%2)!=0) != (j.pixelX<0)?192:64;pixels[y*W+x]=c*0x010101u+0x7b000000u;}
                Fill(cur.get(),pixels,RenderFormat::R8G8B8A8_UNORM);float value=resolve();
                if(frame>=96){low=std::min(low,value);high=std::max(high,value);sum+=value;}
                std::swap(prev,out);
            }
            return std::array<float,2>{high-low,sum/32};
        };
        auto baseline=cycle(false),stationary=cycle(true);
        printf("Stationary 32-phase last cycle: default peak/mean %.3f/%.3f, stationary %.3f/%.3f\n",baseline[0],baseline[1],stationary[0],stationary[1]);
        Require(stationary[0]<baseline[0]*.65f,"stationary strategy reduces last-cycle peak-to-peak coverage variation");
        Near(stationary[1],baseline[1],"stationary strategy retains cycle mean",4.f);
        in.currentJitterX=in.currentJitterY=in.previousJitterX=in.previousJitterY=0;
        in.motionVector=moving.get();in.stabilizeStationaryGeometry=false;auto original=resolve();
        in.stabilizeStationaryGeometry=true;Require(resolve()==original,"one-eighth pixel motion restores original history policy");
        Fill(prev.get(),std::vector<uint32_t>(W*H,0x7b808080u),RenderFormat::R8G8B8A8_UNORM);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned c=x%2?192:64;pixels[y*W+x]=c*0x010101u+0x7b000000u;}
        Fill(cur.get(),pixels,RenderFormat::R8G8B8A8_UNORM);
        in.motionVector=view.velocity;in.stationaryCoverage=false;in.stationaryHistoryWeight=.85f;auto lowWeight=resolve();
        in.stationaryHistoryWeight=.95f;Require(resolve()>lowWeight,"runtime stationary weight changes GPU output with coverage disabled");
        in.motionVector=moving.get();auto defaultRange=resolve();in.stationaryMotionMax=.25f;auto widerRange=resolve();
        Require(widerRange>defaultRange,"runtime motion maximum changes GPU output without recompiling");
        in.stationaryMotionMin=.13f;Require(resolve()>widerRange,"runtime motion minimum changes GPU output without recompiling");
        in.stabilizeStationaryGeometry=false;in.stationaryMotionMax=in.stationaryMotionMin;
        Require(!taa.Resolve(cmd.get(),in),"stationary policy validates range even when disabled");
        in.stationaryMotionMin=.002f;in.stationaryMotionMax=16.01f;Require(!taa.Resolve(cmd.get(),in),"stationary policy rejects excessive motion maximum");
        in.stationaryMotionMax=.125f;in.stationaryMotionMin=-.001f;Require(!taa.Resolve(cmd.get(),in),"stationary policy rejects negative motion minimum");
        in.stationaryMotionMin=.002f;in.stationaryHistoryWeight=std::numeric_limits<float>::quiet_NaN();Require(!taa.Resolve(cmd.get(),in),"stationary policy rejects nonfinite weight");
        in.stationaryHistoryWeight=.951f;Require(!taa.Resolve(cmd.get(),in),"stationary policy rejects excessive weight");
        in.stationaryHistoryWeight=31.f/33.f;in.stationaryCoverage=true;
        in.motionVector=view.velocity;in.stableGrid=false;in.stabilizeStationaryGeometry=false;original=resolve();
        in.stabilizeStationaryGeometry=true;Require(resolve()==original,"raw grid retains original accumulation policy");
        in.stableGrid=true;in.motionVectorValid=false;in.stabilizeStationaryGeometry=false;original=resolve();
        in.stabilizeStationaryGeometry=true;Require(resolve()==original,"camera-only retains original accumulation policy");
        in.motionVectorValid=true;z.assign(W*H,std::bit_cast<uint32_t>(.8f));Fill(old.get(),z,RenderFormat::R32_FLOAT);
        Require(resolve()==uint8_t(pixels[32*W+32]),"stationary strategy retains depth rejection");
        auto data=Read(out.get(),RenderFormat::R8G8B8A8_UNORM,4);Require(data[(32*W+32)*4+3]==123,"stationary strategy retains current alpha");
    }
    void StationaryColorClip() {
        // Consumer-only fixture: controlled geometry validity isolates color policy.
        gpu::TemporalAA taa;Require(taa.Init(device.get()),"stationary color clip consumer initialization");
        auto make=[&](RenderFormat fmt,bool rt=false){return device->createTexture(RenderTextureDesc::Texture2D(W,H,1,fmt,rt?RenderTextureFlag::RENDER_TARGET:RenderTextureFlag::NONE));};
        auto cur=make(RenderFormat::R8G8B8A8_UNORM),prev=make(RenderFormat::R8G8B8A8_UNORM),out=make(RenderFormat::R8G8B8A8_UNORM,true);
        auto cd=make(RenderFormat::R32_FLOAT),pd=make(RenderFormat::R32_FLOAT),mv=make(RenderFormat::R16G16_FLOAT),md=make(RenderFormat::R16G16_FLOAT),mask=make(RenderFormat::R32_FLOAT);
        const unsigned center=32*W+32;
        std::vector<uint32_t> colors(W*H,0x7b404040u);
        colors[center-1]=0x7b202020u;colors[center+1]=0x7b606060u;
        Fill(cur.get(),colors,RenderFormat::R8G8B8A8_UNORM);
        Fill(prev.get(),std::vector<uint32_t>(W*H,0xff808080u),RenderFormat::R8G8B8A8_UNORM);
        Fill(cd.get(),std::vector<uint32_t>(W*H,std::bit_cast<uint32_t>(.5f)),RenderFormat::R32_FLOAT);
        Fill(pd.get(),std::vector<uint32_t>(W*H,std::bit_cast<uint32_t>(.5f)),RenderFormat::R32_FLOAT);
        Fill(md.get(),std::vector<uint32_t>(W*H,0x38003800u),RenderFormat::R16G16_FLOAT);
        Fill(mv.get(),std::vector<uint32_t>(W*H,0),RenderFormat::R16G16_FLOAT);
        Fill(mask.get(),std::vector<uint32_t>(W*H,0),RenderFormat::R32_FLOAT);
        Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};auto camera=Camera::Create(identity,{0,0,W,H});
        gpu::TemporalAAInputs in;in.currentColor=cur.get();in.historyColor=prev.get();in.output=out.get();in.currentDepth=cd.get();in.historyDepth=pd.get();
        in.motionVector=mv.get();in.motionDepths=md.get();in.reactiveMask=mask.get();in.motionVectorValid=true;
        in.width=in.historyWidth=W;in.height=in.historyHeight=H;in.currentCamera=in.previousCamera=&*camera;
        in.historyValid=true;in.rejectAllHistory=false;in.stableGrid=true;in.stabilizeStationaryGeometry=true;
        in.historyWeight=in.stationaryHistoryWeight=.9f;in.rejectOutOfNeighborhoodHistory=true;
        auto resolve=[&](){cmd->begin();cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(out.get(),RenderTextureLayout::COLOR_WRITE));
            if(!taa.Resolve(cmd.get(),in))throw std::runtime_error(taa.LastError());Submit();taa.ReleaseCompleted();
            auto data=Read(out.get(),RenderFormat::R8G8B8A8_UNORM,4);uint32_t result;std::memcpy(&result,data.data()+center*4,4);return result;};
        Require(resolve()==0x7b404040u,"stationary color clip off retains hard color rejection");
        in.stationaryColorClip=true;auto result=resolve();
        Near(float(result&255),92.8f,"stationary out-of-range history clips then blends",1.f);
        Require((result>>24)==123,"stationary color clipping retains current alpha");
        in.diagnosticAcceptance=true;result=resolve();
        Require((result&255)==255&&((result>>16)&255)==0&&std::abs(int((result>>8)&255)-128)<=1,"clipped history diagnostic is orange");
        in.diagnosticAcceptance=false;
        Fill(mv.get(),std::vector<uint32_t>(W*H,0x1400u),RenderFormat::R16G16_FLOAT); // 1/1024 pixel
        in.snapStationaryMotion=true;
        Require(resolve()==0x7b404040u,"nonzero original motion retains color rejection even when addressing snaps");
        Fill(mv.get(),std::vector<uint32_t>(W*H,0),RenderFormat::R16G16_FLOAT);
        in.stableGrid=false;Require(resolve()==0x7b404040u,"raw grid retains hard color rejection");in.stableGrid=true;
        in.stabilizeStationaryGeometry=false;Require(resolve()==0x7b404040u,"color clipping requires stationary stabilization");in.stabilizeStationaryGeometry=true;
        Fill(mask.get(),std::vector<uint32_t>(W*H,std::bit_cast<uint32_t>(1.f)),RenderFormat::R32_FLOAT);
        Require(resolve()==0x7b404040u,"color clipping cannot override reactive rejection");
        Fill(mask.get(),std::vector<uint32_t>(W*H,0),RenderFormat::R32_FLOAT);
        Fill(pd.get(),std::vector<uint32_t>(W*H,std::bit_cast<uint32_t>(.8f)),RenderFormat::R32_FLOAT);
        Require(resolve()==0x7b404040u,"color clipping cannot override history depth rejection");
        Fill(pd.get(),std::vector<uint32_t>(W*H,std::bit_cast<uint32_t>(.5f)),RenderFormat::R32_FLOAT);
        Fill(cur.get(),std::vector<uint32_t>(W*H,0x7b000000u),RenderFormat::R8G8B8A8_UNORM);
        Require(resolve()==0x7b000000u,"uniform black change immediately clamps old history to black");
    }
    void StationarySilhouette() {
        // Deterministic CPU-uploaded two-surface inputs exercise the production GPU
        // consumer. This fixture makes no claim to test geometry/MV production.
        gpu::TemporalAA taa;Require(taa.Init(device.get()),"stationary silhouette consumer initialization");
        auto make=[&](RenderFormat fmt,bool rt=false){return device->createTexture(RenderTextureDesc::Texture2D(W,H,1,fmt,rt?RenderTextureFlag::RENDER_TARGET:RenderTextureFlag::NONE));};
        auto cur=make(RenderFormat::R8G8B8A8_UNORM),prev=make(RenderFormat::R8G8B8A8_UNORM,true),out=make(RenderFormat::R8G8B8A8_UNORM,true);
        auto cd=make(RenderFormat::R32_FLOAT),pd=make(RenderFormat::R32_FLOAT),mv=make(RenderFormat::R16G16_FLOAT),md=make(RenderFormat::R16G16_FLOAT),mask=make(RenderFormat::R32_FLOAT);
        Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};auto camera=Camera::Create(identity,{0,0,W,H});
        gpu::TemporalAAInputs in;in.currentColor=cur.get();in.currentDepth=cd.get();in.historyDepth=pd.get();
        in.motionVector=mv.get();in.motionDepths=md.get();in.reactiveMask=mask.get();in.motionVectorValid=true;
        in.width=in.historyWidth=W;in.height=in.historyHeight=H;in.currentCamera=in.previousCamera=&*camera;
        in.historyValid=true;in.rejectAllHistory=false;in.stableGrid=true;in.historyWeight=31.f/33.f;
        const unsigned center=32*W+32,neighbor=32*W+33;
        std::vector<uint32_t> colors(W*H),depths(W*H),oldDepths(W*H),motion(W*H),replayDepths(W*H),reactive(W*H);
        auto inputs=[&](double j,double previousJ){
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned i=y*W+x;
                bool currentNear=x+.5-j>=32.5,previousNear=x+.5-previousJ>=32.5;
                unsigned color=currentNear?192:64,half=currentNear?0x3800:0x3400;
                colors[i]=color*0x010101u+0x7b000000u;depths[i]=std::bit_cast<uint32_t>(currentNear?.5f:.25f);
                oldDepths[i]=std::bit_cast<uint32_t>(previousNear?.5f:.25f);replayDepths[i]=half|(half<<16);motion[i]=reactive[i]=0;
            }
            in.currentJitterX=j;in.previousJitterX=previousJ;
        };
        auto uploadInputs=[&](){Fill(cur.get(),colors,RenderFormat::R8G8B8A8_UNORM);Fill(cd.get(),depths,RenderFormat::R32_FLOAT);
            Fill(pd.get(),oldDepths,RenderFormat::R32_FLOAT);Fill(mv.get(),motion,RenderFormat::R16G16_FLOAT);
            Fill(md.get(),replayDepths,RenderFormat::R16G16_FLOAT);Fill(mask.get(),reactive,RenderFormat::R32_FLOAT);};
        auto resolve=[&](){in.historyColor=prev.get();in.output=out.get();cmd->begin();
            cmd->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(out.get(),RenderTextureLayout::COLOR_WRITE));
            if(!taa.Resolve(cmd.get(),in))throw std::runtime_error(taa.LastError());Submit();taa.ReleaseCompleted();
            auto data=Read(out.get(),RenderFormat::R8G8B8A8_UNORM,4);uint32_t result;std::memcpy(&result,data.data()+center*4,4);return result;};
        Fill(prev.get(),std::vector<uint32_t>(W*H,0xff808080u),RenderFormat::R8G8B8A8_UNORM);
        // Experimental addressing snap: a tiny nonzero cubic lobe reaches a third
        // depth layer even though the center history sample matches its surface.
        inputs(-.25,-.25);motion[center]=0x1400u; // binary16 1/1024 pixel
        oldDepths[neighbor]=std::bit_cast<uint32_t>(.375f);uploadInputs();
        in.diagnosticAcceptance=in.diagnosticRejectionReasons=true;in.stabilizeStationaryGeometry=true;
        Require(resolve()==0xffff00ffu,"tiny unsnapped history motion reaches third depth layer");
        in.snapStationaryMotion=true;Require(resolve()==0xff0000ffu,"experimental snap uses matching center history depth");
        motion[center]=0x1c00u;uploadInputs(); // binary16 1/256 pixel, above default min
        Require(resolve()==0xffff00ffu,"experimental snap retains third-layer rejection above motion threshold");
        motion[center]=0x1400u;uploadInputs();in.stabilizeStationaryGeometry=false;
        Require(resolve()==0xffff00ffu,"experimental snap requires stationary stabilization");
        in.stabilizeStationaryGeometry=true;in.stableGrid=false;
        Require(resolve()==0xffff00ffu,"experimental snap does not affect raw history addressing");
        in.stableGrid=true;in.snapStationaryMotion=false;in.diagnosticRejectionReasons=false;
        inputs(.25,-.25);uploadInputs();
        in.diagnosticAcceptance=true;in.stabilizeStationaryGeometry=false;
        Require(resolve()==0xff000000u,"original consumer rejects static center ownership exchange");
        in.stabilizeStationaryGeometry=true;Require(resolve()==0xff0000ffu,"stationary two-surface support accepts ownership exchange at identical weight");
        in.stationaryCoverage=false;Require(resolve()==0xff000000u,"runtime coverage switch independently disables surface support");
        in.stationaryCoverage=true;Require(resolve()==0xff0000ffu,"runtime coverage switch restores support without recompiling");
        in.diagnosticRejectionReasons=true;
        reactive[center]=std::bit_cast<uint32_t>(1.f);uploadInputs();Require(resolve()==0xffff0000u,"optional geometric reasons identify reactive rejection");
        inputs(.25,-.25);replayDepths[center]=0x38003800u;uploadInputs();Require(resolve()==0xff00ffffu,"optional geometric reasons identify current/replay depth mismatch");
        inputs(.25,-.25);replayDepths[center]=0;uploadInputs();Require(resolve()==0xffffff00u,"optional geometric reasons identify other invalid inputs");
        in.diagnosticRejectionReasons=false;
        for(unsigned mode=0;mode<8;++mode){
            inputs(.25,-.25);in.stableGrid=true;
            switch(mode){
            case 0:reactive[neighbor]=std::bit_cast<uint32_t>(1.f);break;
            case 1:motion[neighbor]=0x211fu;break; // binary16 approximately .01 px
            case 2:replayDepths[neighbor]=0x34003800u;break; // current .5, previous .25
            case 3:oldDepths.assign(W*H,std::bit_cast<uint32_t>(.5f));break;
            case 4:oldDepths[neighbor]=std::bit_cast<uint32_t>(.375f);break;
            case 5:depths[neighbor]=std::bit_cast<uint32_t>(.375f);replayDepths[neighbor]=0x36003600u;break;
            case 6:in.stableGrid=false;in.currentJitterX=in.previousJitterX=0;break;
            case 7:replayDepths[neighbor]=0x34003400u;break; // replay/current depth mismatch
            }
            uploadInputs();Require(resolve()==0xff000000u,"stationary support rejects unsafe fixture "+std::to_string(mode));
        }
        in.stableGrid=true;in.diagnosticAcceptance=false;
        auto cycle=[&](bool enabled){
            in.stabilizeStationaryGeometry=enabled;Fill(prev.get(),std::vector<uint32_t>(W*H,0x7b808080u),RenderFormat::R8G8B8A8_UNORM);
            float low=255,high=0,sum=0;
            for(unsigned frame=0;frame<128;++frame){auto j=FrameJitter(frame%32,W,H),p=FrameJitter((frame+31)%32,W,H);
                inputs(j.pixelX,p.pixelX);in.currentJitterY=j.pixelY;in.previousJitterY=p.pixelY;uploadInputs();
                auto pixel=resolve();float value=float(pixel&255);
                if(frame>=96){low=std::min(low,value);high=std::max(high,value);sum+=value;}
                if(frame==127)Require((pixel>>24)==123,"stationary silhouette retains current alpha");
                std::swap(prev,out);
            }
            return std::array<float,2>{high-low,sum/32};
        };
        auto baseline=cycle(false),coverage=cycle(true);
        printf("Two-surface 32-phase consumer at equal 31/33 weight: original peak/mean %.3f/%.3f, coverage %.3f/%.3f\n",baseline[0],baseline[1],coverage[0],coverage[1]);
        Require(coverage[0]<baseline[0]*.35f,"two-surface support reduces ownership flicker independently of weight");
        Near(coverage[1],baseline[1],"two-surface support preserves cycle coverage mean",6.f);
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
    for(auto format:{xenos::ShaderBinaryFormat::Dxil,xenos::ShaderBinaryFormat::Spirv}) {
        const auto source=xenos::motion_replay::Pixel(nullptr);
        const auto c=xenos::CompileHlsl(source,"main","ps_6_0",format);
        if(!c.ok){std::ofstream("failed-motion.hlsl")<<source;throw std::runtime_error(c.errors);}
        Require(c.ok,"depth-only replay PS DXIL/SPIR-V compile");
    }
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
    Fixture f;
    if(argc>1&&std::string(argv[1])=="--stationary-color-only"){f.StationaryColorClip();printf("PASS: %u stationary color GPU checks\n",checks);return 0;}
    if(argc>1&&std::string(argv[1])=="--exact-stationary-only"){f.ExactStationary();printf("PASS: %u exact stationary GPU checks\n",checks);return 0;}
    f.replay.EnableGpuTiming(true);f.current[16]=.2f;auto view=f.Run();auto data=f.Read(view.velocity,RenderFormat::R16G16_FLOAT,4);auto mask=f.Read(view.reactive,RenderFormat::R8_UNORM,1);
    const auto at=32*64+38;uint16_t xy[2];std::memcpy(xy,data.data()+at*4,4);
    Near(Half(xy[0]),-6.4f,"GPU rigid backward displacement");Near(Half(xy[1]),0,"GPU rigid Y");Require(mask[at]==0,"rigid interior valid");Require(mask[0]==255,"unwritten pixels invalid");
    view=f.Run(false,true);mask=f.Read(view.reactive,RenderFormat::R8_UNORM,1);Require(mask[at]==0,"extra ordered instance does not revoke the first recorded GPU draw");
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
    f.ExactStationary();f.JitterCycle();f.StationaryAccumulation();f.StationaryColorClip();f.StationarySilhouette();f.TimingLifecycle();
    Require(f.replay.DrawTiming().samples>0&&f.replay.MaskTiming().samples>0,"production replay draw and validity timestamps completed");
    const auto batches=f.replay.MaskBatchAllocations();
    Require(f.replay.PendingCount()==0,"GPU completion releases all in-flight mask resources");f.Stress(1200);
    Require(f.replay.MaskBatchAllocations()<=batches+2,"1200 frames plus resize do not allocate mask batches linearly");
    printf("PASS: %u motion replay GPU/translation checks\n",checks);return 0;
 } catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
