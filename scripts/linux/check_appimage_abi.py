#!/usr/bin/env python3
"""Audit a boat-of-daphnes AppImage: glibc baseline, no C++ runtime dependency, no private game files.

    check_appimage_abi.py IMAGE [MAX_GLIBC] [--fixture | --personal]

Default (a public image): the game module must be absent, and no disc image, extracted game file or save may be
inside. --fixture (CI): the module must be an EMPTY stand-in, a few kilobytes at most with none of the game's entry
points. --personal: a real module is allowed (an image made for yourself; it is then not to be shared).

The program must not need the player's libstdc++ (GLIBCXX_*), and no ELF file of the image may need a glibc newer than
MAX_GLIBC (default 2.39, Ubuntu 24.04's, which SteamOS and current distributions provide). The randomizer's Python
wheels carry their own libraries and are held to the glibc rule only.
"""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

GLIBC_VERSION = re.compile(r"\bGLIBC_(\d+(?:\.\d+)*)\b")
GLIBCXX_VERSION = re.compile(r"\bGLIBCXX_(\d+(?:\.\d+)*)\b")

# These files must never be in a public image or in CI output.
PRIVATE_FILE_SUFFIXES = {
    ".iso", ".gcm", ".rvz", ".wbfs", ".wia", ".ciso", ".gcz", ".nfs",
    ".dol", ".rel", ".card", ".gci", ".sav", ".bwstate",
}
PRIVATE_DIRS = ("game", "assets/swift_sail", "assets/link_colors", "assets/bunnyhood")

REQUIRED_PAYLOAD = (
    "AppRun",
    "boat-of-daphnes.desktop",
    "usr/bin/boat-of-daphnes",
    "usr/bin/dsp/dsp_rom.bin",
    "usr/bin/dsp/dsp_coef.bin",
)
MODULE = "usr/bin/gGZLE01_recomp.so"
FIXTURE_MODULE_LIMIT = 64 * 1024
GAME_ENTRY_POINTS = (b"staticrecomp_get_module", b"bluewake_composite_")


def parse_version(value):
    return tuple(int(part) for part in value.split("."))


def extract_appimage(image, destination):
    """Extract an AppImage without requiring FUSE."""
    result = subprocess.run([str(image), "--appimage-extract"], cwd=destination, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "unknown extraction error")
    return Path(destination) / "squashfs-root"


def check_payload(root, mode):
    errors = []
    missing = [relative for relative in REQUIRED_PAYLOAD if not (root / relative).is_file()]
    if missing:
        errors.append("missing payload: " + ", ".join(missing))
    module = root / MODULE
    if mode == "public" and module.exists():
        errors.append("a public image must not carry the game module (translated from a disc); "
                      "it belongs in the player's data folder")
    if mode == "fixture":
        if not module.is_file():
            errors.append("the fixture needs its empty stand-in module")
        else:
            data = module.read_bytes()
            if len(data) > FIXTURE_MODULE_LIMIT:
                errors.append(f"the fixture's module is {len(data)} bytes: a stand-in must be empty "
                              f"(at most {FIXTURE_MODULE_LIMIT})")
            if any(name in data for name in GAME_ENTRY_POINTS):
                errors.append("the fixture's module exports the game's entry points: it is not an empty stand-in")
    return errors


# wwrando's own patch modules (MIT, built from its sources, not from the game): the randomizer generator carries them.
WWRANDO_OWN_MODULES = {"usr/bin/rando/wwrando/asm/d_a_switch_op.rel", "usr/bin/rando/wwrando/asm/d_a_dungeon_flag_sw.rel"}


def check_for_private_files(root):
    errors = []
    for relative in PRIVATE_DIRS:
        if (root / "usr/bin" / relative).exists():
            errors.append(f"prohibited private folder: usr/bin/{relative}")
    for path in root.rglob("*"):
        if (path.is_file() and not path.is_symlink() and path.suffix.lower() in PRIVATE_FILE_SUFFIXES
                and path.relative_to(root).as_posix() not in WWRANDO_OWN_MODULES):
            errors.append(f"prohibited private file: {path.relative_to(root)}")
    return errors


