"""Package the precompiled startup shader bundle into a release asset archive."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def detect_version() -> str:
    try:
        tag = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
        if tag:
            return tag
    except Exception:
        pass
    try:
        ver = subprocess.check_output(
            ["git", "describe", "--tags", "--always"],
            cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
        if ver:
            return ver
    except Exception:
        pass
    return "v0.5.15"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, help="Path to startup_vk12_v1.bundle")
    parser.add_argument("--cache-dir", type=Path, default=ROOT / "cache/shaders",
                        help="Path to shaders cache directory")
    parser.add_argument("--output", type=Path, default=ROOT / "out/releases",
                        help="Output directory for packaged release assets")
    parser.add_argument("--version", type=str, default="",
                        help="Release version string (e.g. v0.5.15)")
    args = parser.parse_args()

    version = args.version.strip() or detect_version()
    if not version.startswith("v"):
        version = "v" + version

    bundle_path = args.bundle
    if not bundle_path:
        candidates = [
            args.cache_dir / "startup_vk12_v1.bundle",
            Path(r"D:\Mihoyo\LostOdysseyRecomp-windows-x64\cache\shaders\startup_vk12_v1.bundle"),
            ROOT / "cache/shaders/startup_vk12_v1.bundle",
        ]
        for c in candidates:
            if c.is_file():
                bundle_path = c
                break

    if not bundle_path or not bundle_path.is_file():
        sys.exit(f"Error: startup_vk12_v1.bundle not found. Looked in {args.cache_dir} and candidates.")

    bundle_size = bundle_path.stat().st_size
    if bundle_size < 1024:
        sys.exit(f"Error: bundle file {bundle_path} is too small ({bundle_size} bytes).")

    print(f"Source bundle: {bundle_path} ({bundle_size / (1024*1024):.2f} MB)")
    bundle_hash = sha256_file(bundle_path)
    print(f"Bundle SHA-256: {bundle_hash}")

    args.output.mkdir(parents=True, exist_ok=True)
    zip_name = f"LostOdysseyRecomp-shader-bundle-vk12-{version}.zip"
    zip_path = args.output / zip_name

    print(f"Compressing into {zip_path}...")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        # Store under cache/shaders/startup_vk12_v1.bundle so extracting into game dir puts it in place
        zf.write(bundle_path, arcname="cache/shaders/startup_vk12_v1.bundle")

    zip_size = zip_path.stat().st_size
    zip_hash = sha256_file(zip_path)
    sha_path = zip_path.with_suffix(".zip.sha256")
    sha_path.write_text(f"{zip_hash}  {zip_name}\n", encoding="utf-8")

    ratio = (zip_size / bundle_size) * 100.0
    print(f"\nSuccess! Created shader bundle archive:")
    print(f"  Archive:     {zip_path} ({zip_size / (1024*1024):.2f} MB, {ratio:.1f}% of original)")
    print(f"  Checksum:    {sha_path} ({zip_hash})")
    print(f"\nPlayers can download this archive and extract it to the game directory to skip startup compilation.")


if __name__ == "__main__":
    main()
