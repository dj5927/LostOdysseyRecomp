"""AppImage packager collects linuxdeploy output before the temp dir is deleted."""
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import sys


ROOT = Path(__file__).resolve().parents[2]


def load_packager():
    spec = importlib.util.spec_from_file_location('package_appimage', ROOT / 'tools/package_appimage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PackageAppImageTests(unittest.TestCase):
    def test_collects_appimage_from_linuxdeploy_cwd_before_temp_cleanup(self):
        module = load_packager()
        with tempfile.TemporaryDirectory(prefix='lo-package-appimage-') as tmp:
            base = Path(tmp)
            binaries = base / 'build' / 'LostOdysseyRecomp'
            binaries.mkdir(parents=True)
            (binaries / 'LostOdysseyRecomp').write_bytes(b'mock-runtime')
            (binaries / 'libdxcompiler.so').write_bytes(b'mock-dxc')
            output = base / 'output'

            def fake_linuxdeploy(cmd, **kwargs):
                cwd = Path(kwargs['cwd'])
                produced = cwd / 'LostOdysseyRecomp-x86_64.AppImage'
                produced.write_bytes(b'successful mock package')
                self.assertTrue(produced.exists())
                self.assertIn('--appdir', cmd)
                self.assertEqual(cmd[cmd.index('--output') + 1], 'appimage')

            argv = [
                'package_appimage.py',
                '--build', str(base / 'build'),
                '--output', str(output),
                '--version', 'v0.5.13',
            ]
            with patch.object(sys, 'argv', argv), \
                    patch.object(module.shutil, 'which', return_value='linuxdeploy'), \
                    patch.object(module.subprocess, 'run', side_effect=fake_linuxdeploy):
                module.main()

            destination = output / 'LostOdysseyRecomp-linux-x64-v0.5.13.AppImage'
            checksum = destination.with_suffix('.AppImage.sha256')
            self.assertTrue(destination.is_file())
            self.assertEqual(destination.read_bytes(), b'successful mock package')
            self.assertTrue(checksum.is_file())
            self.assertIn(destination.name, checksum.read_text(encoding='utf-8'))

    def test_relative_linuxdeploy_is_resolved_before_subprocess_cwd_change(self):
        module = load_packager()
        with tempfile.TemporaryDirectory(prefix='lo-relative-linuxdeploy-') as tmp:
            base = Path(tmp)
            binaries = base / 'build' / 'LostOdysseyRecomp'
            binaries.mkdir(parents=True)
            (binaries / 'LostOdysseyRecomp').write_bytes(b'mock-runtime')
            (binaries / 'libdxcompiler.so').write_bytes(b'mock-dxc')
            tool_dir = base / 'out' / 'tools' / 'linuxdeploy'
            tool_dir.mkdir(parents=True)
            if os.name == 'nt':
                tool = tool_dir / 'linuxdeploy.cmd'
                tool.write_text('@echo off\r\n', encoding='ascii')
                relative = r'out\tools\linuxdeploy\linuxdeploy.cmd'
            else:
                tool = tool_dir / 'linuxdeploy'
                tool.write_text('#!/bin/sh\n')
                tool.chmod(0o755)
                relative = 'out/tools/linuxdeploy/linuxdeploy'

            captured = {}

            def fake_linuxdeploy(cmd, **kwargs):
                captured['cmd'] = list(cmd)
                captured['cwd'] = kwargs['cwd']
                Path(kwargs['cwd'], 'LostOdysseyRecomp-x86_64.AppImage').write_bytes(b'ok')

            argv = [
                'package_appimage.py',
                '--build', str(base / 'build'),
                '--output', str(base / 'output'),
                '--version', 'v0.5.13',
                '--linuxdeploy', relative,
            ]
            previous = Path.cwd()
            try:
                os.chdir(base)
                self.assertEqual(module.shutil.which(relative), relative.replace('\\', os.sep)
                                 if os.name == 'nt' else relative)
                with patch.object(sys, 'argv', argv), \
                        patch.object(module.subprocess, 'run', side_effect=fake_linuxdeploy):
                    module.main()
            finally:
                os.chdir(previous)

            self.assertTrue(Path(captured['cmd'][0]).is_absolute())
            self.assertEqual(Path(captured['cmd'][0]).resolve(), tool.resolve())
            self.assertNotEqual(Path(captured['cwd']).resolve(), base.resolve())
            destination = base / 'output' / 'LostOdysseyRecomp-linux-x64-v0.5.13.AppImage'
            self.assertTrue(destination.is_file())

    def test_relative_linuxdeploy_executable_survives_real_subprocess_cwd_change(self):
        module = load_packager()
        with tempfile.TemporaryDirectory(prefix='lo-relative-linuxdeploy-run-') as tmp:
            base = Path(tmp)
            binaries = base / 'build' / 'LostOdysseyRecomp'
            binaries.mkdir(parents=True)
            (binaries / 'LostOdysseyRecomp').write_bytes(b'mock-runtime')
            (binaries / 'libdxcompiler.so').write_bytes(b'mock-dxc')
            tool_dir = base / 'out' / 'tools' / 'linuxdeploy'
            tool_dir.mkdir(parents=True)
            if os.name == 'nt':
                tool = tool_dir / 'linuxdeploy.cmd'
                tool.write_text(
                    '@echo off\r\n'
                    'echo successful mock package>LostOdysseyRecomp-x86_64.AppImage\r\n',
                    encoding='ascii')
                relative = r'out\tools\linuxdeploy\linuxdeploy.cmd'
            else:
                tool = tool_dir / 'linuxdeploy'
                tool.write_text('#!/bin/sh\nprintf package > LostOdysseyRecomp-x86_64.AppImage\n')
                tool.chmod(0o755)
                relative = 'out/tools/linuxdeploy/linuxdeploy'

            argv = [
                'package_appimage.py',
                '--build', str(base / 'build'),
                '--output', str(base / 'output'),
                '--version', 'v0.5.13',
                '--linuxdeploy', relative,
            ]
            previous = Path.cwd()
            try:
                os.chdir(base)
                with patch.object(sys, 'argv', argv):
                    module.main()
            finally:
                os.chdir(previous)

            destination = base / 'output' / 'LostOdysseyRecomp-linux-x64-v0.5.13.AppImage'
            self.assertTrue(destination.is_file())
            self.assertTrue(destination.stat().st_size > 0)
            self.assertTrue(tool.exists())
            self.assertIn('successful mock package', destination.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
