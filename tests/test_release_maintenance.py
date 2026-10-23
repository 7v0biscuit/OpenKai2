# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'release_maintenance', Path(__file__).resolve().parents[1] /
    'tools/github/consolidate-releases.py')
MAINTENANCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MAINTENANCE)


class ReleaseMaintenance(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        config = self.root / 'packaging/windows/release-input.json'
        config.parent.mkdir(parents=True)
        config.write_text(json.dumps({'version': MAINTENANCE.NEW_TAG[1:]}))
        notes = self.root / 'docs/release-notes' / (MAINTENANCE.NEW_TAG[1:] + '.md')
        notes.parent.mkdir(parents=True)
        notes.write_text('Release notes')
        self.releases = [dict(id=i, tag_name=f'v0.1.0-preview.{i}',
                              draft=False, published_at='2026-10-23', assets=[])
                         for i in range(1, 38)]
        self.releases[-1]['assets'] = [dict(id=i, name=name, size=100)
                                      for i, name in enumerate([
                                          'OpenKai2.exe', 'OpenKai2-windows-x64.zip',
                                          'OpenKai2-Setup.exe', 'SHA256SUMS.txt'])]
        self.tags = {r['tag_name'] for r in self.releases}
        self.calls = []

    def request(self, request, timeout):
        endpoint = request.full_url.split('/repos/test/repo/')[1]
        method = request.get_method()
        payload = json.loads(request.data) if request.data else None
        self.calls.append((method, endpoint, payload))
        if method == 'GET' and endpoint.startswith('releases?'):
            result = self.releases
        elif method == 'GET' and endpoint == 'git/matching-refs/tags/':
            result = [{'ref': 'refs/tags/' + tag} for tag in self.tags]
        elif method == 'POST' and endpoint == 'git/refs':
            self.tags.add(payload['ref'].removeprefix('refs/tags/'))
            result = payload
        elif endpoint.startswith('releases/'):
            release = next(r for r in self.releases if r['id'] == int(endpoint.split('/')[1]))
            if method == 'PATCH':
                release.update(payload)
            elif method == 'DELETE':
                self.releases.remove(release)
            result = release
        elif method == 'DELETE' and endpoint.startswith('git/refs/tags/'):
            self.tags.remove(endpoint.removeprefix('git/refs/tags/'))
            result = None
        else:
            raise AssertionError((method, endpoint))
        return contextlib.closing(io.BytesIO(json.dumps(copy.deepcopy(result)).encode()))

    def run_migration(self):
        with patch.object(MAINTENANCE, 'ROOT', self.root), \
                patch.dict(os.environ, {'GH_REPO': 'test/repo', 'GH_TOKEN': 'test',
                                        'RELEASE_COMMIT': 'a' * 40}), \
                patch.object(MAINTENANCE.urllib.request, 'urlopen', self.request), \
                contextlib.redirect_stdout(io.StringIO()):
            MAINTENANCE.main()

    def test_consolidates_tags_and_releases_preserving_download_ids(self):
        assets = copy.deepcopy(self.releases[-1]['assets'])
        self.run_migration()
        self.assertEqual(self.tags, {MAINTENANCE.NEW_TAG})
        self.assertEqual(len(self.releases), 1)
        self.assertEqual(self.releases[0]['id'], 37)
        self.assertEqual(self.releases[0]['assets'], assets)
        self.assertTrue(self.releases[0]['prerelease'])
        self.assertEqual(self.releases[0]['name'], MAINTENANCE.TITLE)
        mutations = [method for method, _, _ in self.calls if method != 'GET']
        self.assertEqual(mutations[:2], ['POST', 'PATCH'])

    def test_rerun_preserves_current_release(self):
        self.run_migration()
        self.calls.clear()
        self.run_migration()
        self.assertFalse(any(method in ('DELETE', 'POST') for method, _, _ in self.calls))

    def test_newer_preview_prevents_all_mutations(self):
        self.tags.add('v0.1.0-preview.38')
        with self.assertRaises(AssertionError):
            self.run_migration()
        self.assertTrue(all(method == 'GET' for method, _, _ in self.calls))

    def test_unexpected_tag_prevents_all_mutations(self):
        self.tags.add('v0.2.0')
        with self.assertRaisesRegex(AssertionError, 'Unexpected release tag'):
            self.run_migration()
        self.assertTrue(all(method == 'GET' for method, _, _ in self.calls))

    def test_missing_download_prevents_all_mutations(self):
        self.releases[-1]['assets'].pop()
        with self.assertRaises(AssertionError):
            self.run_migration()
        self.assertTrue(all(method == 'GET' for method, _, _ in self.calls))
