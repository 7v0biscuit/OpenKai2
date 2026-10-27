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

    def test_story_callbacks_are_registered_and_original_metadata_checked(self):
        self.assertEqual(len(ENTRIES.STORY_CALLBACK_ENTRIES),86)
        self.assertIn(0x295f40,ENTRIES.STORY_CALLBACK_ENTRIES)
        for address in ENTRIES.STORY_CALLBACK_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))
        for address in (0x37a6c0,0x37a6e4,0x37a81c):
            data=self.make_elf_metadata();data[address-0x100000+128]^=1
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                ENTRIES.verify_elf_metadata(data)
        for index in range(16):
            data=self.make_elf_metadata();data[0x295f40-0x100000+128+4*index]^=1
            with self.subTest(instruction=index),self.assertRaisesRegex(ValueError,'Story callback instructions'):
                ENTRIES.verify_elf_metadata(data)

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
        self.assertEqual(len(set(ENTRIES.ENTRY_POINTS)), 1025)
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
            self.assertIn('std::array<uint32_t, 1025> entries', content)
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

    def test_memory_stream_interface_and_all_methods_are_registered(self):
        base, slots = ENTRIES.MEMORY_STREAM_TABLES[0]
        self.assertEqual(base, 0x003676A8)
        self.assertEqual(slots[:4], (0, 0, 0, 0x0016CDA0))
        self.assertEqual(len(slots), 12)
        self.assertEqual(len(ENTRIES.MEMORY_STREAM_ENTRIES), 9)
        for entry in ENTRIES.MEMORY_STREAM_ENTRIES:
            with self.subTest(entry=entry), self.assertRaisesRegex(ValueError, f'missing guest entry 0x{entry:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0', omitted=(entry,))

    def test_memory_stream_metadata_rejects_changed_cleanup_method(self):
        data = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(data)
        data[0x003676B4-0x100000+128] ^= 1
        with self.assertRaisesRegex(ValueError, 'Indirect methods differ.*003676a8'):
            ENTRIES.verify_elf_metadata(data)

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

    def test_movie_sdk_setup_uses_original_routines(self):
        routines = (('sceMpegCreate', 0x107978), ('sceMpegReset', 0x107d18),
                    ('sceMpegClearRefBuff', 0x107d78), ('sceMpegAddCallback', 0x107dd0),
                    ('sceIpuSync', 0x109390))
        stubs = ', '.join(f'"{name}@0x{address:08x}"' for name, address in routines)
        output = ENTRIES.configure('[general]\nstubs = ['+stubs+', "sceMpegGetPicture"]\n')
        for name, address in routines:
            self.assertNotIn(f'"{name}@0x{address:08x}"', output)
            self.assertIn(f'"0x{address:08x}"', output)
        self.assertIn('"sceMpegGetPicture"', output)
        self.assertEqual(ENTRIES.configure(output), output)

    def test_mpeg_constructor_must_have_its_own_instruction_entry(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x00107978'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x00107978,))

    def test_asset_relocation_handlers_preserve_tags_and_terminator(self):
        self.assertEqual(ENTRIES.ASSET_RELOCATION_BASE, 0x0033D680)
        self.assertEqual(len(ENTRIES.ASSET_RELOCATION_ENTRIES), 7)
        self.assertEqual([tag for _, tag in ENTRIES.ASSET_RELOCATION_HANDLERS],
                         [b'AMG\0', b'AMT\0', b'AMM\0', b'AMO\0', b'AMC\0', b'AML\0', b'AMP\0'])
        self.assertEqual(ENTRIES.ASSET_RELOCATION_WORDS[-2:], (0, 0))
        self.assertTrue(set(ENTRIES.ASSET_RELOCATION_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))

    def test_logged_draw_setup_callback_requires_its_own_entry(self):
        self.assertEqual(len(ENTRIES.DRAW_SETUP_ENTRIES), 8)
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x001a9380'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x001A9380,))

    def test_draw_setup_metadata_rejects_changed_delay_slot(self):
        data=self.make_elf_metadata()
        struct.pack_into('<I', data, 0x001A9388-0x100000+128, 0)
        with self.assertRaisesRegex(ValueError, 'Draw setup instructions differ'):
            ENTRIES.verify_elf_metadata(data)

    def test_logged_versus_animation_requires_its_own_entry(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x002aeac0'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(ENTRIES.VERSUS_ANIMATION_ENTRY,))

    def test_versus_animation_metadata_rejects_changed_tail_store(self):
        data=self.make_elf_metadata()
        address=ENTRIES.VERSUS_ANIMATION_ENTRY+4*(len(ENTRIES.VERSUS_ANIMATION_WORDS)-1)
        struct.pack_into('<I', data, address-0x100000+128, 0)
        with self.assertRaisesRegex(ValueError, 'Versus animation callback differs'):
            ENTRIES.verify_elf_metadata(data)

    def test_logged_asset_handler_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x0012ac30'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x0012AC30,))

    def test_battle_stages_require_exact_resume_entries(self):
        self.assertEqual(len(ENTRIES.BATTLE_INIT_ENTRIES),11)
        for address in ENTRIES.BATTLE_INIT_ENTRIES:
            with self.subTest(address=address), self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0', omitted=(address,))

    def test_pause_menu_code_and_tables_reject_modifications(self):
        for base,words in ENTRIES.PAUSE_MENU_WORDS:
            for i in (0,len(words)-1):
                data=self.make_elf_metadata();data[base-0x100000+128+i*4]^=1
                with self.subTest(base=base,i=i),self.assertRaisesRegex(ValueError,'Pause menu instructions differ'):
                    ENTRIES.verify_elf_metadata(data)
        for base,row in ENTRIES.PAUSE_MENU_TABLES:
            data=self.make_elf_metadata();data[base-0x100000+128+16]^=1
            with self.subTest(base=base),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                ENTRIES.verify_elf_metadata(data)

    def test_script_command_banks_and_instructions(self):
        self.assertIn(0x27ffc0, ENTRIES.SCRIPT_COMMAND_ENTRIES)
        self.assertIn(0x1fad00, ENTRIES.SCRIPT_COMMAND_ENTRIES)
        data=self.make_elf_metadata()
        for address,words in ENTRIES.SCRIPT_COMMAND_WORDS:
            struct.pack_into('<'+'I'*len(words),data,address-0x100000+128,*words)
        ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.SCRIPT_COMMAND_TABLES+ENTRIES.SCRIPT_COMMAND_ROOTS+ENTRIES.SCRIPT_COMMAND_WORDS:
            edited=bytearray(data);edited[address-0x100000+128]^=1
            with self.assertRaisesRegex(ValueError,'Indirect methods|Script command'):
                ENTRIES.verify_elf_metadata(edited)

    def test_interaction_command_banks(self):
        self.assertEqual(len(ENTRIES.INTERACTION_ENTRIES), 31)
        self.assertEqual(ENTRIES.INTERACTION_TABLES[0][1][21], 0x275fe0)
        self.assertTrue(set(ENTRIES.INTERACTION_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))
        original = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(original)
        for pc, words in ENTRIES.INTERACTION_TABLES + ENTRIES.INTERACTION_WORDS:
            for index in (0, len(words)-1):
                data = bytearray(original); data[pc-0x100000+128+index*4] ^= 1
                with self.subTest(pc=pc, index=index), self.assertRaisesRegex(ValueError, 'Indirect methods|Interaction command'):
                    ENTRIES.verify_elf_metadata(data)

    def test_object_callback_installations(self):
        self.assertEqual(len(ENTRIES.OBJECT_CALLBACK_ENTRIES), 79)
        self.assertIn(0x225930, ENTRIES.OBJECT_CALLBACK_ENTRIES)
        self.assertNotIn(0x33c440, ENTRIES.OBJECT_CALLBACK_ENTRIES)
        self.assertNotIn(0x33c500, ENTRIES.OBJECT_CALLBACK_ENTRIES)
        self.assertTrue(set(ENTRIES.OBJECT_CALLBACK_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))
        original = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(original)
        for _, high_pc, _, low_pc, _, store_pc, _ in ENTRIES.OBJECT_CALLBACK_INSTALLS:
            for pc in (high_pc, low_pc, store_pc):
                data = bytearray(original); data[pc-0x100000+128] ^= 1
                with self.subTest(pc=pc), self.assertRaisesRegex(ValueError, 'Object callback|Actor timing instructions|Impact effect instructions'):
                    ENTRIES.verify_elf_metadata(data)
        for pc, words in ENTRIES.OBJECT_CALLBACK_WORDS:
            for index in (0, len(words)-1):
                data = bytearray(original); data[pc-0x100000+128+index*4] ^= 1
                with self.subTest(pc=pc, index=index), self.assertRaisesRegex(ValueError, 'Object callback|Actor timing instructions|Impact effect instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_actor_state_bank_is_registered(self):
        self.assertEqual(len(ENTRIES.ACTOR_STATE_ENTRIES),13)
        self.assertEqual(ENTRIES.ACTOR_STATE_TABLES[0][1][4],0x1f9290)
        self.assertEqual(ENTRIES.ACTOR_STATE_TABLES[0][1][-3:],(0,0,0))
        for target in ENTRIES.ACTOR_STATE_ENTRIES:
            with self.assertRaisesRegex(ValueError,'missing guest entry'):
                self.verify_fixture('', '// Address: 0x167188 - 0x1671a0', omitted=(target,))

    def test_actor_state_tables_and_complete_methods_are_checked(self):
        data=self.make_elf_metadata();ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.ACTOR_STATE_WORDS+ENTRIES.ACTOR_STATE_TABLES:
            for index in {0,len(words)-1}:
                edited=bytearray(data);edited[address-0x100000+128+index*4]^=1
                with self.assertRaisesRegex(ValueError,'Actor state|Indirect methods'):
                    ENTRIES.verify_elf_metadata(edited)

    def test_actor_timing_bank_and_continuations_are_registered(self):
        self.assertEqual(len(ENTRIES.ACTOR_TIMING_ENTRIES),12)
        self.assertEqual(ENTRIES.ACTOR_TIMING_TABLES[0][1][-3:],(0x1fc1e0,0,0))
        self.assertTrue(set(ENTRIES.ACTOR_TIMING_ENTRIES)<=set(ENTRIES.ENTRY_POINTS))

    def test_actor_timing_metadata_rejects_changed_methods_and_catalog(self):
        data=self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.ACTOR_TIMING_WORDS+ENTRIES.ACTOR_TIMING_TABLES:
            for offset in (0,len(words)-1):
                changed=bytearray(data)
                struct.pack_into('<I',changed,address-0x100000+128+offset*4,words[offset]^1)
                with self.assertRaises(ValueError):ENTRIES.verify_elf_metadata(changed)

    def test_pause_menu_bank_and_continuations_are_registered(self):
        self.assertEqual(len(ENTRIES.PAUSE_MENU_TABLES),21)
        self.assertEqual(ENTRIES.PAUSE_MENU_TABLES[13][0],0x374aa8)
        self.assertEqual(ENTRIES.PAUSE_MENU_TABLES[13][1][4],0x1c0bd0)
        self.assertEqual(len(ENTRIES.PAUSE_MENU_ENTRIES),12)
        self.assertEqual(ENTRIES.PAUSE_MENU_TABLES[-1][1][4:],(0,0,0,0,0,0))
        self.assertTrue(set(ENTRIES.PAUSE_MENU_ENTRIES)<=set(ENTRIES.ENTRY_POINTS))
        for at in (0x1c0bd0,0x1c22f0):
            self.assertTrue(any(base==at and words[0]==0x27bdfff0 for base,words in ENTRIES.PAUSE_MENU_WORDS))

    def test_actor_command_bank_requires_every_callable_slot(self):
        self.assertEqual(len(ENTRIES.ACTOR_COMMAND_TABLES[0][1]),40)
        self.assertEqual(ENTRIES.ACTOR_COMMAND_TABLES[0][1][17],0x1fb010)
        self.assertEqual(len(ENTRIES.ACTOR_COMMAND_ENTRIES),40)
        for address in ENTRIES.ACTOR_COMMAND_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_actor_command_metadata_verifies_bank_methods_and_caller(self):
        for address,words in ENTRIES.ACTOR_COMMAND_WORDS:
            for index in {0,len(words)//2,len(words)-1}:
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Actor command instructions|Script command instructions'):
                    ENTRIES.verify_elf_metadata(data)
        for index in range(40):
            data=self.make_elf_metadata();data[0x375550-0x100000+128+index*4]^=1
            with self.subTest(slot=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                ENTRIES.verify_elf_metadata(data)

    def test_impact_effect_entries_are_required(self):
        self.assertEqual(tuple(len(v) for _,v in ENTRIES.IMPACT_EFFECT_TABLES),(8,8,8,8,4,4))
        self.assertEqual(len(ENTRIES.IMPACT_EFFECT_ENTRIES),34)
        for address in ENTRIES.IMPACT_EFFECT_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_impact_effect_metadata_checks_tables_and_complete_methods(self):
        for address,words in ENTRIES.IMPACT_EFFECT_WORDS:
            for index in range(len(words)):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Impact effect instructions'):
                    ENTRIES.verify_elf_metadata(data)
        for base,slots in ENTRIES.IMPACT_EFFECT_TABLES:
            for index in range(len(slots)):
                data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
                with self.subTest(base=base,index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                    ENTRIES.verify_elf_metadata(data)

    def test_attack_effect_entries_are_required(self):
        self.assertEqual(len(ENTRIES.ATTACK_EFFECT_TABLES[0][1]),20)
        self.assertEqual(len(ENTRIES.ATTACK_EFFECT_ENTRIES),16)
        for address in ENTRIES.ATTACK_EFFECT_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_attack_effect_metadata_checks_every_instruction(self):
        for address,words in ENTRIES.ATTACK_EFFECT_WORDS:
            for index in range(len(words)):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Attack effect instructions'):
                    ENTRIES.verify_elf_metadata(data)
        base,slots=ENTRIES.ATTACK_EFFECT_TABLES[0]
        for index in range(len(slots)):
            data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
            with self.subTest(index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                ENTRIES.verify_elf_metadata(data)

    def test_fight_state_tables_require_every_original_entry(self):
        self.assertEqual(tuple(len(slots) for _,slots in ENTRIES.FIGHT_STATE_TABLES),(8,8,16))
        self.assertEqual({x for _,slots in ENTRIES.FIGHT_STATE_TABLES for x in slots}-{0},set(ENTRIES.FIGHT_STATE_ENTRIES))
        for address in ENTRIES.FIGHT_STATE_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_fight_state_metadata_checks_tables_and_complete_handlers(self):
        for base,slots in ENTRIES.FIGHT_STATE_TABLES:
            for index in range(len(slots)):
                data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
                with self.subTest(base=base,index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                    ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.FIGHT_STATE_WORDS:
            for index in range(len(words)):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Fight state instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_battle_draw_tables_require_every_original_entry(self):
        self.assertEqual(tuple(len(slots) for _,slots in ENTRIES.BATTLE_DRAW_TABLES),(16,14))
        self.assertEqual({x for _,slots in ENTRIES.BATTLE_DRAW_TABLES for x in slots}-{0},set(ENTRIES.BATTLE_DRAW_ENTRIES))
        for address in ENTRIES.BATTLE_DRAW_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_battle_draw_metadata_checks_tables_and_complete_handlers(self):
        for base,slots in ENTRIES.BATTLE_DRAW_TABLES:
            for index in range(len(slots)):
                data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
                with self.subTest(base=base,index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                    ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.BATTLE_DRAW_WORDS:
            for index in range(len(words)):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Battle draw instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_battle_update_tables_require_every_original_entry(self):
        self.assertEqual(len(ENTRIES.BATTLE_UPDATE_ENTRIES),18)
        self.assertEqual({x for _,slots in ENTRIES.BATTLE_UPDATE_TABLES for x in slots}-{0},set(ENTRIES.BATTLE_UPDATE_ENTRIES))
        self.assertTrue(all(len(slots)==16 for _,slots in ENTRIES.BATTLE_UPDATE_TABLES))
        for address in ENTRIES.BATTLE_UPDATE_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_battle_update_metadata_checks_tables_and_complete_handlers(self):
        for base,slots in ENTRIES.BATTLE_UPDATE_TABLES:
            for index in range(len(slots)):
                data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
                with self.subTest(base=base,index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                    ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.BATTLE_UPDATE_WORDS:
            for index in range(len(words)):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Battle update instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_aura_tables_require_every_original_entry(self):
        self.assertEqual(len(ENTRIES.AURA_UPDATE_ENTRIES),6)
        self.assertEqual({x for _,slots in ENTRIES.AURA_UPDATE_TABLES for x in slots}-{0},set(ENTRIES.AURA_UPDATE_ENTRIES))
        self.assertTrue(all(slots[-1]==0 for _,slots in ENTRIES.AURA_UPDATE_TABLES))
        for address in ENTRIES.AURA_UPDATE_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_aura_metadata_checks_tables_and_complete_handlers(self):
        for base,slots in ENTRIES.AURA_UPDATE_TABLES:
            for index in range(len(slots)):
                data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
                with self.subTest(base=base,index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                    ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.AURA_UPDATE_WORDS:
            for index in range(len(words)):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Aura update instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_battle_commands_have_complete_registration(self):
        self.assertEqual(len(ENTRIES.BATTLE_COMMAND_TABLES[0][1]),45)
        self.assertEqual(set(ENTRIES.BATTLE_COMMAND_ENTRIES),set(ENTRIES.BATTLE_COMMAND_TABLES[0][1])-{0})
        for address in ENTRIES.BATTLE_COMMAND_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_battle_command_metadata_and_delay_slots_are_verified(self):
        for base,slots in ENTRIES.BATTLE_COMMAND_TABLES:
            for index in range(len(slots)):
                data=self.make_elf_metadata();data[base-0x100000+128+index*4]^=1
                with self.subTest(base=base,index=index),self.assertRaisesRegex(ValueError,'Indirect methods differ'):
                    ENTRIES.verify_elf_metadata(data)
        for address,words in ENTRIES.BATTLE_COMMAND_WORDS:
            for index in (0,len(words)-1):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Battle command instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_scene_modes_require_exact_entries(self):
        self.assertEqual(set(ENTRIES.SCENE_MODE_ENTRIES),{0x1b2c60,0x1b2da0,0x1b2e30,0x1b3070,0x1b35b0,0x1b38f0,0x1b3ae0})
        for address in ENTRIES.SCENE_MODE_ENTRIES:
            with self.subTest(address=address),self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0',omitted=(address,))

    def test_scene_modes_verify_instructions_and_table_nulls(self):
        for address,words in ENTRIES.SCENE_MODE_WORDS:
            for index in (0,len(words)-1):
                data=self.make_elf_metadata();data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Scene mode instructions'):
                    ENTRIES.verify_elf_metadata(data)
        for index in range(16):
            data=self.make_elf_metadata();data[0x373a60-0x100000+128+index*4]^=1
            with self.subTest(slot=index),self.assertRaisesRegex(ValueError,'Indirect methods differ.*00373a60'):
                ENTRIES.verify_elf_metadata(data)

    def test_battle_asset_handlers_both_require_registration(self):
        self.assertEqual(set(ENTRIES.BATTLE_ASSET_ENTRIES),{0x1d21d0,0x1d2100})
        for address in ENTRIES.BATTLE_ASSET_ENTRIES:
            with self.subTest(address=address), self.assertRaisesRegex(ValueError,f'missing guest entry 0x{address:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0', omitted=(address,))

    def test_battle_asset_table_tag_and_terminator_changes_rejected(self):
        for offset in (0,4,16,20,32):
            data=self.make_elf_metadata()
            data[ENTRIES.BATTLE_ASSET_TABLE_BASE-0x100000+128+offset]^=1
            with self.subTest(offset=offset),self.assertRaisesRegex(ValueError,'Battle asset handlers or tags'):
                ENTRIES.verify_elf_metadata(data)

    def test_battle_asset_instructions_and_delay_slots_are_verified(self):
        for address,words in ENTRIES.BATTLE_ASSET_WORDS:
            for index in (0,len(words)-1):
                data=self.make_elf_metadata()
                data[address-0x100000+128+index*4]^=1
                with self.subTest(address=address,index=index),self.assertRaisesRegex(ValueError,'Battle asset relocation instructions'):
                    ENTRIES.verify_elf_metadata(data)

    def test_battle_stage_table_change_rejected(self):
        data=self.make_elf_metadata()
        data[0x375234-0x100000+128]^=1
        with self.assertRaisesRegex(ValueError,'Indirect methods differ.*00375230'):
            ENTRIES.verify_elf_metadata(data)

    def test_battle_initializer_return_delay_slot_change_rejected(self):
        data=self.make_elf_metadata()
        struct.pack_into('<I',data,0x1b2748-0x100000+128,0)
        with self.assertRaisesRegex(ValueError,'Battle initialization instructions differ'):
            ENTRIES.verify_elf_metadata(data)

    def make_elf_metadata(self):
        data = bytearray(0x2a4900 + 128)
        struct.pack_into('<I', data, 28, 52)
        struct.pack_into('<HH', data, 42, 32, 1)
        struct.pack_into('<8I', data, 52, 1, 128, 0x100000, 0, len(data)-128, len(data)-128, 5, 16)
        def put(address, values):
            struct.pack_into('<'+'I'*len(values), data, address-0x100000+128, *values)
        put(0x00385748, tuple(base for base, _ in ENTRIES.DISPATCH_TABLES))
        for base, entries in (ENTRIES.DISPATCH_TABLES + ENTRIES.FILE_CALLBACK_TABLES
                              + ENTRIES.STREAM_CALLBACK_TABLES + ENTRIES.MEMORY_STREAM_TABLES + ENTRIES.FADE_CALLBACK_TABLES
                              + ENTRIES.MOVIE_CALLBACK_TABLES + ENTRIES.MOVIE_PACKET_TABLES
                              + ENTRIES.MOVIE_TIMING_TABLES + ENTRIES.SCENE_MODE_TABLES + ENTRIES.BATTLE_COMMAND_TABLES + ENTRIES.AURA_UPDATE_TABLES + ENTRIES.BATTLE_UPDATE_TABLES + ENTRIES.BATTLE_DRAW_TABLES + ENTRIES.FIGHT_STATE_TABLES + ENTRIES.ATTACK_EFFECT_TABLES + ENTRIES.IMPACT_EFFECT_TABLES + ENTRIES.ACTOR_COMMAND_TABLES + ENTRIES.ACTOR_TIMING_TABLES + ENTRIES.ACTOR_STATE_TABLES + ENTRIES.PAUSE_MENU_TABLES + ENTRIES.STORY_CALLBACK_TABLES + ENTRIES.SCRIPT_COMMAND_TABLES + ENTRIES.SCRIPT_COMMAND_ROOTS + ENTRIES.INTERACTION_TABLES):
            put(base, entries)
        for _, high_pc, high_word, low_pc, low_word, store_pc, store_word in ENTRIES.OBJECT_CALLBACK_INSTALLS:
            for pc, word in ((high_pc, high_word), (low_pc, low_word), (store_pc, store_word)):
                put(pc, (word,))
        put(0x00387420, ENTRIES.MOVIE_TIMING_RATES)
        for address in ENTRIES.ZERO_RETURN_ENTRIES:
            put(address, (0x03E00008, 0x0000102D))
        for address, words in ENTRIES.DRAW_SETUP_WORDS:
            put(address, words)
        put(ENTRIES.VERSUS_ANIMATION_ENTRY,ENTRIES.VERSUS_ANIMATION_WORDS)
        put(ENTRIES.BATTLE_INIT_ENTRIES[0],ENTRIES.BATTLE_INIT_WORDS)
        for base, slots in ENTRIES.BATTLE_INIT_TABLES:
            put(base,slots)
        put(ENTRIES.BATTLE_ASSET_TABLE_BASE,ENTRIES.BATTLE_ASSET_TABLE_WORDS)
        for address,words in ENTRIES.BATTLE_ASSET_WORDS:
            put(address,words)
        for address,words in ENTRIES.ACTOR_TIMING_WORDS+ENTRIES.ACTOR_STATE_WORDS+ENTRIES.SCRIPT_COMMAND_WORDS+ENTRIES.OBJECT_CALLBACK_WORDS+ENTRIES.INTERACTION_WORDS+ENTRIES.PROJECTION_WORDS:
            put(address,words)
        for address,words in ENTRIES.STORY_CALLBACK_WORDS:
            put(address,words)
        for address,words in ENTRIES.PAUSE_MENU_WORDS:
            put(address,words)
        for address,words in ENTRIES.ACTOR_COMMAND_WORDS:
            put(address,words)
        for address,words in ENTRIES.IMPACT_EFFECT_WORDS:
            put(address,words)
        for address,words in ENTRIES.ATTACK_EFFECT_WORDS:
            put(address,words)
        for address,words in ENTRIES.FIGHT_STATE_WORDS:
            put(address,words)
        for address,words in ENTRIES.BATTLE_DRAW_WORDS:
            put(address,words)
        for address,words in ENTRIES.BATTLE_UPDATE_WORDS:
            put(address,words)
        for address,words in ENTRIES.AURA_UPDATE_WORDS:
            put(address,words)
        for address,words in ENTRIES.BATTLE_COMMAND_WORDS:
            put(address,words)
        for address,words in ENTRIES.SCENE_MODE_WORDS:
            put(address,words)
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

    def test_movie_interface_preserves_reserved_and_volume_slots(self):
        base, slots = ENTRIES.MOVIE_CALLBACK_TABLES[0]
        self.assertEqual(base, 0x00370AE0)
        self.assertEqual(slots[:3], (0, 0, 0))
        self.assertEqual(slots[-1], 0)
        self.assertEqual(slots[0x2c//4], 0x00186E88)
        self.assertEqual(len(ENTRIES.MOVIE_CALLBACK_ENTRIES), 14)
        self.assertTrue(set(ENTRIES.MOVIE_CALLBACK_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))

    def test_logged_movie_volume_method_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x00186e88'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x00186E88,))

    def test_movie_packet_types_have_complete_coverage(self):
        self.assertEqual(ENTRIES.MOVIE_PACKET_TABLES,
                         ((0x00387180, (0x00191218, 0x00191390, 0x001915F0, 0x00191880)),))
        self.assertTrue(set(ENTRIES.MOVIE_PACKET_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))
        output = ENTRIES.configure('[general]\nentry_points = ["packet@0x001915f0"]\n')
        self.assertEqual(output.count('0x001915f0'), 1)
        self.assertEqual(ENTRIES.configure(output), output)

    def test_logged_movie_packet_callback_must_be_registered(self):
        with self.assertRaisesRegex(ValueError, 'missing guest entry 0x001915f0'):
            self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                '// Address: 0x167188 - 0x1671a0', omitted=(0x001915F0,))

    def test_movie_packet_metadata_rejects_a_changed_handler(self):
        data = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(data)
        struct.pack_into('<I', data, 0x00387188-0x100000+128, 0x00191880)
        with self.assertRaisesRegex(ValueError, 'Indirect methods differ.*00387180'):
            ENTRIES.verify_elf_metadata(data)

    def test_movie_metadata_validation_rejects_changed_volume_method(self):
        data = self.make_elf_metadata()
        ENTRIES.verify_elf_metadata(data)
        base = ENTRIES.MOVIE_CALLBACK_TABLES[0][0]
        struct.pack_into('<I', data, base-0x100000+128+0x2c, 0x00186EE0)
        with self.assertRaisesRegex(ValueError, 'Indirect methods differ.*00370ae0'):
            ENTRIES.verify_elf_metadata(data)

    def test_movie_timing_covers_rates_and_both_modes(self):
        base, slots = ENTRIES.MOVIE_TIMING_TABLES[0]
        self.assertEqual(base, 0x00387448)
        self.assertEqual(len(slots), len(ENTRIES.MOVIE_TIMING_RATES)*2)
        self.assertEqual(slots[:2], (0, 0))
        self.assertEqual(slots[8:10], (0x0019D4F0, 0x0019D5E0))
        self.assertEqual(len(ENTRIES.MOVIE_TIMING_ENTRIES), 7)
        self.assertTrue(set(ENTRIES.MOVIE_TIMING_ENTRIES) <= set(ENTRIES.ENTRY_POINTS))
        output = ENTRIES.configure('[general]\nentry_points = ["timing@0x0019d4f0"]\n')
        self.assertEqual(output.count('0x0019d4f0'), 1)
        self.assertEqual(ENTRIES.configure(output), output)

    def test_every_movie_timing_handler_must_be_registered(self):
        for entry in ENTRIES.MOVIE_TIMING_ENTRIES:
            with self.subTest(entry=entry), self.assertRaisesRegex(ValueError, f'missing guest entry 0x{entry:08x}'):
                self.verify_fixture('g_ps2RecompiledFunctionTable[1] = owner; // 0x167188',
                                    '// Address: 0x167188 - 0x1671a0', omitted=(entry,))

    def test_movie_timing_metadata_rejects_changed_rate_or_handler(self):
        for address, message in ((0x00387430, 'Movie timing rates'),
                                 (0x00387468, 'Indirect methods differ.*00387448')):
            with self.subTest(address=address):
                data = self.make_elf_metadata()
                ENTRIES.verify_elf_metadata(data)
                data[address-0x100000+128] ^= 1
                with self.assertRaisesRegex(ValueError, message):
                    ENTRIES.verify_elf_metadata(data)


if __name__ == '__main__':
    unittest.main()
