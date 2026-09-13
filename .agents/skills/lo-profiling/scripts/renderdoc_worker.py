"""Run with portable qrenderdoc --python; never import from unrelated Python."""
import csv
import hashlib
import json
import os
import re
import struct
import time
import traceback
from pathlib import Path
import renderdoc as rd

job_path = Path(os.environ["LO_RENDERDOC_JOB"])
job = json.loads(job_path.read_text(encoding="utf-8-sig"))
output = Path(job["output"])
output.mkdir(parents=True, exist_ok=True)

def save(name, value):
    (output / name).write_text(json.dumps(value, indent=2), encoding="utf-8")

def connected(ident):
    target = rd.CreateTargetControl("", int(ident), "LostOdyssey GPU audit", False)
    if target is None or not target.Connected():
        raise RuntimeError("RenderDoc target control is unavailable or already owned")
    return target

def run():
    mode = job["mode"]
    if mode == "probe":
        save("api-probe.json", {"version": rd.GetVersionString(),
            "ExecuteAndInject": rd.ExecuteAndInject.__doc__,
            "CreateTargetControl": rd.CreateTargetControl.__doc__,
            "TriggerCapture": rd.TargetControl.TriggerCapture.__doc__,
            "python": __import__("sys").version,
            "actions": [x for x in ("Drawcall", "Dispatch", "Copy", "Resolve", "Clear", "Blit") if hasattr(rd.ActionFlags, x)]})
        return
    if mode == "launch":
        for name, value in job.get("environment", {}).items():
            os.environ[str(name)] = str(value)
        result = rd.ExecuteAndInject(job["executable"], job["workingDirectory"],
            job.get("commandLine", ""), [], str(output / "audit"), rd.CaptureOptions(), False)
        save("launch-result.json", {"result": str(result.result), "ident": result.ident,
            "executable": job["executable"], "workingDirectory": job["workingDirectory"]})
        if result.result != rd.ResultCode.Succeeded:
            raise RuntimeError(str(result.result))
        return
    if mode == "status":
        target = connected(job["ident"])
        records = []
        try:
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and target.Connected():
                msg = target.ReceiveMessage(None)
                if msg.type == rd.TargetControlMessageType.RegisterAPI:
                    records.append({"type": "RegisterAPI", "name": msg.apiUse.name,
                        "presenting": msg.apiUse.presenting, "supported": msg.apiUse.supported,
                        "supportMessage": msg.apiUse.supportMessage})
                elif msg.type == rd.TargetControlMessageType.CapturableWindowCount:
                    records.append({"type": "CapturableWindowCount", "count": msg.capturableWindowCount})
                time.sleep(0.02)
            save("target-status.json", {"ident": job["ident"], "connected": target.Connected(),
                "scope": "Read-only target messages; no capture triggered", "messages": records})
            return
        finally:
            target.Shutdown()
    if mode == "capture":
        # Connecting replays NewCapture for captures already owned by the target.
        # They must be consumed before triggering, not accepted as this request.
        def path_key(path):
            return os.path.normcase(os.path.abspath(path))
        prior_paths = {path_key(path) for path in output.glob("*.rdc")}
        prior_ids = set()
        prior_captures = []
        ignored_captures = []
        target = connected(job["ident"])
        try:
            drain_deadline = time.monotonic() + 3
            while time.monotonic() < drain_deadline and target.Connected():
                msg = target.ReceiveMessage(None)
                if msg.type == rd.TargetControlMessageType.NewCapture:
                    old = msg.newCapture
                    prior_ids.add(old.captureId)
                    prior_paths.add(path_key(old.path))
                    prior_captures.append({"captureId": old.captureId, "path": old.path,
                        "frameNumber": old.frameNumber, "timestamp": old.timestamp})
                time.sleep(0.02)
            if not target.Connected():
                raise RuntimeError("Target disconnected while enumerating existing captures")
            triggered_at = time.time()
            save("capture-request.json", {"ident": job["ident"], "triggered_at_unix": triggered_at,
                "existing_captures": prior_captures, "existing_paths": sorted(prior_paths)})
            target.TriggerCapture(1)
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline and target.Connected():
                msg = target.ReceiveMessage(None)
                if msg.type == rd.TargetControlMessageType.NewCapture:
                    capture = msg.newCapture
                    # Timestamp has one-second precision; file/id baselines also
                    # reject captures made earlier in the same second.
                    stale = capture.captureId in prior_ids or path_key(capture.path) in prior_paths
                    stale |= bool(capture.timestamp and capture.timestamp < int(triggered_at))
                    if stale:
                        ignored_captures.append({"captureId": capture.captureId, "path": capture.path,
                            "timestamp": capture.timestamp, "reason": "predates_request"})
                        continue
                    if capture.local:
                        file = Path(capture.path)
                        if not file.is_file() or file.stat().st_size != capture.byteSize:
                            raise RuntimeError("New capture notification disagrees with local file size")
                    save("capture-result.json", {"path": capture.path,
                        "captureId": capture.captureId, "ident": job["ident"],
                        "frameNumber": capture.frameNumber, "timestamp": capture.timestamp,
                        "byteSize": capture.byteSize, "triggered_at_unix": triggered_at,
                        "existing_capture_ids": sorted(prior_ids), "ignored_captures": ignored_captures})
                    return
                time.sleep(0.02)
            raise RuntimeError("No completed capture within 90 seconds")
        finally:
            target.Shutdown()
    if mode != "analyse":
        raise ValueError("Unknown mode: " + mode)
    cap = rd.OpenCaptureFile()
    controller = None
    try:
        result = cap.OpenFile(job["capture"], "", None)
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError(str(result))
        result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError(str(result))
        available = controller.EnumerateCounters()
        if rd.GPUCounter.EventGPUDuration not in available:
            raise RuntimeError("EventGPUDuration counter unavailable")
        # Counter values are seconds. These measure offline replay, not undisturbed live frame time.
        counters = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
        durations = {x.eventId: x.value.d * 1000.0 for x in counters}
        textures = {str(x.resourceId): x for x in controller.GetTextures()}
        resources = {str(x.resourceId): x.name for x in controller.GetResources()}
        cache_index = {}
        cache_report = {"directory": job.get("cacheDirectory"), "valid_headers": 0,
            "rejected_headers": 0, "scope": "144-byte LOSHDR1 headers; payloads not read or rehashed"}
        if job.get("cacheDirectory"):
            for file in Path(job["cacheDirectory"]).rglob("*.spv"):
                match = re.fullmatch(r"(ps|vs)_([0-9a-fA-F]{16})(?:_.*)?", file.stem)
                if not match:
                    continue
                try:
                    with file.open("rb") as stream:
                        header = stream.read(144)
                    if len(header) != 144 or header[:7] != b"LOSHDR1":
                        raise ValueError("Unrecognised shader cache header")
                    digest = header[72:136].decode("ascii").lower()
                    if not re.fullmatch(r"[0-9a-f]{64}", digest):
                        raise ValueError("Invalid payload digest")
                    size = struct.unpack_from("<Q", header, 136)[0]
                    if not size or file.stat().st_size != size + 144:
                        raise ValueError("Payload length disagrees with file size")
                    cache_index.setdefault((digest, size), []).append({"stage": match[1],
                        "guest_hash": match[2].lower(), "cache_file": str(file)})
                    cache_report["valid_headers"] += 1
                except (OSError, UnicodeError, ValueError, struct.error):
                    cache_report["rejected_headers"] += 1
        shader_map = {}
        def shader(pipe, stage):
            rid = str(pipe.GetShader(stage))
            if rid not in shader_map:
                reflection = pipe.GetShaderReflection(stage)
                raw = bytes(reflection.rawBytes) if reflection else b""
                digest = hashlib.sha256(raw).hexdigest() if raw else None
                shader_map[rid] = {"resourceId": rid, "encoding": str(reflection.encoding) if reflection else None,
                    "raw_bytes": len(raw), "raw_sha256": digest,
                    "cache_matches": cache_index.get((digest, len(raw)), []) if digest else [],
                    "match_scope": "Captured raw shader SHA256 and size matched to cache header payload digest and size"}
            return rid
        def resource(rid):
            key = str(rid)
            tex = textures.get(key)
            return {"resourceId": key, "name": resources.get(key, ""),
                "width": tex.width if tex else None, "height": tex.height if tex else None,
                "format": tex.format.Name() if tex else None}
        rows = []
        def walk(actions, parent_path):
            for action in actions:
                label = action.customName or str(action.eventId)
                path = parent_path + [label]
                if action.eventId in durations:
                    flags = action.flags
                    category = "other"
                    for name, value in (("draw", "Drawcall"), ("dispatch", "Dispatch"),
                        ("clear", "Clear"), ("blit", "Blit"), ("resolve", "Resolve"), ("copy", "Copy")):
                        if hasattr(rd.ActionFlags, value) and flags & getattr(rd.ActionFlags, value):
                            category = name
                            break
                    # Vulkan blits may have Copy rather than a distinct action flag.
                    event_name = action.GetName(controller.GetStructuredFile())
                    if "blit" in event_name.lower():
                        category = "blit"
                    controller.SetFrameEvent(action.eventId, False)
                    pipe = controller.GetPipelineState()
                    rows.append({"eventId": action.eventId, "name": event_name,
                        "path": path, "category": category, "gpu_ms": durations[action.eventId],
                        "flags": str(flags), "numIndices": action.numIndices,
                        "numInstances": action.numInstances,
                        "vs": shader(pipe, rd.ShaderStage.Vertex),
                        "ps": shader(pipe, rd.ShaderStage.Fragment),
                        "renderTargets": [resource(x.resource) for x in pipe.GetOutputTargets()],
                        "depthTarget": resource(pipe.GetDepthTarget().resource),
                        "copySource": resource(action.copySource),
                        "copyDestination": resource(action.copyDestination)})
                walk(action.children, path)
        walk(controller.GetRootActions(), [])
        rows.sort(key=lambda x: x["gpu_ms"], reverse=True)
        by_category, by_ps = {}, {}
        for row in rows:
            by_category[row["category"]] = by_category.get(row["category"], 0) + row["gpu_ms"]
            if row["category"] == "draw":
                by_ps[row["ps"]] = by_ps.get(row["ps"], 0) + row["gpu_ms"]
        save("gpu-events.json", rows)
        save("shader-map.json", {"cache": cache_report, "shaders": shader_map})
        save("gpu-summary.json", {"capture": job["capture"], "scope": "offline replay per-event GPU counter",
            "warning": "Capture/replay perturbs performance; resource IDs are capture-local, not guest shader hashes.",
            "events": len(rows), "sum_event_gpu_ms": sum(x["gpu_ms"] for x in rows),
            "by_category_ms": by_category, "by_ps_ms": by_ps, "top_events": rows[:30]})
        with (output / "gpu-events.csv").open("w", newline="", encoding="utf-8-sig") as f:
            keys = ["eventId", "name", "category", "gpu_ms", "vs", "ps", "numIndices", "numInstances"]
            writer = csv.DictWriter(f, fieldnames=keys)
            writer.writeheader()
            writer.writerows({k: row[k] for k in keys} for row in rows)
    finally:
        if controller is not None:
            controller.Shutdown()
        cap.Shutdown()

try:
    run()
except Exception:
    save("error.json", {"mode": job.get("mode"), "traceback": traceback.format_exc()})
finally:
    # qrenderdoc executes --python before showing its main window; SystemExit skips the UI.
    raise SystemExit(0)
