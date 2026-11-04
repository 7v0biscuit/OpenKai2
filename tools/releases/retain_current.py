#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain the current development release and remove obsolete release inputs."""
import json
from pathlib import Path
import re
import subprocess
import time


def version(tag):
    match = re.fullmatch(r'v(\d+)\.(\d+)\.(\d+)(?:-[A-Za-z0-9.-]+)?', tag)
    return tuple(map(int, match.groups())) if match else None


def gh(*args, missing_ok=False):
    for attempt in range(4):
        result = subprocess.run(['gh', *args], text=True, capture_output=True)
        if result.returncode == 0:
            return result.stdout
        error = result.stderr or result.stdout
        if missing_ok and 'HTTP 404' in error:
            return ''
        transient = re.search(r'HTTP (?:429|500|502|503|504)\b', error)
        if not transient or attempt == 3:
            raise RuntimeError(f'GitHub command failed: {" ".join(args)}\n{error}')
        time.sleep(2 ** attempt)


def delete_branch(name, sha):
    ref = 'refs/heads/' + name
    for attempt in range(4):
        result = subprocess.run(['git', 'push', '--force-with-lease=' + ref + ':' + sha,
                                 'origin', ':' + ref], text=True, capture_output=True)
        if result.returncode == 0:
            return
        current = subprocess.run(['git', 'ls-remote', '--heads', 'origin', ref],
                                 text=True, capture_output=True, check=True).stdout.split()
        if not current or current[0] != sha:
            return  # Already deleted, or advanced by another publisher.
        if attempt == 3:
            raise RuntimeError(f'Cannot remove obsolete branch {name}: {result.stderr}')
        time.sleep(2 ** attempt)


def retain(tag):
    current_version = version(tag)
    if current_version is None:
        raise ValueError('Expected a numeric release tag')
    releases = json.loads(gh('release', 'list', '--limit', '100', '--json',
                             'tagName,isPrerelease,isDraft,publishedAt'))
    current = next(release for release in releases if release['tagName'] == tag)
    if current['isDraft'] or not current['publishedAt']:
        raise RuntimeError('The current release must be published before cleanup')
    if any(not release['isDraft'] and version(release['tagName']) and
           version(release['tagName']) > current_version for release in releases):
        raise RuntimeError('A newer release was published; preserving its references')
    for release in releases:
        old_version = version(release['tagName'])
        if (release['isPrerelease'] and not release['isDraft'] and old_version and
                old_version < current_version and release['publishedAt'] and
                release['publishedAt'] < current['publishedAt']):
            # Separate idempotent operations also recover if the release is
            # deleted successfully but a later tag deletion encounters an error.
            gh('release', 'delete', release['tagName'], '--yes', missing_ok=True)
            gh('api', '-X', 'DELETE', 'repos/{owner}/{repo}/git/refs/tags/' +
               release['tagName'], missing_ok=True)
    stable = {release['tagName'] for release in releases if not release['isPrerelease']}
    refs = json.loads(gh('api', 'repos/{owner}/{repo}/git/matching-refs/tags/v'))
    for ref in refs:
        old_tag = ref['ref'].removeprefix('refs/tags/')
        old_version = version(old_tag)
        if old_version and old_version < current_version and old_tag not in stable:
            gh('api', '-X', 'DELETE', 'repos/{owner}/{repo}/git/refs/tags/' + old_tag,
               missing_ok=True)
    branches = json.loads(gh('api', '--paginate', '--slurp',
                             'repos/{owner}/{repo}/branches'))
    for branch in (branch for page in branches for branch in page):
        name = branch['name']
        if name == 'main' or name == 'release-input/' + tag:
            continue
        old_input = version(name.removeprefix('release-input/')) if name.startswith('release-input/') else None
        if old_input:
            obsolete = old_input < current_version
        else:
            comparison = json.loads(gh('api', 'repos/{owner}/{repo}/compare/' +
                                       branch['commit']['sha'] + '...main'))
            obsolete = comparison['status'] in ('ahead', 'identical')
        if obsolete:
            # A lease preserves a branch if another push advances it after the
            # listing. Unmerged work and future release inputs are retained.
            delete_branch(name, branch['commit']['sha'])


if __name__ == '__main__':
    config = json.loads(Path('packaging/windows/release-input.json').read_text())
    retain('v' + config['version'])
