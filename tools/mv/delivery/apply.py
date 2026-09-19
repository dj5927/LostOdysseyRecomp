"""Apply a bounded, fingerprint-checked source bundle. Temporary delivery helper."""
import base64,hashlib,json,lzma,pathlib,subprocess
root=pathlib.Path.cwd()
packed=base64.b64decode(''.join(p.read_text() for p in sorted((root/'tools/mv/delivery').glob('part-*.b64'))), validate=False)
assert hashlib.sha256(packed).hexdigest()=='0d9298c37ab78868cdb122d883967ca8374c4f294b8a8dc922863c3aaca1243e','Payload fingerprint mismatch'
data=json.loads(lzma.decompress(packed))
assert data['schema']==1 and data['base']=='455b420d3027ac719f09a77d88c0a46878cf965e'
allowed={
'.github/workflows/mv-validation.yml',
'LostOdysseyRecomp/gpu/motion_frame.h','LostOdysseyRecomp/gpu/motion_options.h',
'LostOdysseyRecomp/gpu/motion_replay_gpu.h','LostOdysseyRecomp/gpu/motion_vector.h',
'LostOdysseyRecomp/gpu/motion_vector_gpu.h','LostOdysseyRecomp/gpu/renderer.cpp',
'LostOdysseyRecomp/gpu/shader/motion_replay_hlsl.h','LostOdysseyRecomp/gpu/temporal_aa.cpp',
'LostOdysseyRecomp/gpu/temporal_aa.h','LostOdysseyRecomp/gpu/temporal_history.h',
'docs/notes/motion-vector-implementation.md','docs/notes/motion-vector-milestone-report.md',
'tools/tests/motion_replay/CMakeLists.txt','tools/tests/motion_replay/compile_boundary/stdafx.h',
'tools/tests/motion_replay/platform_stub.cpp','tools/tests/motion_replay_fixture.h',
'tools/tests/motion_replay_gpu_test.cpp','tools/tests/motion_vector_test.cpp'}
assert {e['path'] for e in data['items']}==allowed and len(data['items'])==len(allowed)
prepared=[]
for e in data['items']:
 p=root/e['path']; old=p.read_bytes() if p.exists() else None
 if e['before'] is None: assert old is None, 'Unexpected existing file: '+e['path']
 elif e['before'].startswith('git-blob:'):
  assert subprocess.check_output(['git','hash-object','--',e['path']],text=True).strip()==e['before'][9:], 'Workflow changed'
 else: assert old is not None and hashlib.sha256(old).hexdigest()==e['before'], 'Base changed: '+e['path']
 if e.get('delete'): new=None
 elif 'text' in e: new=e['text'].encode()
 else:
  lines=old.decode().splitlines(True); end=len(lines)
  for i,j,text in reversed(e['edits']):
   assert 0<=i<=j<=end; lines[i:j]=text.splitlines(True); end=i
  new=''.join(lines).encode()
 assert (hashlib.sha256(new).hexdigest() if new is not None else None)==e['after'], 'Output mismatch: '+e['path']
 prepared.append((p,new))
for p,new in prepared:
 if new is None: p.unlink()
 else: p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(new)
(root/'out').mkdir(exist_ok=True)
(root/'out/mv-delivery-paths.json').write_text(json.dumps(sorted(allowed-{'.github/workflows/mv-validation.yml'})))
print('Applied 19 fingerprint-verified files; workflow finalization is connector-owned, not pushed by job token.')
