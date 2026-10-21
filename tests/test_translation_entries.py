# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'translation_entries', Path(__file__).resolve().parents[1] / 'tools/native/translation-entries.py')
ENTRIES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENTRIES)


class TranslationEntries(unittest.TestCase):
    def test_preserves_existing_entries_and_patch_policy(self):
        text = '[general]\npatch_cop0 = false\nentry_points = ["existing@0x00123400"]\n\n[patches]\ninstructions = []\n'
        output = ENTRIES.configure(text)
        self.assertIn('existing@0x00123400', output)
        self.assertIn('"0x00167188"', output)
        self.assertIn('"0x001908e0"', output)
        self.assertIn('[patches]\ninstructions = []', output)
        self.assertIn('patch_cop0 = false', output)

    def test_idempotent_and_recognizes_named_entry(self):
        text = '[general]\nentry_points = ["callback@0x00167188"]\n'
        output = ENTRIES.configure(text)
        self.assertEqual(output.count('0x00167188'), 1)
        self.assertEqual(ENTRIES.configure(output), output)

    def test_does_not_put_entries_in_another_table(self):
        output = ENTRIES.configure('[general]\ninput="game"\n[mmio]\nvalue=1\n')
        self.assertLess(output.index('entry_points'), output.index('[mmio]'))
        with self.assertRaisesRegex(ValueError, 'general'):
            ENTRIES.configure('[mmio]\nvalue=1\n')

    def verify_fixture(self, registry, source, complete_tables=True, omitted=()):
        if complete_tables:
            for index, address in enumerate(ENTRIES.ENTRY_POINTS[1:], 1):
                if address in omitted:
                    continue
                registry += f'\ng_ps2RecompiledFunctionTable[{index}] = owner; // 0x{address:x}'
                source += f'\ncase 0x{address:x}u: goto label_{address:x};\nlabel_{address:x}:\n'
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'register_functions.cpp').write_text(registry)
            (root / 'owner.cpp').write_text(source)
            ENTRIES.verify_generated(root)

    def test_missing_dispatch_entry_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry'):
            self.verify_fixture('', '// Address: 0x167110 - 0x1671a0')

    def test_binding_without_correct_resume_rejected(self):
        with self.assertRaisesRegex(ValueError, 'own instruction'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[105570] = owner; // 0x167188',
                                '// Address: 0x167110 - 0x1671a0\nctx->pc = 0x167110u;')

    def test_exact_resume_entry_accepted(self):
        self.verify_fixture('g_ps2RecompiledFunctionTable[105570] = owner; // 0x167188',
                            '// Address: 0x167110 - 0x1671a0\n'
                            'case 0x167188u: goto label_167188;\nlabel_167188:\n')

    def test_related_table_entry_must_also_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x001908e0'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', complete_tables=False)

    def test_reviewed_tables_have_complete_unique_coverage(self):
        self.assertEqual(len(ENTRIES.DISPATCH_TABLES), 7)
        self.assertTrue(all(len(row) == 14 for _, row in ENTRIES.DISPATCH_TABLES))
        self.assertEqual(len(set(ENTRIES.ENTRY_POINTS)), 112)
        self.assertEqual(len(ENTRIES.ZERO_RETURN_ENTRIES), 39)
        self.assertTrue(set(ENTRIES.ZERO_RETURN_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))
        self.assertNotIn(0x00387110, ENTRIES.ENTRY_POINTS)

    def test_named_table_entry_remains_idempotent(self):
        output = ENTRIES.configure('[general]\nentry_points = ["method@0x001908E0"]\n')
        self.assertEqual(output.lower().count('0x001908e0'), 1)
        self.assertEqual(ENTRIES.configure(output), output)

    def test_runtime_check_header_uses_reviewed_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'entries.h'
            ENTRIES.emit_check_header(path)
            content, changed = path.read_text(), path.stat().st_mtime_ns
            self.assertIn('std::array<uint32_t, 112> entries', content)
            self.assertIn('std::array<uint32_t, 39> zero_return_entries', content)
            for address in ENTRIES.ENTRY_POINTS:
                self.assertIn(f'0x{address:08x}u,', content)
            ENTRIES.emit_check_header(path)
            self.assertEqual(path.stat().st_mtime_ns, changed)

    def test_file_callback_bank_preserves_null_slots(self):
        self.assertEqual(len(ENTRIES.FILE_CALLBACK_TABLES), 1)
        base, slots = ENTRIES.FILE_CALLBACK_TABLES[0]
        self.assertEqual(base, 0x0035FB38)
        self.assertEqual(len(slots), 25)
        self.assertEqual(slots[0x60 // 4], 0x00167AF8)
        self.assertEqual(len(ENTRIES.FILE_CALLBACK_ENTRIES), 14)
        self.assertNotIn(0, ENTRIES.ENTRY_POINTS)
        self.assertEqual(ENTRIES.ENTRY_POINTS.count(0x00167188), 1)

    def test_file_query_callback_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x00167af8'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x00167AF8,))


if __name__ == '__main__':
    unittest.main()
