"""CI-only private XEX provisioning; never prints the URL or file contents."""
import hashlib
import os
from pathlib import Path
import re
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
IMPORTER = ROOT / 'LostOdysseyRecomp' / 'install' / 'import_game.cpp'


def asia_disc1_sha256():
    text = IMPORTER.read_text(encoding='utf-8')
    match = re.search(r'"asia"[\s\S]*?\{\s*""\s*,\s*"([0-9a-f]{64})"', text)
    if not match:
        raise SystemExit('Could not read the Disc 1 SHA256 from the native importer.')
    return match.group(1)


local = os.environ.get('LO_BUILD_XEX_PATH', '')
url = os.environ.get('LO_BUILD_XEX_URL', '')
if not local and not url.startswith('https://'):
    raise SystemExit('Configure the LO_BUILD_XEX_URL Actions secret with a private HTTPS Disc 1 XEX URL.')
try:
    with (Path(local).open('rb') if local else urllib.request.urlopen(url, timeout=120)) as response:
        data = response.read(32 * 1024**2 + 1)
except Exception:
    raise SystemExit('Could not retrieve the private build input. Check the Actions secret.') from None
if hashlib.sha256(data).hexdigest() != asia_disc1_sha256():
    raise SystemExit('Private build input does not match the supported Disc 1 XEX SHA256.')
target = Path('LostOdysseyRecompLib/private/disc1/default.xex')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(data)
print('Supported Disc 1 build input verified.')
