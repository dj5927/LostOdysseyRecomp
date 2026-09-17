"""Package staging tests using the real verifier and a CPU-generated fixture."""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from portable_shader_pack_payload import stage_portable_shader_pack

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lo-pack-staging-") as tmp:
        root = Path(tmp)
        runtime, target = root / "runtime", root / "package"
        (runtime / "shaders").mkdir(parents=True)
        (runtime / "cache/shaders").mkdir(parents=True)
        (runtime / "cache/shaders/startup_vk12_v1.bundle").write_bytes(b"DO NOT SHIP")
        tool = runtime / ("LoShaderPackTool.exe" if args.tool.suffix == ".exe" else "LoShaderPackTool")
        shutil.copy2(args.tool, tool)
        source = runtime / "shaders/portable_vk.lospv"
        shutil.copy2(args.fixture, source)
        report = stage_portable_shader_pack(runtime, target, target / "licenses")
        assert report and report["all_payloads_verified"]
        assert (target / "shaders/portable_vk.lospv").read_bytes() == args.fixture.read_bytes()
        assert (target / "licenses/zstd-LICENSE.txt").is_file()
        assert not (target / "cache").exists()
        source.write_bytes(b"corrupt")
        try:
            stage_portable_shader_pack(runtime, target, target / "licenses")
        except SystemExit:
            pass
        else:
            raise AssertionError("corrupt staged pack accepted")
        assert not (target / "shaders/portable_vk.lospv").exists()
        source.unlink()
        assert stage_portable_shader_pack(runtime, target, target / "licenses") is None
        shutil.copy2(args.fixture, source)
        tool.unlink()
        try:
            stage_portable_shader_pack(runtime, target, target / "licenses")
        except SystemExit:
            pass
        else:
            raise AssertionError("missing verifier accepted")
    print("PASS packaging staging: optional pack, exact copy/allowlist/license, real verification, corruption rejection, missing verifier")

if __name__ == "__main__":
    main()
