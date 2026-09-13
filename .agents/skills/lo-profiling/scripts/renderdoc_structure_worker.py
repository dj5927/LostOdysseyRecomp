"""Read capture structure and usage only; no counters, replacements or SetFrameEvent."""
import json, os, traceback, time
from pathlib import Path
import renderdoc as rd
job=json.loads(Path(os.environ['LO_RENDERDOC_JOB']).read_text(encoding='utf-8-sig'))
out=Path(job['output']);out.mkdir(parents=True,exist_ok=True)
def save(name,obj): (out/name).write_text(json.dumps(obj,indent=2),encoding='utf-8')
def scalar(obj):
    kind=obj.type.basetype
    try:
        if kind==rd.SDBasic.String:return obj.data.str
        if kind==rd.SDBasic.Boolean:return obj.data.basic.b
        if kind==rd.SDBasic.Float:return obj.data.basic.d
        if kind==rd.SDBasic.SignedInteger:return obj.data.basic.i
        if kind in (rd.SDBasic.UnsignedInteger,rd.SDBasic.Enum):return obj.data.basic.u
        if kind==rd.SDBasic.Resource:return str(obj.data.basic.id)
        if kind==rd.SDBasic.Null:return None
    except Exception as e:
        return {'error':repr(e),'value':str(obj),'data_members':dir(obj.data),'object_members':dir(obj)}
    return str(obj)
def serialize(obj,depth=0):
    result={'name':str(obj.name),'type':str(obj.type.name),'basetype':str(obj.type.basetype)}
    if depth>16:return dict(result,truncated=True)
    n=obj.NumChildren()
    if n:result['children']=[serialize(obj.GetChild(i),depth+1) for i in range(n)]
    else:result['value']=scalar(obj)
    return result
cap=None;ctrl=None
try:
    cap=rd.OpenCaptureFile();result=cap.OpenFile(job['capture'],'',None)
    if result!=rd.ResultCode.Succeeded:raise RuntimeError(str(result))
    result,ctrl=cap.OpenCapture(rd.ReplayOptions(),None)
    if result!=rd.ResultCode.Succeeded:raise RuntimeError(str(result))
    sf=ctrl.GetStructuredFile();actions=[]
    def walk(seq):
        for a in seq:actions.append(a);walk(a.children)
    walk(ctrl.GetRootActions())
    low,high=job['event_window'];event_map={};near=[]
    for a in actions:
        if low<=a.eventId<=high:
            near.append({'eventId':a.eventId,'name':a.GetName(sf),'flags':str(a.flags),'copySource':str(a.copySource),'copyDestination':str(a.copyDestination)})
        for ev in a.events:
            if low<=ev.eventId<=high:
                event_map[ev.eventId]={'eventId':ev.eventId,'chunkIndex':ev.chunkIndex,'fileOffset':ev.fileOffset}
    save('nearby-actions.json',near)
    commands=[]
    for eid,ev in sorted(event_map.items()):
        commands.append(dict(ev,chunk=serialize(sf.chunks[ev['chunkIndex']])))
    save('api-commands.json',commands)
    selected={str(t.resourceId):t for t in ctrl.GetTextures() if str(t.resourceId) in job.get('resources',[])}
    usages={}
    for rid,t in selected.items():
        usages[rid]={'width':t.width,'height':t.height,'format':t.format.Name(),'usage':[{'eventId':u.eventId,'usage':str(u.usage)} for u in ctrl.GetUsage(t.resourceId)]}
    save('resource-usage.json',usages)
    requested=job.get('events',[])
    save('complete.json',{'complete':True,'api_event_count':len(commands),'requested_events':requested,'found_requested_events':[eid for eid in requested if eid in event_map],'scope':'Read structured API parameters and resource usage. No FetchCounters, SetFrameEvent or resource replacement. Capture opened once.','time':time.time()})
except Exception:
    save('error.json',{'traceback':traceback.format_exc()})
finally:
    if ctrl:ctrl.Shutdown()
    if cap:cap.Shutdown()
    raise SystemExit(0)
