#!/usr/bin/env python3
"""The guest pc at every option hook site, stored just before the host's hook runs.

    python scripts/windows/hook_pc.py COMPOSITE_SRC

The translator emits a hook (mods/*/options.txt: `hook ADDR option before|after N`) as
`if (dolrecomp_option_flags[i]) { hook_next = dolrecomp_native_hook(ctx, N); ... }` at the site,
where the plain translation had already stored the instruction's pc. The prepaid block copies
(fast_blocks.py) and their lean forms leave a pc store out wherever nothing in the copy reads it,
and behaviour timing runs every block's copy, so the host's hook saw an earlier instruction's pc.
Host hooks read it: one tells the pause menu's cloth from the file screen's by pc, the Better
Wind Waker DOL hooks dispatch on it, and two compute their return address from it. In 2026.10.16a
the pause menu's backdrop stopped covering wide windows this way.

Before each hook's `if`, this stores the site's address: for a hook before an instruction, the
instruction that follows it; for a hook after one, the instruction it follows. One store per
site. Repeatable: a chunk that has it is left as it is. Run it last, after behaviour_timing.py.
"""
import re
import sys
from pathlib import Path

MARK = "/* bluewake: the pc stored at each hook site (scripts/windows/hook_pc.py) */\n"
HOOK_IF = re.compile(r"^(\s*)if \(dolrecomp_option_flags\[\d+u\]\) \{\s*$")
HOOK_CALL = "dolrecomp_native_hook(ctx,"
INSN = re.compile(r"^\s*// ([0-9A-F]{8}):")


def transform(text):
    if MARK in text or HOOK_CALL not in text:
        return text, 0
    lines = text.split("\n")
    out, done = [], 0
    for i, line in enumerate(lines):
        m = HOOK_IF.match(line)
        if m and i + 1 < len(lines) and HOOK_CALL in lines[i + 1]:
            # The hook's block ends at the first line back at the `if`'s indent with a closing brace.
            end = i + 1
            while end < len(lines) and not (lines[end].startswith(m.group(1) + "}") and
                                            len(lines[end]) - len(lines[end].lstrip()) == len(m.group(1))):
                end += 1
            after = next((INSN.match(l) for l in lines[end + 1:end + 4] if l.strip()), None)
            if after is not None:
                site = after.group(1)  # a hook before this instruction
            else:
                before = next((INSN.match(l) for l in reversed(lines[max(0, i - 200):i]) if INSN.match(l)), None)
                site = before.group(1) if before is not None else None
            if site is not None:
                out.append(f"{m.group(1)}ctx->pc = 0x{site}u;")
                done += 1
        out.append(line)
    if not done:
        return text, 0
    return MARK + "\n".join(out), done


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    total = files = 0
    for path in sorted(Path(sys.argv[1]).rglob("*.c")):
        text = path.read_text(encoding="utf-8")
        new, done = transform(text)
        if done:
            path.write_text(new, encoding="utf-8", newline="\n")
            total += done
            files += 1
    print(f"hook_pc: {total} hook sites in {files} chunk files store their pc")


if __name__ == "__main__":
    main()
