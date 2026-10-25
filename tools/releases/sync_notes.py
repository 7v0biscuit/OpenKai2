#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep published release tags, titles and notes aligned with numeric versions."""
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def version_tag(tag):
    match = re.fullmatch(r'v(\d+\.\d+\.\d+)(?:-[0-9A-Za-z.-]+)?', tag)
    return 'v' + match[1] if match else None


def gh(*arguments):
    result = subprocess.run(['gh', *arguments], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError('gh ' + ' '.join(arguments) + ': ' + result.stderr.strip())
    return result.stdout.strip()


def assets_of(release):
    # The API URL contains the asset ID. Renaming a release must preserve every
    # existing download, rather than uploading replacements with new identities.
    return sorted((a['apiUrl'], a['name'], a['size']) for a in release['assets'])


def synchronize(tag, published_tags, command=gh, notes_root=ROOT / 'docs/release-notes'):
    target = version_tag(tag)
    if target is None:
        return
    if target != tag and target in published_tags:
        raise RuntimeError(f'Both {tag} and {target} have releases; preserve both for review')
    before = json.loads(command('release', 'view', tag, '--json', 'assets,isDraft'))
    if before['isDraft']:
        return
    commit = None
    if target != tag:
        commit = command('api', f'repos/{{owner}}/{{repo}}/commits/{tag}', '--jq', '.sha')
        if not re.fullmatch('[0-9a-f]{40}', commit):
            raise RuntimeError(f'Invalid source commit for {tag}')
        refs = json.loads(command('api', 'repos/{owner}/{repo}/git/matching-refs/tags/' + target))
        exact = [r for r in refs if r['ref'] == 'refs/tags/' + target]
        if exact:
            resolved = command('api', f'repos/{{owner}}/{{repo}}/commits/{target}', '--jq', '.sha')
            if resolved != commit:
                raise RuntimeError(f'{target} points to a different commit; preserved without changes')
        else:
            # Automation may edit releases but cannot create a tag at an older
            # workflow commit. Require the repository's normal tag publishing
            # credentials to create the replacement before moving the release.
            raise RuntimeError(f'Create {target} at {commit} before synchronizing {tag}')
    arguments = ['release', 'edit', tag, '--title', target]
    if target != tag:
        arguments += ['--tag', target]
    notes = notes_root / (target[1:] + '.md')
    if notes.is_file():
        arguments += ['--notes-file', str(notes)]
    command(*arguments)
    after = json.loads(command('release', 'view', target, '--json', 'assets,tagName,name,isDraft'))
    if (after['tagName'] != target or after['name'] != target or after['isDraft']
            or assets_of(before) != assets_of(after)):
        raise RuntimeError(f'Release verification failed for {target}; original tag retained')
    if target != tag:
        resolved = command('api', f'repos/{{owner}}/{{repo}}/commits/{target}', '--jq', '.sha')
        if resolved != commit:
            raise RuntimeError(f'Release commit changed for {target}; original tag retained')
        command('api', '--method', 'DELETE', 'repos/{owner}/{repo}/git/refs/tags/' + tag)
    print('Synchronized release:', target)


def main():
    releases = json.loads(gh('release', 'list', '--limit', '100', '--json', 'tagName'))
    tags = {r['tagName'] for r in releases}
    for release in releases:
        synchronize(release['tagName'], tags)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        message = str(error).replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print('::error::' + message, flush=True)
        raise
