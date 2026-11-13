# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'release_checks', Path(__file__).resolve().parents[1] / 'tools/releases/wait_for_checks.py')
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)


def run(identifier=1, ref='v0.3.3', status='completed', conclusion='success'):
    return dict(databaseId=identifier, headSha='a'*40, headBranch=ref, event='push',
                status=status, conclusion=conclusion, url='https://github.com/check/' + str(identifier))


class ReleaseChecks(unittest.TestCase):
    def test_main_success_cannot_replace_tag_checks(self):
        self.assertIsNone(checks.result([run(ref='main')], 'a'*40, 'v0.3.3'))

    def test_pending_tag_blocks_publication(self):
        self.assertIsNone(checks.result([run(status='in_progress', conclusion='')], 'a'*40, 'v0.3.3'))

    def test_failed_tag_blocks_publication(self):
        with self.assertRaisesRegex(RuntimeError, 'did not pass'):
            checks.result([run(conclusion='failure')], 'a'*40, 'v0.3.3')

    def test_latest_run_must_succeed(self):
        with self.assertRaises(RuntimeError):
            checks.result([run(), run(2, conclusion='cancelled')], 'a'*40, 'v0.3.3')
        self.assertEqual(checks.result([run()], 'a'*40, 'v0.3.3'), 'https://github.com/check/1')

    def test_other_commit_cannot_replace_checks(self):
        self.assertIsNone(checks.result([run()], 'b'*40, 'v0.3.3'))

    def test_timeout_leaves_release_unpublished(self):
        with patch.object(checks.time, 'monotonic', side_effect=[0, 2401]):
            with self.assertRaises(TimeoutError):
                checks.wait('a'*40, 'v0.3.3')

    def test_main_and_tag_must_both_pass(self):
        with patch.object(checks, 'wait') as wait:
            checks.validate_release('a'*40, 'v0.3.3')
            self.assertEqual([call.args for call in wait.call_args_list],
                             [('a'*40, 'main'), ('a'*40, 'v0.3.3')])

    def test_manual_main_publish_checks_main_once(self):
        with patch.object(checks, 'wait') as wait:
            checks.validate_release('a'*40, 'main')
            wait.assert_called_once_with('a'*40, 'main')


if __name__ == '__main__':
    unittest.main()
