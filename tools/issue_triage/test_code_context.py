import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import code_context


class CodeContextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)

    def source(self, name, content, tracked=True):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
        if tracked:
            subprocess.run(['git', 'add', '--', name], cwd=self.root, check=True)
        return path

    def test_relevant_source_has_exact_lines_and_semantic_mapping(self):
        self.source('LostOdysseyRecomp/movie/bink.cpp', '\n'.join(['// padding'] * 30 + ['void DrawSubtitle() {}']))
        self.source('LostOdysseyRecomp/gpu/texture.cpp', 'void unrelated() {}')
        found = code_context.retrieve(self.root, 'FMV subtitle missing')
        self.assertEqual(found[0]['file'], 'LostOdysseyRecomp/movie/bink.cpp')
        self.assertIn('31: void DrawSubtitle() {}', found[0]['excerpt'])
        self.assertEqual(found[0]['start_line'], 19)

    def test_untracked_excluded_and_outside_never_read(self):
        self.source('tools/hidden.py', 'secret subtitle', tracked=False)
        self.source('tools/thirdparty/bink.cpp', 'secret subtitle')
        self.source('docs/movie.cpp', 'secret subtitle')
        self.assertEqual(code_context.retrieve(self.root, '../secret subtitle tools/hidden.py'), [])
        self.assertIsNone(code_context._safe_source(self.root, 'tools/../../secret.cpp'))

    def test_symlink_rejected(self):
        target = self.source('tools/target.cpp', 'subtitle')
        link = self.root / 'tools/link.cpp'
        try:
            link.symlink_to(target)
        except OSError:
            self.skipTest('symlink privilege unavailable')
        self.assertIsNone(code_context._safe_source(self.root, 'tools/link.cpp'))

    def test_output_and_scan_budgets(self):
        for i in range(9):
            self.source(f'tools/subtitle{i}.cpp', ('subtitle ' + 'x' * 60 + '\n') * 100)
        found = code_context.retrieve(self.root, 'subtitle')
        self.assertEqual(len(found), 6)
        self.assertLessEqual(len(json.dumps(found, ensure_ascii=False)), code_context.MAX_CHARS)
        with patch.object(code_context, 'MAX_SCAN_BYTES', 10):
            self.assertEqual(code_context.retrieve(self.root, 'subtitle'), [])

    def test_explicit_path_ranks_first_and_binary_is_ignored(self):
        self.source('tools/specific.cpp', 'void specific() {}')
        self.source('tools/subtitle.cpp', 'subtitle\n' * 100)
        self.source('tools/binary.cpp', 'subtitle\0secret')
        found = code_context.retrieve(self.root, 'Look at tools/specific.cpp subtitle')
        self.assertEqual(found[0]['file'], 'tools/specific.cpp')
        self.assertFalse(any(x['file'].endswith('binary.cpp') for x in found))

    def test_entity_matches_beat_generic_words_and_test_sources(self):
        self.source('LostOdysseyRecomp/media.cpp', 'void DrawSubtitles() {}')
        self.source('tools/tests/subtitle_test.cpp', 'subtitle\n' * 100)
        self.source('LostOdysseyRecomp/allocation.cpp', 'allocation loading issue\n' * 100)
        query = '@codex analyze the PAL Spanish missing FMV subtitles issue and locate subtitle loading code'
        found = code_context.retrieve(self.root, query)
        self.assertEqual(found[0]['file'], 'LostOdysseyRecomp/media.cpp')
        self.assertFalse(any(x['file'].endswith('allocation.cpp') for x in found))

    def test_no_core_match_does_not_fill_with_unrelated_sources(self):
        self.source('LostOdysseyRecomp/memory.cpp', 'allocation loading Spanish PAL')
        self.assertEqual(code_context.retrieve(self.root, 'Spanish PAL FMV subtitles loading'), [])


if __name__ == '__main__':
    unittest.main()
