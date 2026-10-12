import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/windows/behaviour_timing.py"

HEAD = """label_8016E0AC:
    ctx->pc = 0x8016E0ACu;
    cycle_block_prepaid = dolrecomp_block_can_precharge(ctx, 3u);
    if (ctx->downcount <= -(s64)DOLRECOMP_C_LOOP_CYCLE_BUDGET) {
        ctx->pc = 0x8016E0ACu;
        return;
    }
    ctx->downcount -= cycle_block_prepaid ? 3u : 1u;
    if (cycle_block_prepaid) goto bwfast_161;
    // 8016E0AC: addi    r3, r1, 288
"""
# A block with no prepaid copy keeps its precharge test.
NO_COPY = """label_8016E0C4:
    ctx->pc = 0x8016E0C4u;
    cycle_block_prepaid = dolrecomp_block_can_precharge(ctx, 2u);
    if (ctx->downcount <= -(s64)DOLRECOMP_C_LOOP_CYCLE_BUDGET) {
        ctx->pc = 0x8016E0C4u;
        return;
    }
    ctx->downcount -= cycle_block_prepaid ? 2u : 1u;
    // 8016E0C4: li      r4, -1
"""


class BehaviourTimingTest(unittest.TestCase):
    def run_script(self, root):
        return subprocess.run([sys.executable, SCRIPT, root], check=True, capture_output=True, text=True).stdout

    def test_prepaid_heads_and_repeatable(self):
        with tempfile.TemporaryDirectory() as tmp:
            chunk = Path(tmp) / "chunks_dol" / "chunk_0091.c"
            chunk.parent.mkdir()
            chunk.write_text(HEAD + NO_COPY)
            self.assertIn("1 block starts in 1 chunks", self.run_script(tmp))
            text = chunk.read_text()
            self.assertIn("    cycle_block_prepaid = true;\n    ctx->downcount -= 3u;\n    goto bwfast_161;\n", text)
            head = text.split("label_8016E0C4:")[0]
            self.assertNotIn("dolrecomp_block_can_precharge", head)
            # The exit still stores the program counter; the head does not.
            self.assertEqual(head.count("ctx->pc = 0x8016E0ACu;"), 1)
            self.assertIn("dolrecomp_block_can_precharge(ctx, 2u)", text)
            self.assertIn("0 block starts in 0 chunks", self.run_script(tmp))
            self.assertEqual(chunk.read_text(), text)


if __name__ == "__main__":
    unittest.main()
