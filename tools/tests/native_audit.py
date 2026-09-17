"""Run game-data-free CPU fixtures with explicit arguments and bounded timeouts.

No runtime/GPU claim is made. Third-party-dependent fixtures are reported as
skipped when their headers are absent; real DXC integration remains a separate
suite. Tests with custom allocation counters supply matching sized deletes.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = '''prerelease_audit backend_selection host_ui_composite game_path user_paths notified_wait poll_wait
texture_key texture_descriptor_cache texture_layout geometry_prepare depth_clear_layout
register_snapshot frame_pacer render_resolution render_timing render_arena_policy
render_batch_policy resolve_copy_policy temporal_scene temporal_jitter temporal_math
 temporal_history_diagnostic shader_identity shader_preparation_queue cpx_decode
shader_resource_scan shader_resource_variants shader_startup_cache shader_prebuild_io
pipeline_cache position_evidence test_input_pulse installer_controller collection_diagnostics
shader_source_capture shader_source_collection position_evidence_collection summary_collection
taa_binding_collection taa_binding_producer temporal_collection collection_upload_request
scene_aa_provenance xma_loop particle_material_compat'''.split()
DIRECTORY_ARGUMENT = {'shader_preparation_queue','shader_resource_scan','shader_resource_variants',
    'shader_startup_cache','shader_prebuild_io','pipeline_cache','shader_source_capture'}
OPTIONAL = {
    'vertex_cache': ROOT/'thirdparty/unordered_dense/include/ankerl/unordered_dense.h',
    'temporal_collection_nonblocking': ROOT/'thirdparty/plume/plume_render_interface.h',
}

GPU_ONLY = ['polygon_offset','bloom_prefilter']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx',default='clang++')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--only',nargs='+',choices=FIXTURES+list(OPTIONAL)+GPU_ONLY)
    args=parser.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c++20','-pthread','-DLO_TEST_NO_DXC','-I'+str(ROOT/'LostOdysseyRecomp'),
        '-I'+str(ROOT/'thirdparty/unordered_dense/include'),'-I'+str(ROOT/'thirdparty/plume')]
    flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
    env={key:value for key,value in os.environ.items() if not key.startswith('LO_')}
    env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    env.update(HOME=str(out/'home'),XDG_DATA_HOME=str(out/'home/data'),
        XDG_CACHE_HOME=str(out/'home/cache'),XDG_CONFIG_HOME=str(out/'home/config'))
    results=[]
    for name in args.only or FIXTURES+list(OPTIONAL)+GPU_ONLY:
        result={'name':name}
        if name in GPU_ONLY:
            result.update(status='SKIP',reason='requires real DXC and GPU; outside the CPU fixture suite')
        elif name in OPTIONAL and not OPTIONAL[name].is_file():
            result.update(status='SKIP',reason='missing third-party header: '+str(OPTIONAL[name].relative_to(ROOT)))
        else:
            command=[args.cxx,*flags,str(ROOT/f'tools/tests/{name}_test.cpp'),'-o',str(out/name)]
            print('BUILD',name,flush=True)
            try:
                build=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
                (out/(name+'.build.log')).write_text(build.stdout,encoding='utf-8')
                if build.returncode: result.update(status='BUILD_FAIL',returncode=build.returncode)
                else:
                    scratch=out/(name+'-data')
                    arguments=[str(scratch)] if name in DIRECTORY_ARGUMENT else []
                    run=subprocess.run([str(out/name),*arguments],cwd=ROOT,env=env,stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,text=True,timeout=60)
                    (out/(name+'.run.log')).write_text(run.stdout,encoding='utf-8')
                    print(run.stdout,end='',flush=True)
                    result.update(status='PASS' if run.returncode==0 else 'FAIL',returncode=run.returncode)
            except subprocess.TimeoutExpired as error:
                result.update(status='TIMEOUT',reason=str(error))
        print(name,result['status'],flush=True);results.append(result)
        (out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    passed=sum(r['status']=='PASS' for r in results);skipped=sum(r['status']=='SKIP' for r in results)
    print(f'Native fixtures: {passed} passed, {skipped} skipped, {len(results)-passed-skipped} failed',flush=True)
    print('Real DXC, GPU drivers and gameplay were not exercised.',flush=True)
    return 0 if all(r['status'] in {'PASS','SKIP'} for r in results) else 1

if __name__=='__main__':sys.exit(main())