def elf_files(image, root):
    """The outer AppImage runtime and every regular ELF file in its filesystem."""
    candidates = [(image, "AppImage runtime")]
    candidates.extend((path, str(path.relative_to(root))) for path in root.rglob("*"))
    for path, label in candidates:
        if path.is_symlink() or not path.is_file():
            continue
        try:
            with path.open("rb") as stream:
                is_elf = stream.read(4) == b"\x7fELF"
        except OSError as error:
            yield path, label, error
            continue
        if is_elf:
            yield path, label, None


def audit_elf_versions(image, root, readelf, maximum_glibc):
    errors = []
    highest_glibc = "0"
    elf_count = 0
    for path, label, open_error in elf_files(image, root):
        if open_error is not None:
            errors.append(f"{label}: {open_error}")
            continue
        elf_count += 1
        result = subprocess.run([readelf, "--version-info", str(path)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        if result.returncode:
            errors.append(f"{label}: readelf failed: {result.stderr.strip()}")
            continue
        glibc_versions = GLIBC_VERSION.findall(result.stdout)
        if glibc_versions:
            file_maximum = max(glibc_versions, key=parse_version)
            highest_glibc = max((highest_glibc, file_maximum), key=parse_version)
            if parse_version(file_maximum) > parse_version(maximum_glibc):
                errors.append(f"{label} requires GLIBC_{file_maximum} (maximum GLIBC_{maximum_glibc})")
        # The program and the libraries beside it must not depend on the host's libstdc++; the randomizer's wheels
        # carry their own.
        if not label.startswith("usr/bin/rando/"):
            glibcxx_versions = GLIBCXX_VERSION.findall(result.stdout)
            if glibcxx_versions:
                required = max(glibcxx_versions, key=parse_version)
                errors.append(f"{label} requires GLIBCXX_{required}; the C++ runtime must be linked in "
                              "(-static-libstdc++), not taken from the player's system")
    if elf_count == 0:
        errors.append("no ELF payloads found")
    return errors, elf_count, highest_glibc


def main(args):
    mode = "public"
    for flag, name in (("--fixture", "fixture"), ("--personal", "personal")):
        if flag in args:
            mode = name
            args = [a for a in args if a != flag]
    if len(args) not in (1, 2):
        print("usage: check_appimage_abi.py IMAGE [MAX_GLIBC] [--fixture | --personal]", file=sys.stderr)
        return 2
    image = Path(args[0]).resolve()
    maximum_glibc = args[1] if len(args) == 2 else "2.39"
    if not image.is_file():
        print(f"appimage audit: not a file: {image}", file=sys.stderr)
        return 2
    try:
        parse_version(maximum_glibc)
    except ValueError:
        print(f"appimage audit: invalid GLIBC version: {maximum_glibc}", file=sys.stderr)
        return 2
    readelf = shutil.which("readelf")
    if readelf is None:
        print("appimage audit: readelf is not on PATH", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="bod-appimage-audit-") as work:
        try:
            root = extract_appimage(image, work)
        except RuntimeError as error:
            print(f"appimage audit: extraction failed: {error}", file=sys.stderr)
            return 1
        errors = check_payload(root, mode)
        if mode != "personal":
            errors.extend(check_for_private_files(root))
        abi_errors, elf_count, highest_glibc = audit_elf_versions(image, root, readelf, maximum_glibc)
        errors.extend(abi_errors)
        if errors:
            for error in errors:
                print(f"appimage audit: {error}", file=sys.stderr)
            return 1
        print(f"appimage audit ({mode}): {elf_count} ELF files; maximum GLIBC_{highest_glibc}; "
              "no private game files" if mode != "personal" else
              f"appimage audit (personal): {elf_count} ELF files; maximum GLIBC_{highest_glibc}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
