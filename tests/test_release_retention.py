# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('retention', Path(__file__).resolve().parents[1] /
                                             'tools/releases/retain_current.py')
retention = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retention)


class ReleaseRetentionTests(unittest.TestCase):
    def test_transient_github_error_is_retried(self):
        results = [subprocess.CompletedProcess([], 1, '', 'HTTP 500: Internal Server Error'),
                   subprocess.CompletedProcess([], 0, '[]', '')]
        with patch.object(retention.subprocess, 'run', side_effect=results) as run, \
                patch.object(retention.time, 'sleep'):
            self.assertEqual(retention.gh('release', 'list'), '[]')
            self.assertEqual(run.call_count, 2)

    def test_permission_errors_are_not_retried(self):
        with patch.object(retention.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 1, '', 'HTTP 403: Forbidden')) as run:
            with self.assertRaises(RuntimeError):
                retention.gh('release', 'list')
            self.assertEqual(run.call_count, 1)

    def test_deleted_references_are_idempotent(self):
        with patch.object(retention.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 1, '', 'HTTP 404: Not Found')):
            self.assertEqual(retention.gh('api', '-X', 'DELETE', 'reference', missing_ok=True), '')

    def test_branch_advanced_during_cleanup_is_preserved(self):
        results = [subprocess.CompletedProcess([], 1, '', 'stale info'),
                   subprocess.CompletedProcess([], 0, 'new-sha\trefs/heads/fix/work\n', '')]
        with patch.object(retention.subprocess, 'run', side_effect=results) as run:
            retention.delete_branch('fix/work', 'old-sha')
            self.assertEqual(run.call_count, 2)

    def test_branch_delete_retries_when_its_commit_is_unchanged(self):
        results = [subprocess.CompletedProcess([], 1, '', 'fatal error in commit_refs'),
                   subprocess.CompletedProcess([], 0, 'old-sha\trefs/heads/fix/work\n', ''),
                   subprocess.CompletedProcess([], 0, '', '')]
        with patch.object(retention.subprocess, 'run', side_effect=results) as run, \
                patch.object(retention.time, 'sleep'):
            retention.delete_branch('fix/work', 'old-sha')
            self.assertEqual(run.call_count, 3)

    def releases(self):
        return [dict(tagName='v0.1.81', isPrerelease=True, isDraft=False, publishedAt='2026-11-04'),
                dict(tagName='v0.1.80', isPrerelease=True, isDraft=False, publishedAt='2026-11-03')]

    def test_cleanup_preserves_current_future_and_unmerged_branches(self):
        names = ['main', 'release-input/v0.1.81', 'release-input/v0.1.80',
                 'release-input/v0.1.82', 'fix/merged', 'fix/unfinished']
        branches = [dict(name=name, commit={'sha': str(index) * 40})
                    for index, name in enumerate(names)]
        def gh(*args, **kwargs):
            if args[0] == 'release' and args[1] == 'list':
                return json.dumps(self.releases())
            if args[-1] == 'repos/{owner}/{repo}/branches':
                return json.dumps([branches])
            if 'matching-refs' in args[-1]:
                return json.dumps([{'ref': 'refs/tags/v0.1.79'}])
            if '/compare/' in args[-1]:
                return json.dumps({'status': 'ahead' if '4444' in args[-1] else 'diverged'})
            return ''
        with patch.object(retention, 'gh', side_effect=gh) as calls, \
                patch.object(retention.subprocess, 'run', return_value=
                             subprocess.CompletedProcess([], 0, '', '')) as run:
            retention.retain('v0.1.81')
            deleted = [call.args[0][-1] for call in run.call_args_list]
            self.assertEqual(deleted, [':refs/heads/release-input/v0.1.80', ':refs/heads/fix/merged'])
            self.assertIn('--force-with-lease=refs/heads/fix/merged:' + '4' * 40,
                          run.call_args_list[1].args[0])
            self.assertTrue(any(call.args[-1].endswith('/tags/v0.1.79') for call in calls.call_args_list))

    def test_unpublished_current_release_blocks_cleanup(self):
        releases = self.releases()
        releases[0]['isDraft'] = True
        with patch.object(retention, 'gh', return_value=json.dumps(releases)) as calls:
            with self.assertRaises(RuntimeError):
                retention.retain('v0.1.81')
            self.assertEqual(calls.call_count, 1)

    def test_newer_release_blocks_cleanup(self):
        releases = self.releases()
        releases.append(dict(tagName='v0.1.82', isPrerelease=True, isDraft=False, publishedAt='2026-11-05'))
        with patch.object(retention, 'gh', return_value=json.dumps(releases)) as calls:
            with self.assertRaises(RuntimeError):
                retention.retain('v0.1.81')
            self.assertEqual(calls.call_count, 1)


if __name__ == '__main__':
    unittest.main()
