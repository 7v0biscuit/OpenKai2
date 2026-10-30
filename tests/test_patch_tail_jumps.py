"""Listed tail jumps go through the function table; nothing else changes."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('patch_tail_jumps', ROOT / 'tools/native/patch-tail-jumps.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

TRANSLATED = '''    // 0x1d8230: 0x807d2bc  j           func_1F4AF0
    ctx->pc = 0x1D8230u;
    ctx->pc = 0x1D8234u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D8230u;
    WRITE32(ADD32(GPR_U32(ctx, 1), 4294941652), GPR_U32(ctx, 2));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1F4AF0u;
    sub_001F4AF0_0x1f4af0(rdram, ctx, runtime); return;
    ctx->pc = 0x1D8238u;
'''


class PatchTailJumps(unittest.TestCase):
    def test_the_jump_is_dispatched_once_and_again_is_kept(self):
        first = module.patched(TRANSLATED, 0x1D8230, 0x1F4AF0)
        self.assertNotIn('sub_001F4AF0_0x1f4af0(rdram, ctx, runtime)', first)
        self.assertIn('dispatchGuestBranch(rdram, ctx, 0x1F4AF0u, 0x1D8230u, 0x0u, '
                      'PS2Runtime::GuestBranchKind::DirectJump, "J")', first)
        self.assertIn('WRITE32(ADD32(GPR_U32(ctx, 1), 4294941652), GPR_U32(ctx, 2));', first)
        self.assertEqual(module.patched(first, 0x1D8230, 0x1F4AF0), first)

    def test_a_file_without_the_jump_is_left_alone(self):
        self.assertIsNone(module.patched(TRANSLATED, 0x1D8300, 0x1F4AF0))

    def test_an_unexpected_call_is_refused(self):
        with self.assertRaises(ValueError):
            module.patched(TRANSLATED.replace('sub_001F4AF0', 'sub_001F4B00'), 0x1D8230, 0x1F4AF0)


if __name__ == '__main__':
    unittest.main()
