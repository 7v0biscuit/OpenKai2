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

    def verify_fixture(self, registry, source):
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


if __name__ == '__main__':
    unittest.main()
