# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import struct
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
        self.assertEqual(len(set(ENTRIES.ENTRY_POINTS)), 133)
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
            self.assertIn('std::array<uint32_t, 133> entries', content)
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

    def test_stream_callback_bank_preserves_reserved_slots(self):
        base, slots = ENTRIES.STREAM_CALLBACK_TABLES[0]
        self.assertEqual(base, 0x00367B68)
        self.assertEqual(slots[:3], (0, 0, 0))
        self.assertEqual(slots[3], 0x0016D310)
        self.assertEqual(len(slots), 12)
        self.assertEqual(len(ENTRIES.STREAM_CALLBACK_ENTRIES), 9)
        self.assertTrue(set(ENTRIES.STREAM_CALLBACK_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))

    def test_stream_cleanup_callback_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x0016d310'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x0016D310,))

    def test_sound_transfer_sdk_is_translated_with_its_exact_entry(self):
        text = ('[general]\nstubs = ["sceSdTransToIOP@0x00116268", "ret0@0x00200000", '
                '"sceSdTransToIOP@0x00200004"]\npatch_syscalls = false\n')
        output = ENTRIES.configure(text)
        self.assertNotIn('"sceSdTransToIOP@0x00116268"', output)
        self.assertIn('"ret0@0x00200000"', output)
        self.assertIn('"sceSdTransToIOP@0x00200004"', output)
        self.assertIn('"0x00116268"', output)
        self.assertIn('patch_syscalls = false', output)
        self.assertEqual(ENTRIES.configure(output), output)

    def test_unqualified_sound_transfer_stub_is_also_removed(self):
        output = ENTRIES.configure('[general]\nstubs = ["sceSdTransToIOP", "ret1"]\n')
        self.assertNotIn('"sceSdTransToIOP"', output)
        self.assertIn('"ret1"', output)

    def test_asset_relocation_handlers_preserve_tags_and_terminator(self):
        self.assertEqual(ENTRIES.ASSET_RELOCATION_BASE, 0x0033D680)
        self.assertEqual(len(ENTRIES.ASSET_RELOCATION_ENTRIES), 7)
        self.assertEqual([tag for _, tag in ENTRIES.ASSET_RELOCATION_HANDLERS],
                         [b'AMG\0', b'AMT\0', b'AMM\0', b'AMO\0', b'AMC\0', b'AML\0', b'AMP\0'])
        self.assertEqual(ENTRIES.ASSET_RELOCATION_WORDS[-2:], (0, 0))
        self.assertTrue(set(ENTRIES.ASSET_RELOCATION_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))

    def test_logged_asset_handler_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x0012ac30'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x0012AC30,))

    def make_elf_metadata(self):
        data = bytearray(0x290000 + 128)
        struct.pack_into('<I', data, 28, 52)
        struct.pack_into('<HH', data, 42, 32, 1)
        struct.pack_into('<8I', data, 52, 1, 128, 0x100000, 0, len(data)-128, len(data)-128, 5, 16)
        def put(address, values):
            struct.pack_into('<'+'I'*len(values), data, address-0x100000+128, *values)
        put(0x00385748, tuple(base for base, _ in ENTRIES.DISPATCH_TABLES))
        for base, entries in (ENTRIES.DISPATCH_TABLES + ENTRIES.FILE_CALLBACK_TABLES
                              + ENTRIES.STREAM_CALLBACK_TABLES + ENTRIES.FADE_CALLBACK_TABLES):
            put(base, entries)
        for address in ENTRIES.ZERO_RETURN_ENTRIES:
            put(address, (0x03E00008, 0x0000102D))
        put(ENTRIES.ASSET_RELOCATION_BASE, ENTRIES.ASSET_RELOCATION_WORDS)
        return data

    def test_asset_metadata_validation_rejects_changed_tags(self):
        data = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(data)
        data[ENTRIES.ASSET_RELOCATION_BASE-0x100000+128+4] ^= 1
        with self.assertRaisesRegex(ValueError, 'Asset relocation handlers or tags'):
            ENTRIES.verify_elf_metadata(data)

    def test_fade_callback_modes_preserve_shared_slots(self):
        base, slots = ENTRIES.FADE_CALLBACK_TABLES[0]
        self.assertEqual(base, 0x003786E0)
        self.assertEqual(len(slots), 20)
        self.assertEqual(slots[0], 0)
        self.assertEqual(slots[9:11], (0x00249210, 0x002492D0))
        self.assertEqual(len(ENTRIES.FADE_CALLBACK_ENTRIES), 4)
        self.assertEqual(slots.count(0x002487D0), 9)
        self.assertEqual(slots.count(0x00247CF0), 8)
        self.assertTrue(set(ENTRIES.FADE_CALLBACK_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))

    def test_logged_fade_renderer_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x002492d0'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x002492D0,))

    def test_fade_metadata_validation_rejects_changed_mode(self):
        data = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(data)
        base = ENTRIES.FADE_CALLBACK_TABLES[0][0]
        struct.pack_into('<I', data, base-0x100000+128+10*4, 0x00249210)
        with self.assertRaisesRegex(ValueError, 'Indirect methods differ.*003786e0'):
            ENTRIES.verify_elf_metadata(data)


if __name__ == '__main__':
    unittest.main()
