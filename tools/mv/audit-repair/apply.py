#!/usr/bin/env python3
"""Apply the exact tested c605eb4 audit repair; no network access or game data."""
import base64, gzip, hashlib, json, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[3]
FOLDER = pathlib.Path(__file__).resolve().parent
payload = gzip.decompress(base64.b64decode(''.join((FOLDER / f'part-{i}.b64').read_text().strip() for i in range(3)), validate=True))
if hashlib.sha256(payload).hexdigest() != '92282984fd8adeda72d7dee99164b971bb64a57b056a558dd953a961257f0e2f':
    raise SystemExit('Audit payload checksum mismatch')
data = json.loads(payload)
manifest = data['manifest']
def digest(path):
    p = ROOT / path
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
for path in manifest:
    p = pathlib.PurePosixPath(path)
    if p.is_absolute() or '..' in p.parts or p.parts[0] not in ('LostOdysseyRecomp', 'tools', 'docs'):
        raise SystemExit('Unexpected patch path: ' + path)
if not all(digest(p) == m['after'] for p, m in manifest.items()):
    for path, m in manifest.items():
        if digest(path) != m['before']:
            raise SystemExit('Base source changed; refusing overwrite: ' + path)
    patch = data['patch'].encode()
    subprocess.run(['git', 'apply', '--check', '-'], input=patch, cwd=ROOT, check=True)
    subprocess.run(['git', 'apply', '-'], input=patch, cwd=ROOT, check=True)
for path, m in manifest.items():
    if digest(path) != m['after']:
        raise SystemExit('Post-apply fingerprint mismatch: ' + path)
(ROOT / 'out').mkdir(exist_ok=True)
(ROOT / 'out/mv-audit-repair-paths.json').write_text(json.dumps(list(manifest)))
print('Verified audit repair:', len(manifest), 'source/test/document files')
