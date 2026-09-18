"""Stage only the portable distribution artifact, never local cache directories."""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def stage_portable_shader_pack(runtime_directory: Path, executable_directory: Path,
                               licenses: Path) -> dict | None:
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "thirdparty/zstd-LICENSE.txt", licenses / "zstd-LICENSE.txt")
    env_override = os.environ.get("LO_PORTABLE_SHADER_PACK")
    source = Path(env_override) if env_override else runtime_directory / "shaders/portable_vk.lospv"
    if not source.exists():
        return None
    if source.is_symlink() or not source.is_file():
        raise SystemExit("Portable shader pack must be a regular file")
    tools = [runtime_directory / "LoShaderPackTool.exe", runtime_directory / "LoShaderPackTool"]
    tool = next((path for path in tools if path.is_file()), None)
    if tool is None:
        raise SystemExit("Build target LoShaderPackTool before packaging a portable shader pack")
    destination = executable_directory / "shaders/portable_vk.lospv"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    try:
        image = Path(os.environ.get("LO_SHADER_RUNTIME_IMAGE", ROOT / "LostOdysseyRecompLib/private/image_disc1.bin"))
        result = subprocess.run([str(tool), "verify-runtime", str(destination), str(image)], check=True,
                                capture_output=True, text=True, timeout=600)
        report = json.loads(result.stdout)
        if not report.get("all_payloads_verified") or not report.get("runtime_compatibility_verified") or report.get("file_bytes") != destination.stat().st_size:
            raise ValueError("verifier did not validate the staged artifact")
    except (subprocess.SubprocessError, OSError, ValueError) as error:
        destination.unlink(missing_ok=True)
        raise SystemExit(f"Portable shader pack verification failed: {error}") from error
    print(f"Portable shaders: {report['records']} records, {report['unique_binaries']} unique binaries, "
          f"{report['file_bytes']} distributed bytes")
    # Verifier uses the shared runtime contract and this build's loaded image.
    return report
