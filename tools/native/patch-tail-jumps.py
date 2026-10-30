#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Send listed tail jumps through the runtime's function table.

The recompiler turns a `j` to a translated function into a direct C++ call.
That is the same jump, but it bypasses the table, so a callback registered
there in place of the target is never reached. The jumps listed here go
through `dispatchGuestBranch`, as the recompiler emits for jumps to targets
it does not translate, which runs the same function unless one replaced it.
"""
import argparse
from pathlib import Path
import re

# (jump instruction, target): the melee entry's tail call into its reset.
TAIL_JUMPS = ((0x001D8230, 0x001F4AF0),)


def dispatched(branch, target):
    return (f'    ctx->pc = 0x{target:X}u;\n'
            f'    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x{target:X}u, 0x{branch:X}u, 0x0u, '
            f'PS2Runtime::GuestBranchKind::DirectJump, "J")) {{\n'
            f'        return;\n'
            f'    }}\n'
            f'    return;\n')


def patched(source, branch, target):
    """The source with the jump at `branch` dispatched, or None when the
    source holds no such jump. Raises when the jump is there but its call is
    not the one the recompiler emits."""
    marker = f'    ctx->branch_pc = 0x{branch:X}u;\n'
    at = source.find(marker)
    if at < 0:
        return None
    direct = re.compile(rf'    ctx->pc = 0x{target:X}u;\n    sub_0*{target:X}_0x{target:x}\(rdram, ctx, runtime\); return;\n')
    found = direct.search(source, at, at + 1200)
    if found:
        return source[:found.start()] + dispatched(branch, target) + source[found.end():]
    if dispatched(branch, target) in source[at:at + 1200]:
        return source
    raise ValueError(f'The jump at 0x{branch:x} does not call 0x{target:x} as expected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    files = sorted(args.directory.glob('*.cpp'))
    for branch, target in TAIL_JUMPS:
        done = False
        for path in files:
            original = path.read_text()
            updated = patched(original, branch, target)
            if updated is None:
                continue
            if updated != original:
                path.write_text(updated)
            done = True
            break
        if not done:
            raise SystemExit(f'No translated jump at 0x{branch:x}')
    print(f'Dispatched {len(TAIL_JUMPS)} tail jumps through the function table.')


if __name__ == '__main__':
    main()
