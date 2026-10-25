# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_names', ROOT / 'tools/releases/sync_notes.py')
NAMES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NAMES)


class ReleaseNames(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.commit = 'a' * 40
        self.assets = [{'apiUrl': 'https://api.github.com/assets/1', 'name': 'OpenKai2.zip', 'size': 123}]
        self.after = dict(assets=self.assets, tagName='v0.1.10', name='v0.1.10', isDraft=False)
        self.refs = [{'ref': 'refs/tags/v0.1.10'}]

    def command(self, *args):
        self.calls.append(args)
        if args[:3] == ('release', 'view', 'v0.1.10-issue'):
            return json.dumps(dict(assets=self.assets, isDraft=False))
        if args[:3] == ('release', 'view', 'v0.1.10'):
            return json.dumps(self.after)
        if args[0] == 'api' and '/commits/' in args[1]:
            return self.commit
        if args[0] == 'api' and '/matching-refs/' in args[1]:
            return json.dumps(self.refs)
        return ''

    def synchronize(self):
        with tempfile.TemporaryDirectory() as folder:
            NAMES.synchronize('v0.1.10-issue', {'v0.1.10-issue'}, self.command, Path(folder))

    def deleted(self):
        return any(c[:3] == ('api', '--method', 'DELETE') for c in self.calls)

    def test_versions_accept_suffixes_and_leave_other_tags_alone(self):
        self.assertEqual(NAMES.version_tag('v0.1.10-issue'), 'v0.1.10')
        self.assertEqual(NAMES.version_tag('v0.1.10'), 'v0.1.10')
        for tag in ['main', 'v1', 'v1.2.3/unsafe', 'other-v1.2.3']:
            self.assertIsNone(NAMES.version_tag(tag))

    def test_migration_keeps_commit_assets_then_deletes_original_tag(self):
        self.synchronize()
        self.assertFalse(any(c[:3] == ('api', '--method', 'POST') for c in self.calls))
        edit = next(c for c in self.calls if c[:2] == ('release', 'edit'))
        self.assertEqual(edit[edit.index('--tag') + 1], 'v0.1.10')
        self.assertNotIn('--target', edit)
        self.assertGreaterEqual(sum('/commits/v0.1.10' in c[1] for c in self.calls if c[0] == 'api' and len(c)>1), 3)
        self.assertEqual(self.calls[-1], ('api', '--method', 'DELETE', 'repos/{owner}/{repo}/git/refs/tags/v0.1.10-issue'))

    def test_changed_download_preserves_original_tag(self):
        self.after['assets'] = [{'apiUrl': 'https://api.github.com/assets/2', 'name': 'OpenKai2.zip', 'size': 123}]
        with self.assertRaisesRegex(RuntimeError, 'verification failed'):
            self.synchronize()
        self.assertFalse(self.deleted())

    def test_changed_title_preserves_original_tag(self):
        self.after['name'] = 'Unexpected title'
        with self.assertRaisesRegex(RuntimeError, 'verification failed'):
            self.synchronize()
        self.assertFalse(self.deleted())

    def test_release_collision_has_no_side_effects(self):
        with self.assertRaisesRegex(RuntimeError, 'Both'):
            NAMES.synchronize('v0.1.10-issue', {'v0.1.10-issue', 'v0.1.10'}, self.command)
        self.assertEqual(self.calls, [])

    def test_existing_numeric_tag_must_point_to_original_commit(self):
        self.refs = [{'ref': 'refs/tags/v0.1.10'}]
        def different(*args):
            if len(args)>1 and args[1] == 'repos/{owner}/{repo}/commits/v0.1.10':
                return 'b' * 40
            return self.command(*args)
        with self.assertRaisesRegex(RuntimeError, 'different commit'):
            NAMES.synchronize('v0.1.10-issue', {'v0.1.10-issue'}, different)
        self.assertFalse(self.deleted())
        self.assertFalse(any(c[:2] == ('release', 'edit') for c in self.calls))

    def test_missing_numeric_tag_cannot_move_release(self):
        self.refs = []
        with self.assertRaisesRegex(RuntimeError, 'Create v0.1.10'):
            self.synchronize()
        self.assertFalse(any(c[:2] == ('release', 'edit') for c in self.calls))
        self.assertFalse(self.deleted())

    def test_numeric_release_title_is_version_only(self):
        NAMES.synchronize('v0.1.10', {'v0.1.10'}, self.command)
        edit = next(c for c in self.calls if c[:2] == ('release', 'edit'))
        self.assertEqual(edit[:5], ('release', 'edit', 'v0.1.10', '--title', 'v0.1.10'))
        self.assertFalse(self.deleted())
