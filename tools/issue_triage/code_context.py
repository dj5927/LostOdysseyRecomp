"""Bounded, read-only retrieval of tracked first-party source excerpts."""
from pathlib import Path, PurePosixPath
import re
import subprocess

MAX_FILES = 6
MAX_CHARS = 24000
MAX_FILE_BYTES = 256000
MAX_SCAN_BYTES = 12000000
EXTENSIONS = {'.cpp', '.h', '.hpp', '.c', '.cc', '.inl', '.py', '.js', '.ts',
              '.cs', '.cmake', '.hlsl', '.glsl', '.vert', '.frag', '.comp', '.ps1'}
EXCLUDED = {'thirdparty', 'third_party', 'vendor', 'node_modules', 'out', 'build',
            'generated', 'private', 'docs', '.git', '__pycache__'}
STOP = {'the', 'and', 'that', 'this', 'with', 'from', 'issue', 'codex', 'please',
        'what', 'when', 'have', 'does', 'code', 'analyze', 'analysis', 'missing',
        'locate', 'loading', 'look', 'into', 'find', 'show', 'explain', 'could',
        'would', 'should', 'problem', 'error', 'using', 'after', 'before', 'not'}
ALIASES = {'subtitle': ('movie', 'bink', 'subtitle'), 'fmv': ('movie', 'bink', 'subtitle'),
           'video': ('movie', 'bink'), 'movie': ('movie', 'bink', 'subtitle'),
           'crash': ('exception', 'allocation', 'memory'),
           'allocation': ('alloc', 'memory'), 'audio': ('audio', 'xma'),
           'shader': ('shader', 'gpu'), '字幕': ('movie', 'bink', 'subtitle'),
           '崩溃': ('exception', 'allocation', 'memory')}


def _safe_source(root, name):
    parts = PurePosixPath(name).parts
    if not parts or parts[0] not in {'LostOdysseyRecomp', 'tools'}:
        return None
    if any(p.lower() in EXCLUDED or p in {'.', '..'} for p in parts) or parts[:2] == ('tools', 'issue_triage'):
        return None
    if PurePosixPath(name).suffix.lower() not in EXTENSIONS:
        return None
    current = root
    for part in parts:
        current = current / part
        if current.is_symlink() or (hasattr(current, 'is_junction') and current.is_junction()):
            return None
    try:
        current.resolve().relative_to(root)
        if not current.is_file():
            return None
    except (ValueError, OSError):
        return None
    return current


def _tokens(text):
    text = re.sub(r'([a-z0-9])([A-Z])', r'\1 \2', text)
    return {t[:-1] if t.endswith('s') and len(t) > 4 else t
            for t in re.findall(r'[a-z0-9]+', text.lower())}


def retrieve(root: Path, query: str) -> list[dict]:
    """Return source facts only; query text is never executed or used as a path."""
    root = root.resolve()
    query = query[:20000]
    terms = {t for t in _tokens(query) if len(t) >= 3} - STOP
    query = query.lower()
    core = set()
    for key, values in ALIASES.items():
        if key in terms or (not key.isascii() and key in query):
            terms.update(values)
            core.update(values)
    terms = sorted(terms)[:80]
    if not terms:
        return []
    try:
        result = subprocess.run(['git', 'ls-files', '-z', '--', 'LostOdysseyRecomp', 'tools'],
                                cwd=root, capture_output=True, check=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return []
    names = sorted(set(result.stdout.decode('utf-8', errors='replace').split('\0')))
    def path_score(name):
        lower = name.lower()
        tokens = _tokens(name)
        return (100 if lower in query else 0) + sum(12 if t in core else 4 for t in terms if t in tokens)
    names.sort(key=lambda name: (-path_score(name), name))
    scanned = 0
    candidates = []
    for name in names:
        path = _safe_source(root, name)
        if path is None:
            continue
        try:
            size = path.stat().st_size
            if size > MAX_FILE_BYTES or scanned + size > MAX_SCAN_BYTES:
                continue
            raw = path.read_bytes()[:MAX_FILE_BYTES + 1]
            scanned += len(raw)
            if len(raw) > MAX_FILE_BYTES or b'\0' in raw:
                continue
            source = raw.decode('utf-8')
        except (OSError, UnicodeError):
            continue
        lines = source.splitlines()
        if not lines:
            continue
        token_lines = [_tokens(line) for line in lines]
        matched = set().union(*token_lines, _tokens(name)) & set(terms)
        if core and not matched & core and name.lower() not in query:
            continue
        scores = [sum(8 if t in core else 1 for t in terms if t in tokens) for tokens in token_lines]
        score = path_score(name) + sum(16 if t in core else 2 for t in matched)
        if name.startswith('tools/'):
            score *= 0.4
        if 'test' in _tokens(name):
            score *= 0.25
        if not score:
            continue
        anchor = max(range(len(lines)), key=lambda i: scores[i])
        start = max(0, anchor - 12)
        stop = min(len(lines), start + 65)
        numbered = []
        length = 0
        for i in range(start, stop):
            line = f'{i + 1}: {lines[i]}'
            if length + len(line) + 1 > 3600:
                break
            numbered.append(line)
            length += len(line) + 1
        if numbered:
            candidates.append((score, name, start + 1, '\n'.join(numbered)))
    output = []
    total = 2
    import json
    for score, name, start, excerpt in sorted(candidates, key=lambda x: (-x[0], x[1])):
        item = {'file': name, 'start_line': start, 'excerpt': excerpt}
        cost = len(json.dumps(item, ensure_ascii=False)) + 2
        if total + cost > MAX_CHARS:
            continue
        output.append(item)
        total += cost
        if len(output) == MAX_FILES:
            break
    return output
