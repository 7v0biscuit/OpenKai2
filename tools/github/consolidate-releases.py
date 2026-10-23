#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Consolidate the existing preview releases without changing their binaries."""
import json
import os
from pathlib import Path
import re
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
OLD_TAG = 'v0.1.0-preview.37'
NEW_TAG = 'v0.1.0-battle-startup-pcm-audio'
TITLE = 'Battle startup and PCM audio'
PREVIEW = re.compile(r'v0\.1\.0-preview\.(\d+)')


def main():
    repo = os.environ['GH_REPO']
    commit = os.environ['RELEASE_COMMIT']
    assert re.fullmatch(r'[0-9a-f]{40}', commit), 'Expected an exact source commit'
    config = json.loads((ROOT / 'packaging/windows/release-input.json').read_text())
    assert NEW_TAG == 'v' + config['version']

    def api(method, endpoint, payload=None):
        request = urllib.request.Request(
            f'https://api.github.com/repos/{repo}/{endpoint}',
            data=None if payload is None else json.dumps(payload).encode(),
            headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                     'Accept': 'application/vnd.github+json',
                     'X-GitHub-Api-Version': '2022-11-28',
                     'Content-Type': 'application/json'}, method=method)
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
            return json.loads(data) if data else None

    releases = []
    page = 1
    while True:
        batch = api('GET', f'releases?per_page=100&page={page}')
        releases.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    refs = api('GET', 'git/matching-refs/tags/')
    tags = {ref['ref'].removeprefix('refs/tags/'): ref for ref in refs}
    # Refuse to remove a newer publication if this one-time migration is rerun.
    assert all(int(m[1]) <= 37 for tag in tags if (m := PREVIEW.fullmatch(tag)))
    assert all(tag in (OLD_TAG, NEW_TAG) or PREVIEW.fullmatch(tag) for tag in tags), \
        'Unexpected release tag; review the migration before proceeding'
    current = next((r for r in releases if r['tag_name'] == NEW_TAG), None)
    if current is None:
        current = next(r for r in releases if r['tag_name'] == OLD_TAG)
    assert not current['draft'] and current['published_at'], 'Keep a published release'
    expected_assets = {'OpenKai2.exe', 'OpenKai2-windows-x64.zip',
                       'OpenKai2-Setup.exe', 'SHA256SUMS.txt'}
    assets = {a['name']: a['id'] for a in current['assets']}
    assert set(assets) == expected_assets and all(a['size'] > 0 for a in current['assets'])

    if NEW_TAG not in tags:
        api('POST', 'git/refs', {'ref': 'refs/tags/' + NEW_TAG, 'sha': commit})
    notes = (ROOT / 'docs/release-notes/0.1.0-battle-startup-pcm-audio.md').read_text()
    api('PATCH', f'releases/{current["id"]}', {
        'tag_name': NEW_TAG, 'name': TITLE, 'body': notes,
        'draft': False, 'prerelease': True, 'make_latest': 'false'})
    kept = api('GET', f'releases/{current["id"]}')
    assert kept['tag_name'] == NEW_TAG
    assert {a['name']: a['id'] for a in kept['assets']} == assets, 'Download assets changed'
    print(f'Retained {NEW_TAG}: all four download assets preserved', flush=True)

    for release in releases:
        if release['id'] != current['id'] and PREVIEW.fullmatch(release['tag_name']):
            api('DELETE', f'releases/{release["id"]}')
            print('Removed superseded release:', release['tag_name'], flush=True)
    for tag in sorted(tags):
        if PREVIEW.fullmatch(tag):
            api('DELETE', 'git/refs/tags/' + urllib.parse.quote(tag, safe=''))
            print('Removed superseded tag:', tag, flush=True)
    remaining = api('GET', 'git/matching-refs/tags/')
    assert {ref['ref'] for ref in remaining} == {'refs/tags/' + NEW_TAG}
    print('Release page consolidation complete', flush=True)


if __name__ == '__main__':
    main()
