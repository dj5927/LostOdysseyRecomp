import hashlib,json,os,time,traceback
from pathlib import Path
import renderdoc as rd
job=json.loads(Path(os.environ['LO_RENDERDOC_JOB']).read_text(encoding='utf-8-sig'))
out=Path(job['output']);out.mkdir(parents=True,exist_ok=True)
def save(name,data): (out/name).write_text(json.dumps(data,indent=2),encoding='utf-8')
cap=None;ctrl=None;original=None;replacement=None
try:
    cap=rd.OpenCaptureFile(); r=cap.OpenFile(job['capture'],'',None)
    if r!=rd.ResultCode.Succeeded: raise RuntimeError(str(r))
    r,ctrl=cap.OpenCapture(rd.ReplayOptions(),None)
    if r!=rd.ResultCode.Succeeded: raise RuntimeError(str(r))
    stage=getattr(rd.ShaderStage,job.get('shaderStage','Pixel'))
    ctrl.SetFrameEvent(job['shaderEventId'],False)
    original=ctrl.GetPipelineState().GetShader(stage)
    reflection=ctrl.GetPipelineState().GetShaderReflection(stage)
    if reflection is None: raise RuntimeError('No shader reflection at selected event/stage')
    raw=bytes(reflection.rawBytes)
    if hashlib.sha256(raw).hexdigest()!=job['originalSha256'].lower(): raise RuntimeError('Unexpected original shader')
    replacement_bytes=Path(job['replacement']).read_bytes()
    if job.get('replacementSha256') and hashlib.sha256(replacement_bytes).hexdigest()!=job['replacementSha256'].lower():
        raise RuntimeError('Unexpected replacement shader')
    replacement,errors=ctrl.BuildTargetShader(job.get('entry','main'),rd.ShaderEncoding.SPIRV,replacement_bytes,rd.ShaderCompileFlags(),stage)
    save('replacement.json',{'original':str(original),'replacement':str(replacement),'errors':errors})
    if replacement==rd.ResourceId.Null(): raise RuntimeError(errors)
    records=[]
    for label,patched in [('original-a',False),('probe-a',True),('probe-b',True),('original-b',False)]:
        if patched: ctrl.ReplaceResource(original,replacement)
        else: ctrl.RemoveReplacement(original)
        started=time.time()
        counters=ctrl.FetchCounters([rd.GPUCounter.EventGPUDuration])
        ended=time.time()
        ms={str(c.eventId):c.value.d*1000 for c in counters}
        missing=[e for e in job['hotspotEventIds'] if str(e) not in ms]
        if missing: raise RuntimeError('No GPU counter for selected events: '+str(missing))
        records.append({'label':label,'started_unix':started,'finished_unix':ended,'gpu_ms':ms,
            'hotspot_ms':sum(ms[str(e)] for e in job['hotspotEventIds']),'sum_event_ms':sum(ms.values())})
        save('ab-counters.json',records)
        if label in ['original-a','probe-a']:
            ctrl.SetFrameEvent(job['imageEventId'],True)
            pipe=ctrl.GetPipelineState()
            target=pipe.GetOutputTargets()[job.get('outputTargetIndex',0)].resource
            if target==rd.ResourceId.Null(): raise RuntimeError('Selected output target is unbound')
            tex=rd.TextureSave();tex.resourceId=target;tex.destType=rd.FileType.PNG;tex.mip=0;tex.slice.sliceIndex=0
            r=ctrl.SaveTexture(tex,str(out/(label+'.png')))
            if r!=rd.ResultCode.Succeeded: raise RuntimeError(str(r))
    hashes={label:hashlib.sha256((out/(label+'.png')).read_bytes()).hexdigest() for label in ['original-a','probe-a']}
    save('complete.json',{'complete':True,'image_sha256':hashes,'images_byte_identical':len(set(hashes.values()))==1,
        'scope':'same captured frame, one replacement shader, ABBA offline counters; not live FPS or power validation'})
except Exception: save('error.json',{'traceback':traceback.format_exc()})
finally:
    if ctrl:
        if original: ctrl.RemoveReplacement(original)
        if replacement and replacement!=rd.ResourceId.Null():ctrl.FreeTargetResource(replacement)
        ctrl.Shutdown()
    if cap:cap.Shutdown()
    raise SystemExit(0)
