#!/usr/bin/env python3
"""Collect the most recent component reports for a bug report."""
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / 'reports/probes'
ARCHIVE = ROOT / 'reports/probe-diagnostics.zip'


def main():
    if not (REPORT / 'STATUS.json').is_file():
        print('No component reports found. Run tools/probes/run.py first.', file=sys.stderr)
        return 1
    with zipfile.ZipFile(ARCHIVE, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(REPORT.iterdir()):
            if (path.is_file() and path.suffix in ('.json', '.txt', '.log')
                    and path.stat().st_size < 2_000_000):
                archive.write(path, 'probes/' + path.name)
    print(ARCHIVE)
    return 0


if __name__ == '__main__':
    sys.exit(main())
