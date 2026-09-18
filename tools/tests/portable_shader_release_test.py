#!/usr/bin/env python3
"""Exercise the real pack CLI, runtime contract, and release staging (synthetic data)."""
from __future__ import annotations
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]

def module(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

def require(ok: bool, message: str):
    if not ok:
        raise AssertionError(message)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    args = parser.parse_args()
    tool, generator = args.tool.resolve(), args.fixtures.resolve()
    fetch = module('lo_test_fetch_pack', 'tools/release/fetch_shader_pack.py')
    stage = module('lo_test_stage_pack', 'tools/portable_shader_pack_payload.py')
    with tempfile.TemporaryDirectory(prefix='lo-release-contract-') as temporary:
        root = Path(temporary)
        subprocess.run([str(generator), str(root)], check=True, timeout=30)
        image, matching, explicit = root/'image.bin', root/'matching.lospv', root/'explicit.lospv'
        def command(*arguments, success=True):
            result = subprocess.run([str(tool), *map(str, arguments)], capture_output=True, text=True, timeout=30)
            require((result.returncode == 0) == success, f'CLI {arguments}: {result.stdout} {result.stderr}')
            return json.loads(result.stdout) if success else None
        report = command('verify-runtime', matching, image)
        require(report['runtime_compatibility_verified'] and report['all_payloads_verified'], 'missing runtime confirmation')
        require(not command('verify', matching)['runtime_compatibility_verified'], 'structural verification claimed compatibility')
        command('verify-runtime', matching, root/'wrong-image.bin', success=False)
        command('verify-runtime', root/'wrong-contract.lospv', image, success=False)
        command('verify-runtime', matching, root/'short-image.bin', success=False)
        command('verify-runtime', matching, root/'missing-image.bin', success=False)
        fetch.verify_pack(matching, tool, image)
        try:
            fetch.verify_pack(root/'wrong-contract.lospv', tool, image)
        except subprocess.CalledProcessError:
            pass
        else:
            raise AssertionError('release fetch accepted incompatible pack')
        target = root/'target'; target.mkdir()
        shutil.copy2(matching, target/'portable_vk.lospv')
        clean_env = {k:v for k,v in os.environ.items() if not k.startswith('LO_') and k!='GITHUB_ENV'}
        clean_env['LO_SHADER_RUNTIME_IMAGE'] = str(image)
        with patch.dict(os.environ, clean_env, clear=True):
            argv = ['fetch', '--target-dir', str(target), '--pack-tool', str(tool), '--runtime-image', str(image)]
            with patch.object(sys, 'argv', argv), patch.dict(os.environ, LO_PORTABLE_SHADER_PACK=str(explicit)):
                require(fetch.main() == 0, 'explicit override failed')
            require((target/'portable_vk.lospv').read_bytes() == explicit.read_bytes(), 'old cache won over explicit candidate')
            with patch.object(sys, 'argv', argv), patch.dict(os.environ, LO_PORTABLE_SHADER_PACK=str(target/'portable_vk.lospv')):
                require(fetch.main() == 0, 'source=destination failed')
            with patch.object(sys, 'argv', argv), patch.dict(os.environ, LO_PORTABLE_SHADER_PACK=str(root/'missing.lospv')), contextlib.redirect_stderr(io.StringIO()):
                try:
                    fetch.main()
                except SystemExit as error:
                    require(error.code != 0, 'explicit missing file returned success')
                else:
                    raise AssertionError('explicit missing file accepted')
            # Never contact GitHub or scan developer drives in an unavailable-input test.
            with patch.object(fetch, 'ROOT', root/'isolated'), patch.object(fetch, 'try_download_release_asset', return_value=False), patch.object(fetch, 'try_copy', return_value=False), patch.object(fetch, 'try_assemble_parts', return_value=False), patch.object(sys, 'argv', ['fetch','--target-dir',str(root/'optional'),'--no-required']):
                require(fetch.main() == 0, '--no-required unavailable path failed')
            runtime, packaged = root/'runtime', root/'packaged'
            runtime.mkdir()
            shutil.copy2(tool, runtime / ('LoShaderPackTool.exe' if os.name == 'nt' else 'LoShaderPackTool'))
            with patch.dict(os.environ, LO_PORTABLE_SHADER_PACK=str(matching)):
                result = stage.stage_portable_shader_pack(runtime, packaged, root/'licenses')
                require(result['runtime_compatibility_verified'], 'staged payload not checked against runtime')
            with patch.dict(os.environ, LO_PORTABLE_SHADER_PACK=str(root/'wrong-contract.lospv')):
                try:
                    stage.stage_portable_shader_pack(runtime, root/'bad-package', root/'licenses')
                except SystemExit:
                    require(not (root/'bad-package/shaders/portable_vk.lospv').exists(), 'rejected pack distributed')
                else:
                    raise AssertionError('packager accepted wrong contract')
    print('PASS release contract: matching/wrong/short/missing image, override precedence, same path, optional absence, staged validation')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
