"""The select-screen edits change only the listed translated instructions."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('patch_select_screen', ROOT / 'tools/native/patch-select-screen.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def translated(edits):
    """A translated file holding each edited instruction, padded by others."""
    lines = ['#include "ps2_runtime.h"', 'void function() {']
    for _, address, original, _ in edits:
        lines += [f'    // 0x{address:x}: 0x0  original', f'    ctx->pc = 0x{address:x}u;',
                  f'    STATEMENT({original}',
                  f'    // 0x{address + 2:x}: 0x0  unrelated', '    UNCHANGED(30), 29), 1652));']
    return '\n'.join(lines + ['}']) + '\n'


class PatchSelectScreen(unittest.TestCase):
    def files(self):
        grouped = {}
        for edit in module.EDITS:
            grouped.setdefault(edit[0], []).append(edit)
        return grouped

    def test_every_edit_applies_once_and_again_is_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, edits in self.files().items():
                (root / f'{name}.cpp').write_text(translated(edits))
            arguments = ['--directory', str(root)]
            module.main(arguments)
            once = {path.name: path.read_text() for path in root.iterdir()}
            module.main(arguments)
            self.assertEqual(once, {path.name: path.read_text() for path in root.iterdir()})
            for name, edits in self.files().items():
                text = once[f'{name}.cpp']
                self.assertEqual(text.count(module.MARKER), 1)
                self.assertEqual(text.count('UNCHANGED(30), 29), 1652));'), len(edits))
                for _, _, _, replacement in edits:
                    self.assertIn(replacement, text)

    def test_a_file_patched_by_an_earlier_version_gains_the_new_edits(self):
        # An older version made only the first edit in this file.
        edits = self.files()['sub_002AF000_0x2af000']
        text = translated(edits).replace(edits[0][2], edits[0][3], 1)
        text = module.INCLUDE + module.MARKER + '\n' + text
        patched = module.patch(text, edits)
        self.assertEqual(patched.count(module.MARKER), 1)
        for _, _, _, replacement in edits:
            self.assertIn(replacement, patched)
        self.assertEqual(patched.count('preview_row(GPR_U32(ctx, 18))'), 4)

    def test_the_offset_edits_cover_the_fields_after_the_cells(self):
        offsets = [edit for edit in module.EDITS if 'select_offset' in edit[3]]
        self.assertEqual(len(offsets), 19)
        self.assertNotIn(0x2aec28, [edit[1] for edit in module.EDITS])  # a preview-table row, not a cell

    def test_a_changed_or_missing_instruction_is_refused(self):
        name, edits = next(iter(self.files().items()))
        text = translated(edits)
        with self.assertRaises(ValueError):
            module.patch(text.replace(edits[0][2], 'SOMETHING_ELSE)'), edits)
        comment = f'    // 0x{edits[0][1]:x}:'
        with self.assertRaises(ValueError):
            module.patch(text.replace(comment, '    // 0xdead:'), edits)


if __name__ == '__main__':
    unittest.main()
