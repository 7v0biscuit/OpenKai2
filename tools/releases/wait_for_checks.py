#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Require main and release-ref component checks before publishing assets."""
import json
import os
import subprocess
import time


def result(runs, revision, ref):
    matching = [run for run in runs if run['headSha'] == revision and
                run['headBranch'] == ref and run['event'] == 'push']
    if not matching:
        return None
    current = max(matching, key=lambda run: run['databaseId'])
    if current['status'] != 'completed':
        return None
    if current['conclusion'] != 'success':
        raise RuntimeError('Component tests did not pass for this release ref: ' + current['url'])
    return current['url']


def wait(revision, ref, timeout=2400):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        runs = json.loads(subprocess.check_output([
            'gh', 'run', 'list', '--workflow', 'probes.yml', '--commit', revision,
            '--limit', '100', '--json',
            'databaseId,headSha,headBranch,event,status,conclusion,url'], text=True))
        url = result(runs, revision, ref)
        if url:
            print('Release component checks passed: ' + url, flush=True)
            return
        print('Waiting for Component tests on ' + ref + ' at ' + revision[:12], flush=True)
        time.sleep(15)
    raise TimeoutError('Component checks did not finish; release remains unpublished.')


def validate_release(revision, ref):
    wait(revision, 'main')
    if ref != 'main':
        wait(revision, ref)


if __name__ == '__main__':
    validate_release(os.environ['GITHUB_SHA'], os.environ['GITHUB_REF_NAME'])
