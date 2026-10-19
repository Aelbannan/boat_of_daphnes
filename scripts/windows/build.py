#!/usr/bin/env python3
"""boat-of-daphnes builder for Windows: turn your own game disc into your own game, on your PC.

    python scripts/windows/build.py DISC [--out build/windows] [options]

DISC is your own The Wind Waker (GameCube, USA GZLE01 revision 0) image: an .iso or
.gcm, or a Dolphin .rvz/.wia/.gcz/.ciso/.nfs image (converted to an ISO with
nodtool, which is built from crates.io with Rust's cargo on first use).

Builds the game for Windows from your disc. The runtime is vendored in
runtime/recompcore; the translator (DolRecomp) and the verified source digest
are pinned in scripts/windows/pins.txt. docs/WINDOWS.md explains the port.

Steps, each logged under OUT/logs:
  1 tools        Visual Studio's clang and the Windows SDK, CMake 3.25+, Ninja, git
  2 dependencies the pinned DolRecomp source (ref/dolrecomp)
  3 disc         check the disc; convert a compressed image to an ISO
  4 extract      main.dol and the 415 RELs from the disc (the disc is verified)
  5 translate    the game's PowerPC code to C (DolRecomp)
  6 generate     the composite source, compared with the verified digest
  7 mods         widescreen 16:9 and 16:10 and Better Wind Waker's options (--no-mods skips); with --rando-variant also the
                 randomizer (wwrando's patches as one more mod, docs/fork/rando-inhouse.md)
  8 compile      the game module, gGZLE01_recomp.dll (the long step)
  9 app          boat-of-daphnes.exe, Aurora (Direct3D 12 through Dawn), SDL3 and the DSP
 10 package      the app folder OUT/boat-of-daphnes, ready to run

The app folder contains code translated from YOUR disc and a copy of the disc:
it is yours alone. Never share or upload it. Your saves live in
Save\\ beside the executable, as in Ship of Harkinian; rebuilding never touches them.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PINS = ROOT / "scripts/windows/pins.txt"
MODULE = "gGZLE01_recomp.dll"
# nodtool (https://github.com/encounter/nod) converts Dolphin's compressed
# formats to ISO and checks the result against Redump.
NODTOOL_VERSION = "2.0.0-alpha.9"
DISC_FORMATS = (".rvz", ".wia", ".gcz", ".ciso", ".nfs", ".wbfs", ".tgc")
GC_MAGIC = 0xC2339F3D


class BuildError(Exception):
    pass


def die(message):
    raise BuildError(message)


def step(title):
    print(f"\n==> {title}", flush=True)


def default_jobs():
    """All cores, but no more parallel compiles than memory allows: the large
    translated chunks take 1 to 3 GB each in clang, and running out of commit
    kills the compiler ("LLVM ERROR: out of memory"; compile_module retries)."""
    cores = os.cpu_count() or 8
    try:
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                        ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
        status = MemoryStatus()
        status.dwLength = ctypes.sizeof(MemoryStatus)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            available = min(status.ullAvailPhys, status.ullAvailPageFile)
            return max(1, min(cores, int(available // (2.5 * 2**30))))
    except (AttributeError, OSError):
        pass
    return cores


def profile_value(name):
    """A NAME=value pin from scripts/windows/pins.txt."""
    match = re.search(rf"^{name}=(\S+)$", PINS.read_text(), re.M)
    if not match:
        die(f"{PINS} has no {name}")
    return match.group(1)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 22), b""):
            digest.update(block)
    return digest.hexdigest()


def sync_tree(new, current):
    """Make `current` the same tree as `new`, replacing only files that differ,
    so the unchanged ones keep their timestamps and Ninja does not recompile
    them. `new` is removed."""
    current.mkdir(parents=True, exist_ok=True)
    wanted = set()
    for source in new.rglob("*"):
        rel = source.relative_to(new)
        target = current / rel
        wanted.add(rel)
        if source.is_dir():
            target.mkdir(exist_ok=True)
            continue
        if (target.is_file() and target.stat().st_size == source.stat().st_size
                and target.read_bytes() == source.read_bytes()):
            continue
        os.replace(source, target)
    for target in sorted(current.rglob("*"), reverse=True):
        if target.relative_to(current) not in wanted:
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()
    shutil.rmtree(new)


def tree_digest(root):
    """scripts/windows/composite_manifest.py's digest of a generated tree."""
    out = subprocess.check_output([sys.executable, str(ROOT / "scripts/windows/composite_manifest.py"), str(root)],
                                  text=True)
    return out.split()[0]


class Builder:
    def __init__(self, args):
        self.args = args
        self.out = args.out
        self.logs = self.out / "logs"
        self.env = None
        self.recompcore = ROOT / "runtime/recompcore"
        self.dolrecomp_src = ROOT / "ref/dolrecomp"
        self.iso = None
        self.accept_sha1 = None
        self.profile = None

    # --- helpers -------------------------------------------------------
    def run(self, name, command, *, env=None, cwd=None, ninja=False):
        """Run a command with a complete log and progress every 15 seconds.
        A compile (cmake --build) waits for the machine's build lock: builds
        of several worktrees at once made the PC unusable (machine_lock.py)."""
        if "--build" in [str(c) for c in command]:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            from machine_lock import machine_lock
            with machine_lock("build"):
                return self._run(name, command, env=env, cwd=cwd, ninja=ninja)
        return self._run(name, command, env=env, cwd=cwd, ninja=ninja)

    def _run(self, name, command, *, env=None, cwd=None, ninja=False):
        self.logs.mkdir(parents=True, exist_ok=True)
        log = self.logs / f"{name}.log"
        environment = dict(env or self.env or os.environ)
        if ninja:
            environment["NINJA_STATUS"] = "[%f/%t] "
        # Windows looks a bare program name up on this process's PATH, not the
        # environment given to it: resolve it on the build's own PATH.
        command = [str(c) for c in command]
        if os.sep not in command[0] and "/" not in command[0]:
            command[0] = shutil.which(command[0], path=environment.get("PATH")) or command[0]
        start = time.monotonic()
        print(f"  {name} (log: {log})", flush=True)
        with open(log, "wb") as stream:
            process = subprocess.Popen(command, cwd=cwd or ROOT, stdout=stream,
                                       stderr=subprocess.STDOUT, env=environment)
            last = start
            while True:
                try:
                    status = process.wait(timeout=5)
                    break
                except subprocess.TimeoutExpired:
                    pass
                except KeyboardInterrupt:
                    process.terminate()
                    raise
                now = time.monotonic()
                if now - last >= 15:
                    last = now
                    detail = ""
                    try:
                        with open(log, "rb") as recent:
                            recent.seek(max(0, log.stat().st_size - 16384))
                            units = re.findall(rb"\[(\d+/\d+)\]", recent.read())
                        if units:
                            detail = f", {units[-1].decode()}"
                    except OSError:
                        pass
                    elapsed = int(now - start)
                    print(f"  {name}: {elapsed // 60}m {elapsed % 60:02d}s{detail}", flush=True)
        if status != 0:
            tail = log.read_bytes()[-4000:].decode(errors="replace")
            print(tail, file=sys.stderr)
            die(f"{name} failed (exit {status}); full log {log}")
        elapsed = int(time.monotonic() - start)
        if elapsed >= 60:
            print(f"  {name}: done in {elapsed // 60}m {elapsed % 60:02d}s", flush=True)
        return log

    def git(self, *args, cwd=None):
        return subprocess.check_output(["git", *args], cwd=cwd or ROOT, text=True,
                                       stderr=subprocess.DEVNULL).strip()

    # --- 1 tools -----------------------------------------------------
    def check_tools(self):
        if platform.system() != "Windows":
            die("this builder is for Windows only")
        if platform.machine().lower() not in ("amd64", "x86_64"):
            die(f"an x86-64 PC is required (this is {platform.machine()})")
        if sys.version_info < (3, 10):
            die("Python 3.10 or newer is required")
        self.env = self.visual_studio_env()
        for tool in ("git", "cmake", "ninja"):
            if shutil.which(tool, path=self.env["PATH"]) is None:
                die(f"missing {tool}: install Git, CMake 3.25+ and Ninja (pip install cmake ninja)")
        cmake = shutil.which("cmake", path=self.env["PATH"])
        version = subprocess.check_output([cmake, "--version"], text=True, env=self.env).split()[2]
        if tuple(int(x) for x in version.split(".")[:2]) < (3, 25):
            die(f"CMake 3.25 or newer is required (found {version})")
        clang = shutil.which("clang", path=self.env["PATH"])
        self.clang = clang
        if clang is None:
            die("Visual Studio's clang is missing: in the Visual Studio Installer, add "
                "'C++ Clang Compiler for Windows' (and 'MSBuild support for LLVM')")
        clang_version = subprocess.check_output([clang, "--version"], text=True, env=self.env).splitlines()[0]
        major = int(re.search(r"version (\d+)", clang_version).group(1))
        if major < 17:
            die(f"clang 17 or newer is required ({clang_version})")
        self.clang_version = clang_version
        self.check_march()
        print(f"{clang_version}; cmake {version} ({cmake}); ninja {subprocess.check_output([shutil.which('ninja', path=self.env['PATH']), '--version'], text=True, env=self.env).strip()}; "
              f"{self.args.jobs} jobs; -march={self.args.march}")

    def visual_studio_env(self):
        """The x64 developer environment of the newest Visual Studio with clang."""
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
            "Microsoft Visual Studio/Installer/vswhere.exe"
        if not vswhere.exists():
            die("Visual Studio 2022 or newer with the C++ workload is required (vswhere.exe not found)")
        found = json.loads(subprocess.check_output(
            [str(vswhere), "-all", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-format", "json", "-utf8"], text=True, encoding="utf-8"))
        candidates = [Path(v["installationPath"]) for v in found
                      if (Path(v["installationPath"]) / "VC/Tools/Llvm/x64/bin/clang.exe").exists()]
        if not candidates:
            die("no Visual Studio with the C++ workload and its clang was found: in the Visual Studio "
                "Installer, add 'Desktop development with C++' and 'C++ Clang Compiler for Windows'")
        install = sorted(candidates, key=lambda p: [v for v in found if Path(v["installationPath"]) == p][0]
                         .get("installationVersion", ""))[-1]
        vcvars = install / "VC/Auxiliary/Build/vcvars64.bat"
        # vcvars fails on quoted PATH entries or ones with parentheses (some
        # SDK installers add them); keep the clean ones for the build. Leave
        # out MSYS2 and Cygwin tool folders (devkitPro, Git's usr/bin, ...):
        # their cmake turns D:/x paths into /d/... and fails to configure.
        def posix_layer(folder):
            return any((Path(folder) / dll).exists() for dll in ("msys-2.0.dll", "cygwin1.dll"))
        path = [p.strip('"') for p in os.environ.get("PATH", "").split(os.pathsep)]
        base = dict(os.environ, PATH=os.pathsep.join(p for p in path if p and "(" not in p and ")" not in p
                                                     and not posix_layer(p)))
        output = subprocess.run(f'cmd /d /c ""{vcvars}" >nul 2>&1 && set"', capture_output=True, text=True,
                                env=base, shell=False)
        if output.returncode != 0 or "INCLUDE=" not in output.stdout:
            die(f"{vcvars} failed; open a 'x64 Native Tools Command Prompt' and rerun from there")
        env = {}
        for line in output.stdout.splitlines():
            key, sep, value = line.partition("=")
            if sep and key:
                env[key] = value
        env["PATH"] = str(install / "VC/Tools/Llvm/x64/bin") + os.pathsep + env.get("PATH", "")
        print(f"Visual Studio: {install}")
        return env

    def check_march(self):
        """The game module is compiled for --march; refuse a level this CPU lacks."""
        levels = {"x86-64": set(), "x86-64-v2": {"sse4.2", "popcnt"},
                  "x86-64-v3": {"sse4.2", "popcnt", "avx", "avx2", "fma", "bmi1", "bmi2", "movbe", "lzcnt"}}
        if self.args.march not in levels:
            return
        # CPUID and XGETBV directly (__builtin_cpu_supports needs compiler-rt,
        # which the MSVC target does not link); AVX also needs the OS to save
        # the YMM state.
        probe = self.out / "tools/march_probe.c"
        probe.parent.mkdir(parents=True, exist_ok=True)
        probe.write_text(r"""#include <immintrin.h>
#include <intrin.h>
#include <stdio.h>
int main(void) {
    int r[4];
    __cpuid(r, 1);
    const int ecx1 = r[2];
    __cpuidex(r, 7, 0);
    const int ebx7 = r[1];
    __cpuid(r, 0x80000001);
    const int ecx81 = r[2];
    const int os_avx = ((ecx1 >> 27) & 1) && (_xgetbv(0) & 6) == 6;
    printf("sse4.2=%d popcnt=%d movbe=%d fma=%d avx=%d avx2=%d bmi1=%d bmi2=%d lzcnt=%d\n",
           (ecx1 >> 20) & 1, (ecx1 >> 23) & 1, (ecx1 >> 22) & 1, ((ecx1 >> 12) & 1) && os_avx,
           ((ecx1 >> 28) & 1) && os_avx, ((ebx7 >> 5) & 1) && os_avx, (ebx7 >> 3) & 1,
           (ebx7 >> 8) & 1, (ecx81 >> 5) & 1);
    return 0;
}
""")
        exe = probe.with_suffix(".exe")
        subprocess.run([self.clang, "-O1", "-mxsave", str(probe), "-o", str(exe)], env=self.env, check=True,
                       capture_output=True)
        report = subprocess.check_output([str(exe)], text=True).split()
        have = {name for name, bit in (item.split("=") for item in report) if bit == "1"}
        if not levels[self.args.march] <= have:
            fallback = "x86-64-v2" if levels["x86-64-v2"] <= have else "x86-64"
            print(f"this CPU lacks {self.args.march} ({', '.join(sorted(levels[self.args.march] - have))}); "
                  f"using -march={fallback}")
            self.args.march = fallback

    # --- 2 dependencies ------------------------------------------------
    def dependencies(self):
        """The vendored runtime, and DolRecomp fetched at its pinned commit."""
        if not (self.recompcore / "GXRuntime/CMakeLists.txt").exists():
            die(f"{self.recompcore} is missing: the runtime is vendored in the repository (runtime/VENDORED.md)")
        sha = profile_value("DOLRECOMP_SHA")
        url = profile_value("DOLRECOMP_URL")
        sub = self.dolrecomp_src
        if not (sub / ".git").exists():
            if sub.exists() and any(sub.iterdir()):
                die(f"{sub} exists but is not a git checkout: move it aside and rerun")
            sub.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(["git", "init", "-q"], cwd=sub)
            # The sources as committed (LF), whatever the global core.autocrlf
            # says: the generated source must match the verified digest.
            subprocess.check_call(["git", "config", "core.autocrlf", "false"], cwd=sub)
            subprocess.check_call(["git", "config", "core.longpaths", "true"], cwd=sub)
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=sub, capture_output=True, text=True).stdout.strip()
        if head != sha:
            if self.git("status", "--porcelain", "--untracked-files=no", cwd=sub):
                die(f"{sub} has local changes and is not at {sha}: move it aside and rerun")
            print(f"fetching DolRecomp {sha}")
            subprocess.run(["git", "remote", "remove", "pinned"], cwd=sub, capture_output=True)
            subprocess.check_call(["git", "remote", "add", "pinned", url], cwd=sub)
            self.run("dolrecomp-fetch", ["git", "-C", sub, "fetch", "--depth", "1", "pinned", sha], env=os.environ)
            subprocess.check_call(["git", "checkout", "-q", "--detach", "FETCH_HEAD"], cwd=sub)
        if self.git("rev-parse", "HEAD", cwd=sub) != sha:
            die(f"{sub} is not at {sha}")
        if self.git("status", "--porcelain", "--untracked-files=no", cwd=sub):
            die(f"{sub} has local changes; the translator must be the pinned source exactly")
        print(f"runtime vendored ({self.recompcore}), DolRecomp {sha}")

    # --- 3 disc ----------------------------------------------------------
    def disc(self):
        source = self.args.disc.resolve()
        if not source.is_file():
            die(f"disc image not found: {source}")
        if source.suffix.lower() in DISC_FORMATS:
            iso = self.out / "disc/GZLE01.iso"
            stamp = self.out / "disc/source.json"
            key = {"source": str(source), "size": source.stat().st_size, "mtime": source.stat().st_mtime_ns,
                   "nodtool": NODTOOL_VERSION}
            if iso.exists() and stamp.exists() and json.loads(stamp.read_text()) == key:
                print(f"reusing {iso}")
            else:
                nodtool = self.nodtool()
                iso.parent.mkdir(parents=True, exist_ok=True)
                pending = iso.with_name("GZLE01.pending.iso")  # nodtool picks the format by extension
                self.run("disc-convert", [nodtool, "--no-color", "convert", source, pending], env=os.environ)
                log = (self.logs / "disc-convert.log").read_text(encoding="utf-8", errors="replace")
                if "Redump:" in log and "\u2705" not in log.split("Redump:")[1].splitlines()[0]:
                    print("warning: nodtool did not match this image to a Redump entry")
                os.replace(pending, iso)
                stamp.write_text(json.dumps(key))
            self.iso = iso
        else:
            self.iso = source
        with open(self.iso, "rb") as disc:
            header = disc.read(0x20)
        if len(header) < 0x20 or int.from_bytes(header[0x1C:0x20], "big") != GC_MAGIC:
            die(f"{self.iso} is not a GameCube disc image")
        if self.args.rando_iso:
            # A randomizer seed's disc (scripts/rando/patch_game.py --iso-out): wwrando's own ISO, game id GZLE99.
            if header[:6] != b"GZLE99":
                die(f"--rando-iso needs wwrando's randomized disc (GZLE99); this is {header[:6].decode(errors='replace')}")
            self.accept_sha1 = self.dol_sha1(self.iso)
            print(f"disc: {self.iso} (randomizer seed disc GZLE99, executable sha1 {self.accept_sha1})")
            return
        if header[:6] != b"GZLE01":
            die(f"this is a GameCube disc, but not The Wind Waker (USA, GZLE01): its id is "
                f"{header[:6].decode(errors='replace')}")
        if header[7] != 0:
            die(f"this is GZLE01 revision {header[7]}; BlueWake supports revision 0 only")
        print(f"disc: {self.iso} (GZLE01 revision 0)")

    @staticmethod
    def dol_sha1(iso):
        """SHA-1 of the disc's main.dol (offset in boot.bin at 0x420, size from the DOL's section table)."""
        with open(iso, "rb") as disc:
            disc.seek(0x420)
            offset = int.from_bytes(disc.read(4), "big")
            disc.seek(offset)
            header = disc.read(0x100)
            end = max(int.from_bytes(header[4 * i:4 * i + 4], "big") + int.from_bytes(header[0x90 + 4 * i:0x94 + 4 * i], "big")
                      for i in range(18))
            disc.seek(offset)
            return hashlib.sha1(disc.read(end)).hexdigest()

    def nodtool(self):
        root = ROOT / "build/tools/nodtool"
        exe = root / "bin/nodtool.exe"
        if exe.exists():
            return exe
        if shutil.which("cargo") is None:
            die("this disc image is compressed; converting it needs Rust's cargo (https://rustup.rs) "
                "to build nodtool, or convert it to .iso yourself (Dolphin: right-click the game, "
                "Convert File..., format ISO) and pass the .iso")
        print(f"building nodtool {NODTOOL_VERSION} from crates.io (once)")
        self.run("nodtool-install", ["cargo", "install", "nodtool", "--version", NODTOOL_VERSION, "--locked",
                                     "--root", root], env=os.environ)
        return exe

    # --- tools built from source ---------------------------------------
    def build_dolrecomp(self):
        build = self.out / "dolrecomp"
        # binmode.obj: the CRT opens files in binary mode by default, so the
        # translator writes the same bytes (LF) as on macOS and the generated
        # source matches the verified digest.
        self.run("dolrecomp-configure", ["cmake", "-S", self.dolrecomp_src, "-B", build, "-G", "Ninja",
                                         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang-cl",
                                         "-DDOLRECOMP_WARNINGS_AS_ERRORS=OFF",
                                         "-DCMAKE_EXE_LINKER_FLAGS=binmode.obj"])
        self.run("dolrecomp-build", ["cmake", "--build", build, "--target", "dolrecomp", "-j", self.args.jobs],
                 ninja=True)
        return build / "dolrecomp.exe"

    APP_PROFILE = ROOT / "windows/pgo/app.profdata"

    def configure_app(self, build=None, instrument=False):
        """The app's build, by default build/windows/app. With the committed
        profile of the app's own code (windows/pgo/app.profdata,
        scripts/windows/train_app_profile.py) it is compiled with it and with
        ThinLTO: on four of the i9's E-cores that took the GX worker's CPU per
        game frame from 16.6-17.2 ms to 13.8-14.3 and the game thread's from
        23.9-24.3 to 22.4-23.2 (2026-10-02). `instrument` builds it to record
        such a profile instead."""
        self.app_build = build or self.out / "app"
        profile, link = "", ""
        if instrument:
            profile = link = "-fprofile-instr-generate"
        elif self.APP_PROFILE.exists() and not getattr(self.args, "no_app_pgo", False):
            # Functions changed since the profile was recorded are compiled
            # without counts (the warnings say so; they are expected).
            profile = (f"-fprofile-instr-use={self.APP_PROFILE.as_posix()} -Wno-profile-instr-unprofiled "
                       "-Wno-profile-instr-out-of-date -Wno-backend-plugin -flto=thin")
            link = "-flto=thin"
        # The app for the same CPU level as the game module: the FIFO worker's
        # matrix work for Smooth Motion needs AVX2 and FMA to keep up (at the
        # baseline level it held the game below 30 FPS on Outset, 2026-09-29).
        self.run("app-configure", [
            "cmake", "-S", ROOT / "windows", "-B", self.app_build, "-G", "Ninja",
            "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_BUILD_TYPE=Release",
            # Debug information in a PDB beside the build's BlueWake.exe (the
            # package copies only the exe and DLLs), so a crash address names its
            # function; /OPT:REF,ICF keep the code what it is without /DEBUG.
            f"-DCMAKE_C_FLAGS=-march={self.args.march} -g -gcodeview {profile}",
            f"-DCMAKE_CXX_FLAGS=-march={self.args.march} -g -gcodeview {profile}",
            "-DBUILD_TESTING=OFF", f"-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,/DEBUG -Wl,/OPT:REF -Wl,/OPT:ICF {link}",
            f"-DBLUEWAKE_RANDO_SITES={self.out / 'rando/sites/rando_sites.inc' if getattr(self.args, 'rando_variant', False) else ''}",
            f"-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld {link}",
            f"-DBLUEWAKE_WINDOWS_CONSOLE={'ON' if self.args.console else 'OFF'}"])

    # --- 4 extract ---------------------------------------------------------
    def extract(self, iso, game, accept_sha1=None):
        self.run("disc-extract-build", ["cmake", "--build", self.app_build, "--target", "bluewake_disc_extract"],
                 ninja=True)
        env = dict(self.env)
        if accept_sha1:
            env["BLUEWAKE_ACCEPT_DOL_SHA1"] = accept_sha1
        # disc_extract checks the disc id (GZLE01) and the executable's hash
        # (revision 0) and refuses anything else.
        self.run(f"disc-extract{'-' + game.parent.name if accept_sha1 else ''}",
                 [self.app_build / "bluewake_disc_extract.exe", iso, game], env=env)
        rels = len(list((game / "rels").glob("*.rel")))
        if rels != 415 and not (accept_sha1 and rels >= 415):
            die(f"expected 415 RELs in {game / 'rels'}, found {rels}")

    # --- 5 translate -------------------------------------------------------
    def translate(self, dol, out, rels=None, name="translate", sites=(), staged=False):
        """DolRecomp over main.dol (and the RELs); `sites` is --option-sites FILE
        for Better Wind Waker's options (scripts/mods/build_mods.sh). `staged`: the REL folder holds the randomizer's two
        extra modules, translated under names that sort last (rando_translation_rels)."""
        pending = out.with_name(out.name + ".new")
        shutil.rmtree(pending, ignore_errors=True)
        pending.mkdir(parents=True)
        if rels is not None and (self.args.rando_iso or staged):
            rels = self.rando_translation_rels(rels)
        self.run(f"{name}-dol", [self.dolrecomp, "--gamecube", "--backend", "c", "--cpu", "gekko",
                                 "--partition-instructions", "4096", *sites, dol, pending / "dol",
                                 "-j", self.args.jobs])
        if rels is not None:
            # With option sites, name the RELs' folder with "/" and a trailing
            # "/": DolRecomp matches a REL's sites (d_a_ship.rel+0x...) to the
            # file name after the last "/" of its path, and on Windows it joins
            # a folder and a file with "\" unless the folder already ends in a
            # separator, so the sites in the RELs would never match.
            rels_arg = rels.as_posix().rstrip("/") + "/" if sites else rels
            self.run(f"{name}-rels", [self.dolrecomp, "--gamecube", "--backend", "c", "--cpu", "gekko",
                                      "--rel-base", "0xC0400000", *sites, rels_arg, pending / "rels",
                                      "-j", self.args.jobs])
        shutil.rmtree(out, ignore_errors=True)
        os.replace(pending, out)

    def rando_patch(self):
        """Step `rando-patch`: wwrando's patches as one fixed reference game, and the sites that make it every seed's game.

        scripts/rando/patch_game.py patches the player's disc with the reference options (every option that adds a
        block on, seed RV1; variant_sites.REFERENCE_OPTIONS) and keeps main.dol and the RELs. The result is
        OUT/rando/game (main.dol, a complete rels folder: the disc's with the patched and the two new RELs over them)
        and OUT/rando/rels (only the patched and new ones, which the package puts in game/rando/rels). Then
        scripts/rando/variant_sites.py turns scripts/rando/seed_sites.json and the reference into OUT/rando/sites: the
        options.txt lines of the randomizer's switches, option words and seed hooks, and the host's rando_sites.inc.
        Nothing here is seed dependent: a seed is a seed pack read at run time."""
        r = self.out / "rando"
        runtime = self.out / "rando_runtime"
        if not self.rando_python(runtime).is_file():
            die(f"the randomizer variant needs wwrando's runtime: python {ROOT / 'scripts/windows/rando_runtime.py'}")
        sys.path.insert(0, str(ROOT / "scripts/rando"))
        import variant_sites
        key = hashlib.sha256()
        key.update(json.dumps([variant_sites.REFERENCE_OPTIONS, variant_sites.REFERENCE_SEED, str(self.iso),
                               self.iso.stat().st_size, self.iso.stat().st_mtime_ns], sort_keys=True).encode())
        for f in (ROOT / "scripts/rando/patch_game.py", ROOT / "scripts/rando/generate_seed.py",
                  ROOT / "scripts/rando/variant_sites.py", ROOT / "scripts/rando/seed_sites.json",
                  runtime / "manifest.json", Path(__file__)):
            key.update(f.read_bytes())
        key = key.hexdigest()
        stamp = r / "rando-patch.json"
        if (r / "game/main.dol").is_file() and (r / "sites/rando_sites.inc").is_file() and stamp.is_file() \
                and json.loads(stamp.read_text())["key"] == key:
            print(f"reusing the reference game in {r}")
            self.rando_key = key
            return
        shutil.rmtree(r, ignore_errors=True)
        r.mkdir(parents=True)
        options = r / "reference-options.json"
        options.write_text(json.dumps(variant_sites.REFERENCE_OPTIONS, indent=1) + "\n")
        env = dict(os.environ, RANDO_RUNTIME=str(runtime))
        self.run("rando-patch", [sys.executable, ROOT / "scripts/rando/patch_game.py", "--iso", self.iso, "--out",
                                 r / "ref", "--seed", variant_sites.REFERENCE_SEED, "--options", options, "--files", "code"],
                 env=env)
        patched = sorted((r / "ref/files/rels").glob("*.rel"))
        if len(patched) < 80 or not (r / "ref/sys/main.dol").is_file():
            die(f"the reference run made {len(patched)} RELs and no usable main.dol")
        (r / "game").mkdir()
        shutil.copy2(r / "ref/sys/main.dol", r / "game/main.dol")
        shutil.copytree(self.out / "game/rels", r / "game/rels")
        (r / "rels").mkdir()
        for path in patched:
            shutil.copy2(path, r / "game/rels" / path.name)
            shutil.copy2(path, r / "rels" / path.name)
        modules = len(list((r / "game/rels").glob("*.rel")))
        if modules != 417:
            die(f"the reference game has {modules} RELs, not 417")
        self.run("rando-sites", [sys.executable, ROOT / "scripts/rando/variant_sites.py", "--vanilla", self.out / "game",
                                 "--reference", r / "ref", "--wwrando", runtime / "wwrando", "--out", r / "sites"])
        stamp.write_text(json.dumps({"key": key}))
        self.rando_key = key
        print(f"reference game: main.dol and {len(patched)} patched RELs (2 new) in {r}")

    def helper_python(self):
        """The Python that runs the disc-derived asset scripts (they need Pillow)."""
        return sys.executable

    @staticmethod
    def rando_python(runtime):
        """The randomizer runtime's Python (scripts/windows/rando_runtime.py); the Linux builder's is in a venv."""
        return runtime / "python" / "python.exe"

    def rando_runtime_parts(self):
        """The runtime folders a package carries in rando/."""
        return ("python", "site-packages", "wwrando", "gclib")

    def rando_translation_rels(self, rels):
        """The REL folder DolRecomp reads for a randomizer seed's game. DolRecomp links the modules in file-name
        order, 64 KiB apart, so the two RELs wwrando adds (d_a_dungeon_flag_sw, d_a_switch_op) would move the
        address of every module after them, and with them every function name of the optimization profile
        (func_<address>). They are translated under names that sort last (zz_NAME.rel, which keeps the 415 game
        modules where the game and the profile have them); the game itself still loads them by their own names
        from game/rels."""
        staged = rels.with_name(rels.name + "-translate")
        shutil.rmtree(staged, ignore_errors=True)
        staged.mkdir(parents=True)
        for path in sorted(rels.glob("*.rel")):
            with open(path, "rb") as f:
                module_id = int.from_bytes(f.read(4), "big")
            target = staged / (("zz_" if module_id > 415 else "") + path.name)
            try:
                os.link(path, target)
            except OSError:
                shutil.copy2(path, target)
        return staged

    def composite(self, dol_dir, rels_dir, rels_bin, main_dol, out, name):
        shutil.rmtree(out, ignore_errors=True)
        self.run(name, [sys.executable, ROOT / "scripts/generate_composite.py", "--dol-dir", dol_dir,
                        "--rels-dir", rels_dir, "--rels-bin-dir", rels_bin, "--main-dol", main_dol,
                        "--output-dir", out])

    # --- 6 generate ------------------------------------------------------
    def generate(self):
        o = self.out
        new = o / "composite-src.new"
        self.composite(o / "translated/dol/generated", o / "translated/rels/generated/rels", o / "game/rels",
                       o / "game/main.dol", new, "composite-generate")
        expected = profile_value("COMPOSITE_DIGEST")
        digest = tree_digest(new)
        if digest == expected:
            print(f"composite source digest {digest}: the verified tree")
        elif self.args.accept_new_composite or self.args.rando_iso:
            print(f"composite source digest {digest} differs from the verified {expected} (accepted)")
        else:
            die(f"composite source digest {digest} differs from the verified {expected} (wrong disc revision "
                f"or translator?); --accept-new-composite overrides")
        # Keep an identical tree in place: rewriting 750 files would make the
        # compile start over. Mods are part of the recorded inputs.
        inputs = hashlib.sha256()
        inputs.update((f"{digest}\n{int(self.mods)}\n{int(self.args.prepared_blocks)}\n"
                       f"{int(self.args.fixed_cpu)}\n{int(self.args.fixed_mem1)}\n{int(self.args.inline_fp)}\n{int(self.args.gather_pipe)}\n{int(self.args.direct_calls)}\n{int(self.args.inline_gpr)}\n{int(self.args.native_j3d)}\n{int(self.args.native_vec)}\n{int(self.args.native_math)}\n{int(self.args.native_skin)}\n{int(self.args.native_game_math)}\n"
                       f"{int(self.args.lean_memory)}\n{int(self.args.native_entries)}\n"
                       + ("lean_blocks\n" if getattr(self.args, "lean_blocks", False) else "")
                       + ("lean_deadlines\n" if getattr(self.args, "lean_deadlines", False) else "")
                       + ("behaviour_timing\n" if getattr(self.args, "behaviour_timing", False) else "")
                       + f"{int(getattr(self.args, 'rando_variant', False))}\n{self.rando_key}\n").encode())
        for f in (sorted((ROOT / "scripts/mods").glob("*")) + sorted((ROOT / "mods/widescreen").glob("*.gecko"))
                  + [ROOT / "mods/betterww/options.txt", ROOT / "scripts/windows/fast_blocks.py",
                     ROOT / "scripts/windows/global_guest_cpu.py", ROOT / "scripts/windows/chunk_headers.py",
                     ROOT / "cmake/composite/inline_fp.h", ROOT / "cmake/composite/gather_pipe.h",
                     ROOT / "cmake/composite/gather_pipe.c", ROOT / "cmake/composite/gather_pipe_batch.h",
                     ROOT / "scripts/windows/direct_calls.py", ROOT / "cmake/composite/direct_calls.c",
                     ROOT / "cmake/composite/direct_calls.h", ROOT / "cmake/composite/inline_gpr.h",
                     ROOT / "cmake/composite/native_j3d.c", ROOT / "cmake/composite/native_j3d.h",
                     ROOT / "cmake/composite/native_vec.c", ROOT / "cmake/composite/native_vec.h",
                     ROOT / "scripts/windows/native_game_math.py", ROOT / "cmake/composite/native_game_math.c",
                     ROOT / "cmake/composite/native_game_math.h", ROOT / "scripts/windows/native_skin.py", ROOT / "cmake/composite/native_skin.c",
                     ROOT / "cmake/composite/native_skin.h", ROOT / "cmake/composite/native_math.c", ROOT / "cmake/composite/native_math.h",
                     ROOT / "cmake/composite/native_work_pool.c", ROOT / "cmake/composite/native_work_pool.h",
                     ROOT / "scripts/windows/inline_save_restore_gpr.py",
                     ROOT / "cmake/composite/native_fifo.c",
                     ROOT / "cmake/composite/native_fifo.h",
                     ROOT / "cmake/composite/native_bg.c",
                     ROOT / "cmake/composite/native_bg.h",
                     ROOT / "cmake/composite/native_mtxcalc.c",
                     ROOT / "cmake/composite/native_mtxcalc.h",
                     ROOT / "cmake/composite/native_search.c",
                     ROOT / "cmake/composite/native_search.h",
                     ROOT / "scripts/windows/native_entries.py",
                     ROOT / "scripts/windows/lean_memory.py", ROOT / "scripts/windows/return_ranges.py",
                     ROOT / "scripts/windows/hook_pc.py",
                     ROOT / "scripts/windows/chunk_pool.py", ROOT / "cmake/composite/module_export.c",
                     ROOT / "cmake/composite/direct_skip.h",
                     ROOT / "scripts/rando/check_variant.py", Path(__file__)]):
            if f.is_file():
                inputs.update(f.read_bytes())
        if getattr(self.args, "lean_deadlines", False):
            inputs.update((ROOT / "scripts/windows/lean_deadlines.py").read_bytes())
        if getattr(self.args, "behaviour_timing", False):
            inputs.update((ROOT / "scripts/windows/behaviour_timing.py").read_bytes())
        if self.args.direct_calls or self.args.native_game_math:
            # The source-derived watch list is part of the prepared module.
            for folder in ("runtime/host/src", "windows/src"):
                for path in sorted((ROOT / folder).rglob("*")):
                    if path.suffix in (".c", ".h", ".cpp", ".mm", ".m"):
                        inputs.update(str(path.relative_to(ROOT)).encode())
                        inputs.update(path.read_bytes())
        inputs = inputs.hexdigest()
        current = o / "composite-src"
        saved = (o / "composite-final.digest").read_text().strip() if (o / "composite-final.digest").exists() else ""
        same_inputs = (o / "composite-inputs.digest").exists() and \
            (o / "composite-inputs.digest").read_text().strip() == inputs
        if current.exists() and same_inputs and saved and tree_digest(current) == saved:
            shutil.rmtree(new)
            print("the existing composite source is current")
            self.composite_current = True
            self.mods_pending = (o / "mods.done").read_text().strip() != "complete" \
                if (o / "mods.done").exists() else self.mods
        else:
            sync_tree(new, current)
            (o / "composite-src.digest").write_text(digest + "\n")
            (o / "composite-inputs.digest").write_text(inputs + "\n")
            (o / "composite-final.digest").write_text(digest + "\n")
            (o / "mods.done").write_text("pending\n")
            self.mods_pending = self.mods

    # --- 7 mods --------------------------------------------------------------
    def build_mods(self):
        """Widescreen 16:9 and 16:10, Better Wind Waker's options, and each
        widescreen with them, as variants compiled into the same module: the
        steps of scripts/mods/build_mods.sh (docs/MODS.md)."""
        o, m = self.out, self.out / "mods"
        m.mkdir(parents=True, exist_ok=True)
        options = ROOT / "mods/betterww/options.txt"
        # The site list DolRecomp reads, with LF line ends (Python's stdout
        # writes CRLF on Windows).
        listed = subprocess.run([sys.executable, ROOT / "scripts/mods/game_options.py", "sites", options],
                                capture_output=True, check=True).stdout
        (m / "option-sites.txt").write_bytes(listed.replace(b"\r\n", b"\n"))
        sites = ("--option-sites", m / "option-sites.txt")

        for name, gecko in (("widescreen", "GZLE01.gecko"), ("widescreen1610", "GZLE01-16x10.gecko")):
            print(name)
            (m / name).mkdir(exist_ok=True)
            self.run(f"mods-{name}-gecko", [sys.executable, ROOT / "scripts/mods/gecko_apply.py",
                                           ROOT / "mods/widescreen" / gecko, o / "game/main.dol",
                                           m / name / "main.dol", m / name / "runtime.json"])
            self.translate(m / name / "main.dol", m / name / "translated", name=f"mods-{name}-translate")
            self.composite(m / name / "translated/dol/generated", o / "translated/rels/generated/rels",
                           o / "game/rels", m / name / "main.dol", m / name / "composite-src",
                           f"mods-{name}-composite")

        # The game's own executable and modules translated with the option
        # sites; the variants are the chunks that hold a site.
        print("Better Wind Waker options")
        (m / "betterww").mkdir(exist_ok=True)
        self.translate(o / "game/main.dol", m / "betterww/translated", o / "game/rels",
                       name="mods-betterww-translate", sites=sites)
        self.composite(m / "betterww/translated/dol/generated", m / "betterww/translated/rels/generated/rels",
                       o / "game/rels", o / "game/main.dol", m / "betterww/composite-src", "mods-betterww-composite")

        for combo, widescreen in (("combo", "widescreen"), ("combo1610", "widescreen1610")):
            print(f"{widescreen} + Better Wind Waker options")
            (m / combo).mkdir(exist_ok=True)
            self.translate(m / widescreen / "main.dol", m / combo / "translated", name=f"mods-{combo}-translate",
                           sites=sites)
            self.composite(m / combo / "translated/dol/generated", m / "betterww/translated/rels/generated/rels",
                           o / "game/rels", m / widescreen / "main.dol", m / combo / "composite-src",
                           f"mods-{combo}-composite")

        rando = self.build_rando_trees(m, options) if self.rando else None

        print("variants into the composite source")
        base = m / "composite-src.base"
        self.composite(o / "translated/dol/generated", o / "translated/rels/generated/rels", o / "game/rels",
                       o / "game/main.dol", base, "mods-base-composite")
        # The --mod and --combo specs are colon-separated, and a Windows path
        # has a colon after its drive letter: run in the build directory and
        # name the mod trees relative to it. The mods keep build_mods.sh's
        # order, which numbers them.
        self.run("mods-variants", [
            sys.executable, ROOT / "scripts/mods/build_mod_variants.py", "--composite-src", base,
            "--base-dol", o / "game/main.dol",
            "--mod", "widescreen:mods/widescreen/composite-src:mods/widescreen/main.dol:mods/widescreen/runtime.json",
            "--mod", "betterww:mods/betterww/composite-src:game/main.dol",
            "--mod", "widescreen1610:mods/widescreen1610/composite-src:mods/widescreen1610/main.dol:"
                     "mods/widescreen1610/runtime.json",
            "--combo", "widescreen+betterww:mods/combo/composite-src",
            "--combo", "widescreen1610+betterww:mods/combo1610/composite-src",
            *(rando["mod"] if rando else ()),
            "--exclusive", "widescreen,widescreen1610",
            "--options", rando["options"] if rando else options, "--rels-bin-dir", o / "game/rels"], cwd=o)
        dst = o / "composite-src"
        if (base / "generated.h").read_bytes() != (dst / "generated.h").read_bytes():
            die(f"{dst} was generated from other inputs; rebuild it first")
        # The base tree plus the variants: only the variant chunks and the
        # files that list them change.
        sync_tree(base, dst)
        (o / "composite-final.digest").write_text(tree_digest(dst) + "\n")
        (o / "mods.done").write_text("complete\n")

    def build_rando_trees(self, m, options):
        """The randomizer as a mod (bit 3, after Better Wind Waker's and the two widescreens, so their masks stay): the
        reference game translated with the randomizer's option sites and seed hooks, and the trees the combinations with
        the other mods need (a chunk that two mods both change gets a variant of its own, build_mod_variants --combo).
        Returns the arguments of build_mod_variants."""
        o = self.out
        r = o / "rando"
        rm = m / "rando"
        rm.mkdir(parents=True, exist_ok=True)
        merged = rm / "options-all.txt"
        # An option site of Better Wind Waker's options.txt at an address the randomizer's own option sites (the reference
        # game's words, switched by rando_no_*) already hold is left out here: the randomizer's switch governs it in this tree.
        rando_text = (r / "sites/rando-options.txt").read_text()
        taken = {line.split()[1] for line in rando_text.splitlines() if line.startswith("site ")}
        kept = [line for line in options.read_text().splitlines()
                if not (line.startswith("site ") and line.split()[1] in taken)]
        merged.write_text("\n".join(kept) + "\n" + rando_text, newline="\n")

        def site_list(name, source):
            listed = subprocess.run([sys.executable, ROOT / "scripts/mods/game_options.py", "sites", source],
                                    capture_output=True, check=True).stdout
            (rm / name).write_bytes(listed.replace(b"\r\n", b"\n"))
            return ("--option-sites", rm / name)

        rando_sites = site_list("sites-rando.txt", r / "sites/rando-options.txt")
        all_sites = site_list("sites-all.txt", merged)
        dol = r / "game/main.dol"
        # Which DOL, which sites, and which REL translation (the RELs are translated only where they differ).
        trees = [("rando", dol, rando_sites, True), ("rando_betterww", dol, all_sites, True)]
        for name, gecko in (("widescreen", "GZLE01.gecko"), ("widescreen1610", "GZLE01-16x10.gecko")):
            patched = rm / f"{name}.dol"
            self.run(f"mods-rando-{name}-gecko", [sys.executable, ROOT / "scripts/mods/gecko_apply.py",
                                                  ROOT / "mods/widescreen" / gecko, dol, patched, rm / f"{name}-runtime.json"])
            trees.append((f"rando_{name}", patched, rando_sites, False))
            trees.append((f"rando_{name}_betterww", patched, all_sites, False))
        for name, source, sites, with_rels in trees:
            print(f"randomizer: {name}")
            (rm / name).mkdir(exist_ok=True)
            self.translate(source, rm / name / "translated", r / "game/rels" if with_rels else None,
                           name=f"mods-{name}-translate", sites=sites, staged=True)
            rel_tree = name if with_rels else ("rando_betterww" if name.endswith("_betterww") else "rando")
            self.composite(rm / name / "translated/dol/generated", rm / rel_tree / "translated/rels/generated/rels",
                           r / "game/rels-translate", source, rm / name / "composite-src", f"mods-{name}-composite")
        rel = lambda path: path.relative_to(o).as_posix()
        return {"options": merged, "mod": [
            "--mod", f"rando:{rel(rm)}/rando/composite-src:{rel(dol)}",
            "--layout", "rando", "--layout-rels", f"rando={rel(r)}/game/rels-translate",
            "--switchable", "rando",
            "--combo", f"betterww+rando:{rel(rm)}/rando_betterww/composite-src",
            "--combo", f"widescreen+rando:{rel(rm)}/rando_widescreen/composite-src",
            "--combo", f"widescreen1610+rando:{rel(rm)}/rando_widescreen1610/composite-src",
            "--combo", f"widescreen+betterww+rando:{rel(rm)}/rando_widescreen_betterww/composite-src",
            "--combo", f"widescreen1610+betterww+rando:{rel(rm)}/rando_widescreen1610_betterww/composite-src"]}

    def prepare_blocks(self):
        """Explicit generic optimization, after variants and before compilation.

        generate() verifies both the input fingerprint and final tree digest.
        Interrupted/edited preparation cannot be mistaken for finished work.
        """
        o = self.out
        # A rerun on a finished build: the tree is the prepared one already
        # (its digest is the one recorded after preparation), and the native
        # passes refuse to patch their own output again.
        receipt = o / "prepared-blocks.json"
        if getattr(self, "composite_current", False) and not self.mods_pending and receipt.exists():
            try:
                done = json.loads(receipt.read_text()).get("final_digest")
            except (OSError, ValueError):
                done = None
            if done and done == tree_digest(o / "composite-src"):
                print("the prepared composite source is current")
                return
        script = ROOT / "scripts/windows/fast_blocks.py"
        cpu_script = ROOT / "scripts/windows/global_guest_cpu.py"
        if self.args.native_game_math:
            self.run("native-game-math", [sys.executable, ROOT / "scripts/windows/native_game_math.py",
                                          o / "composite-src"])
        if self.args.native_j3d:
            self.run("native-j3d", [sys.executable, ROOT / "scripts/mods/prepare_native_j3d.py",
                                     o / "composite-src"])
        if self.args.native_vec:
            self.run("native-vec", [sys.executable, ROOT / "scripts/mods/prepare_native_vec.py",
                                     o / "composite-src"])
        if self.args.native_math:
            self.run("native-math", [sys.executable, ROOT / "scripts/mods/prepare_native_math.py",
                                     o / "composite-src"])
        if self.args.native_skin:
            self.run("native-skin", [sys.executable, ROOT / "scripts/windows/native_skin.py",
                                      o / "composite-src"])
        if self.args.fixed_cpu:
            self.run("fixed-cpu", [sys.executable, cpu_script, o / "composite-src"])
        if self.args.inline_fp or self.args.gather_pipe:
            helpers = [sys.executable, ROOT / "scripts/windows/chunk_headers.py", o / "composite-src"]
            if self.args.inline_fp:
                helpers.append("--inline-fp")
            if self.args.gather_pipe:
                helpers.append("--gather-pipe")
            self.run("inline-helpers", helpers)
        if self.args.inline_gpr:
            self.run("inline-gpr", [sys.executable, ROOT / "scripts/windows/inline_save_restore_gpr.py",
                                     o / "composite-src"])
        if self.args.prepared_blocks:
            self.run("prepared-blocks", [sys.executable, script, o / "composite-src"]
                     + (["--lean"] if getattr(self.args, "lean_blocks", False) else []))
        if self.args.direct_calls:
            self.run("direct-calls", [sys.executable, ROOT / "scripts/windows/direct_calls.py",
                                       o / "composite-src"])
        # Elliott Tate's Windows steps, off by default. Each changes only what it
        # can prove: lean_memory.py needs the prepaid copies' deadline test, and
        # native_entries.py hooks a native only where the translation hashes to the
        # one its comparison test was run on (it reports the rest as not hooked).
        if self.args.lean_memory:
            self.run("lean-memory", [sys.executable, ROOT / "scripts/windows/lean_memory.py",
                                      o / "composite-src"])
        # The deadline test after a lean access only on its way out of line, where the
        # deadline can move (lean_deadlines.py; needs --lean-memory).
        if getattr(self.args, "lean_deadlines", False):
            self.run("lean-deadlines", [sys.executable, ROOT / "scripts/windows/lean_deadlines.py",
                                         o / "composite-src"])
        # A return into another chunk leaves the return dispatch before its switch
        # (Elliott Tate's 7aca42a): with direct calls, as both shape how a chunk is left.
        if self.args.direct_calls:
            self.run("return-ranges", [sys.executable, ROOT / "scripts/windows/return_ranges.py",
                                        o / "composite-src"])
        if self.args.native_entries:
            self.run("native-entries", [sys.executable, ROOT / "scripts/windows/native_entries.py",
                                         o / "composite-src"])
        # Game-behaviour timing (behaviour_timing.py; on by default, --no-behaviour-timing):
        # every block runs its prepaid copy, a device event lands at the next block's
        # start, and the turn's budget is checked at loops and calls. Last.
        if getattr(self.args, "behaviour_timing", False):
            self.run("behaviour-timing", [sys.executable, ROOT / "scripts/windows/behaviour_timing.py",
                                           o / "composite-src", "--loop-checks"])
        # The pc at every option hook site, stored before the host's hook runs (hook_pc.py): the prepaid
        # copies leave pc stores out, and host hooks read it (2026.10.16a's pause backdrop). After the rest.
        self.run("hook-pc", [sys.executable, ROOT / "scripts/windows/hook_pc.py", o / "composite-src"])
        digest = tree_digest(o / "composite-src")
        receipt = {"enabled": self.args.prepared_blocks,
                   "fixed_cpu": self.args.fixed_cpu,
                   "fixed_mem1": self.args.fixed_mem1,
                   "inline_fp": self.args.inline_fp,
                   "gather_pipe": self.args.gather_pipe,
                   "direct_calls": self.args.direct_calls,
                   "inline_gpr": self.args.inline_gpr,
                   "native_j3d": self.args.native_j3d,
                   "native_vec": self.args.native_vec,
                   "native_math": self.args.native_math,
                   "native_skin": self.args.native_skin,
                   "native_game_math": self.args.native_game_math,
                   "lean_memory": self.args.lean_memory,
                   "lean_deadlines": getattr(self.args, "lean_deadlines", False),
                   "behaviour_timing": getattr(self.args, "behaviour_timing", False),
                   "lean_blocks": getattr(self.args, "lean_blocks", False),
                   "native_entries": self.args.native_entries,
                   "gather_sha256": {name: sha256_file(ROOT / "cmake/composite" / name)
                                     for name in ("gather_pipe.h", "gather_pipe.c", "gather_pipe_batch.h")},
                   "inline_fp_script_sha256": sha256_file(ROOT / "scripts/windows/chunk_headers.py"),
                   "inline_fp_header_sha256": sha256_file(ROOT / "cmake/composite/inline_fp.h"),
                   "fixed_cpu_script_sha256": sha256_file(cpu_script),
                   "script_sha256": sha256_file(script),
                   "base_digest": (o / "composite-src.digest").read_text().strip(),
                   "final_digest": digest}
        pending = o / "prepared-blocks.json.tmp"
        pending.write_text(json.dumps(receipt, indent=2) + "\n")
        os.replace(pending, o / "prepared-blocks.json")
        pending = o / "composite-final.digest.tmp"
        pending.write_text(digest + "\n")
        os.replace(pending, o / "composite-final.digest")

    # --- 8 compile -----------------------------------------------------------
    def compile_module(self):
        flags = []
        if self.profile is not None:
            # The profile is a compiler input but not a header dependency: its
            # hash in the file name makes Ninja recompile when the counts change.
            # Code the training never ran is optimized as cold; that saves size
            # and costs nothing in the scenes that matter (docs/BUILDER.md).
            flags = [f"-fprofile-instr-use={self.profile.as_posix()}", "-Wno-profile-instr-unprofiled",
                     "-Wno-profile-instr-out-of-date", "-Wno-backend-plugin"]
            print(f"with the optimization profile {self.profile.name}")
        tiered = self.profile is not None and not getattr(self.args, "no_tiered", False)
        cold = self.cold_sources() if tiered else None
        return self.compile_composite(self.out / "composite", self.args.opt_level, flags, [], "composite", cold)

    def cold_sources(self):
        """The chunks whose function the training never ran, listed for
        cmake/composite to compile at -O1 without GVN's memory dependence
        analysis (the module's longest passes on its largest functions), as
        DeepSea compiles its cold actor code: the module compiles in about 15
        minutes instead of 39, and Gohma's room, which no training visits, ran
        as fast as with the whole module at -O2 (2026-10-02; the profile had
        already compiled those chunks for size). --no-tiered compiles them all
        at -O2. A chunk is one function, named func_<its file's address>."""
        stats = subprocess.run([self.llvm_profdata, "show", "--all-functions", self.profile], capture_output=True,
                               text=True).stdout
        counts = {name.upper(): int(count) for name, count in
                  re.findall(r"(?m)^  func_([0-9A-Fa-f]+):\n(?:    .*\n)*?    Function count: (\d+)", stats)}
        src = self.out / "composite-src"
        cold = []
        for path in sorted(src.glob("chunks_*/*.c")):
            address = path.stem.rsplit("_", 1)[-1].upper()
            if counts.get(address) == 0:
                cold.append(path.relative_to(src).as_posix())
        listing = self.out / "composite-cold-sources.txt"
        listing.write_text("\n".join(cold) + "\n", encoding="utf-8")
        print(f"tiered: {len(cold)} chunks the training never ran at -O1")
        return listing

    def compile_composite(self, build, opt_level, extra_flags, extra_link_flags, name, cold=None):
        rc = self.recompcore
        # Each chunk is one very large function, and two LLVM passes are
        # superlinear on it (clang 22, x86-64, measured with -ftime-report):
        # - the SLP vectorizer took 92 percent of a typical large chunk's time;
        #   -fno-slp-vectorize took the 1.8 MB chunks from over 30 minutes each
        #   to about 2. There is little to vectorize across register moves.
        # - the register coalescer took 95 percent of d_a_movie_player's 44
        #   minutes, joining copies into the context pointer's function-long
        #   live interval over and over. Capping that per large interval took
        #   it to about 2 minutes, at the cost of a few register copies.
        flags = (f"-march={self.args.march} -fno-slp-vectorize "
                 "-mllvm -large-interval-freq-threshold=10")
        flags += " " + subprocess.list2cmdline(extra_flags)
        link_flags = subprocess.list2cmdline(["-fuse-ld=lld", *extra_link_flags])
        self.run(f"{name}-configure", [
            "cmake", "-S", ROOT / "cmake/composite", "-B", build, "-G", "Ninja", "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_SHARED_LINKER_FLAGS={link_flags}",
            f"-DBLUEWAKE_FIXED_CPU={'ON' if self.args.fixed_cpu else 'OFF'}",
            f"-DBLUEWAKE_NATIVE_J3D={'ON' if self.args.native_j3d else 'OFF'}",
            f"-DBLUEWAKE_NATIVE_VEC={'ON' if self.args.native_vec else 'OFF'}",
            f"-DBLUEWAKE_NATIVE_GAME_MATH={'ON' if self.args.native_game_math else 'OFF'}",
            f"-DBLUEWAKE_NATIVE_SKIN={'ON' if self.args.native_skin else 'OFF'}",
            f"-DBLUEWAKE_NATIVE_MATH={'ON' if self.args.native_math else 'OFF'}",
            f"-DBLUEWAKE_NATIVE_ENTRIES={'ON' if self.args.native_entries else 'OFF'}",
            f"-DBLUEWAKE_DIRECT_CALLS={'ON' if self.args.direct_calls else 'OFF'}",
            f"-DBLUEWAKE_GATHER_PIPE={'ON' if self.args.gather_pipe else 'OFF'}",
            f"-DBLUEWAKE_INLINE_FP={'ON' if self.args.inline_fp else 'OFF'}",
            f"-DBLUEWAKE_FIXED_MEM1={'ON' if self.args.fixed_mem1 else 'OFF'}",
            f"-DCOMPOSITE_OPTIMIZATION_LEVEL={opt_level}", f"-DCOMPOSITE_DIR={self.out / 'composite-src'}",
            f"-DGXRUNTIME_DIR={rc / 'GXRuntime'}", f"-DABI_DIR={rc / 'Source/Core/Core/PowerPC/StaticRecomp'}",
            f"-DCOMPOSITE_COLD_SOURCES_FILE={cold if cold is not None else ''}"])
        # -k 0: a chunk that fails does not stop the others. The usual cause is
        # memory (clang reports "out of memory" when several of the largest
        # chunks peak together), so what failed is retried with fewer jobs.
        jobs = self.args.jobs
        while True:
            try:
                self.run(f"{name}-build", ["cmake", "--build", build, "-j", jobs, "--", "-k", "0"], ninja=True)
                break
            except BuildError:
                log = (self.logs / f"{name}-build.log").read_text(errors="replace")
                if jobs <= 1 or "out of memory" not in log:
                    raise
                jobs = max(1, jobs // 2)
                print(f"  some chunks ran out of memory; compiling the rest with {jobs} jobs")
        module = build / MODULE
        if not module.exists():
            die("the game module was not produced")
        return module

    # --- local optimization training ---------------------------------------
    # Local profile-guided optimization: the game module is
    # compiled with LLVM's instrumentation, the normal app plays the opening to
    # player control headless with it, and the counts it records guide the
    # optimized compile. The profile is made from the game, so it is private and
    # stays in the build directory. Adapted from Elliott Tate's Windows builder
    # (4fbcc7f and follow-ups through 7ca0cb9). Unlike the Mac, the
    # bundled Apple-silicon profiles are not used: this one covers the runtime
    # in the module too, from this compiler.
    TRAINING_VERSION = "bluewake-2"  # the tour of the game (Elliott Tate's TRAINING_VERSION 4)
    TRAINING_RETRACES = 23000
    # After player control (retrace 20,257 on the lookout), Link runs - off the
    # lookout, around Outset, turning - instead of standing until the end: the
    # collision, movement and animation code that play spends its time in is
    # then trained hot, not compiled cold for size. retrace:buttons:length:x:y.
    TRAINING_RUN = ["20400:0:700:0:127", "21100:0:500:90:110", "21600:0:500:-90:110", "22100:0:900:0:127"]
    # Then the plain playback tours the game (BLUEWAKE_TEST_WARP, a scene change
    # as a door makes it): towns, islands, dungeons, the sea from the boat. The
    # opening and Outset alone left most of the game's code untrained (288 of
    # 813 translated functions ran), so a dungeon's or the sea's code was
    # compiled cold, for size. Each stop: the scene loads, then Link runs,
    # turns and runs on. stage:room:point.
    TRAINING_TOUR = ["sea:11:1",        # Windfall Island
                     "sea:13:0",        # Dragon Roost Island
                     "M_NewD2:0:0",     # Dragon Roost Cavern
                     "sea:41:0",        # Forest Haven
                     "kindan:0:0",      # the Forbidden Woods
                     "Siren:0:0",       # the Tower of the Gods
                     "majroom:0:0",     # the Forsaken Fortress
                     "sea:1:100",       # the sea by the Fortress, on the boat
                     "Hyrule:0:0",      # Hyrule Castle
                     "sea:44:0"]        # back to Outset
    TOUR_START = 23200
    TOUR_STOP = 1500  # retraces at each stop

    def training_tour(self):
        """The tour's warps, its runs at each stop, and the retraces it ends at."""
        warps, moves = [], []
        for i, place in enumerate(self.TRAINING_TOUR):
            at = self.TOUR_START + i * self.TOUR_STOP
            warps.append(f"{at}:{place}")
            moves += [f"{at + 450}:0:300:0:127", f"{at + 780}:0:300:110:60", f"{at + 1110}:0:300:-110:60"]
        return warps, moves, self.TOUR_START + len(self.TRAINING_TOUR) * self.TOUR_STOP + 300

    def training_fingerprint(self):
        """Bind local counts to actual prepared source, compiler and playback code."""
        key = hashlib.sha256()
        key.update(json.dumps({"recipe": self.TRAINING_VERSION,
                               "compiler": self.clang_version, "march": self.args.march,
                               "mods": self.mods,
                               "options": {name: getattr(self.args, name, False) for name in
                                           ("prepared_blocks", "fixed_cpu", "fixed_mem1", "inline_fp",
                                            "gather_pipe", "direct_calls", "inline_gpr", "native_j3d",
                                            "native_vec", "native_math", "native_skin", "native_game_math",
                                            "lean_memory", "native_entries", "lean_blocks")},
                               **({"lean_deadlines": True} if getattr(self.args, "lean_deadlines", False) else {}),
                               **({"behaviour_timing": True} if getattr(self.args, "behaviour_timing", False) else {}),
                               "runtime": "vendored",
                               "source": tree_digest(self.out / "composite-src")},
                              sort_keys=True).encode())
        for folder in ("cmake/composite", "runtime/host/src", "windows/src", "runtime/recompcore/GXRuntime",
                       "runtime/recompcore/Source/Core/Core/PowerPC/StaticRecomp"):
            for path in sorted((ROOT / folder).rglob("*")):
                if path.is_file():
                    key.update(path.relative_to(ROOT).as_posix().encode())
                    key.update(path.read_bytes())
        key.update(Path(__file__).read_bytes())
        return key.hexdigest()

    def train(self):
        work = self.out / "pgo-local"
        work.mkdir(parents=True, exist_ok=True)
        profile = work / "composite.profdata"
        receipt = work / "training.json"
        key = self.training_fingerprint()
        if profile.exists() and receipt.exists() and not self.args.retrain:
            try:
                previous = json.loads(receipt.read_text())
            except ValueError:
                previous = {}
            if previous.get("fingerprint") == key and previous.get("profile") == sha256_file(profile):
                print("reusing the optimization profile trained for these inputs")
                return self.hashed_profile(profile)
            if getattr(self.args, "reuse_profile", False) and previous.get("profile") == sha256_file(profile):
                # Inputs changed (a hook site, host code): the profile's counts still
                # fit every function that did not change; clang compiles the others
                # without counts (the out-of-date warnings say which).
                print(f"reusing the optimization profile trained {previous.get('trained', '?')} "
                      "for earlier inputs (--reuse-profile)")
                return self.hashed_profile(profile)

        exe = self.build_app()
        start = time.monotonic()
        # -O1, not -O0: with the lean prepaid copies the -O0 instrumented module passes 2 GB, and Windows refuses an image
        # that large (error 193). The counters are the front end's, the same at either level (upstream trains at -O1 too).
        module = self.compile_composite(work / "composite", "1", ["-fprofile-instr-generate"],
                                        ["-fprofile-instr-generate"], "training-composite")
        print(f"instrumented game module built ({int(time.monotonic() - start) // 60} min)")
        attempt = Path(tempfile.mkdtemp(prefix="attempt-", dir=work))
        raw = []
        # The opening as a new player plays it, then again with widescreen and
        # Better Wind Waker's options, so the chunks those mods replace are
        # optimized for play too rather than as code that never ran.
        runs = [("plain", None)]
        if self.mods:
            runs.append(("mods", "widescreen,betterww"))
        for name, mods in runs:
            raw += self.training_run(exe, module, attempt / f"run-{name}", mods, tour=name == "plain")
        candidate = attempt / "composite.profdata"
        self.run("training-merge", [self.llvm_profdata, "merge", "-o", candidate, *raw])
        shown = subprocess.run([self.llvm_profdata, "show", "--all-functions", candidate], capture_output=True,
                               text=True, env=self.env)
        if shown.returncode:
            die(f"llvm-profdata could not read the new profile; previous profile retained (see {candidate})")
        stats = shown.stdout
        executed = [int(c) for c in re.findall(r"(?m)^  func_[0-9A-Fa-f]+\S*:\n(?:    .*\n)*?    Function count: (\d+)",
                                               stats)]
        ran = sum(1 for count in executed if count > 0)
        if ran == 0:
            die("the optimization profile counted no translated game functions")
        print(f"optimization profile: {ran} translated functions ran ({len(executed)} in the module)")
        os.replace(candidate, profile)
        pending = receipt.with_suffix(".tmp")
        pending.write_text(json.dumps({"fingerprint": key, "profile": sha256_file(profile),
                                       "trained": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                                       "executed_functions": ran}, indent=2))
        os.replace(pending, receipt)
        return self.hashed_profile(profile)

    def training_run(self, exe, module, run, mods, tour=False, headless=True):
        """One headless playback of the opening: boot, A at the title, the
        opening cutscene's text confirmed, player control on Outset, and with
        `tour` the tour of the game after it. A new card in its own folder; the
        player's saves are never touched. headless=False draws it in a window
        (the app's own training, scripts/windows/train_app_profile.py)."""
        warps, tour_moves, tour_end = self.training_tour() if tour else ([], [], self.TRAINING_RETRACES)
        run.mkdir(parents=True)
        env = {k: v for k, v in (self.env or os.environ).items() if not k.startswith(("BLUEWAKE_", "DOL_", "LLVM_PROFILE_"))}
        env.update({
            "LLVM_PROFILE_FILE": str(run / "%m-%p.profraw"),
            "BLUEWAKE_DATA_DIR": str(run), "BLUEWAKE_NO_DIALOG": "1",
            "BLUEWAKE_DOL": str(self.out / "game/main.dol"), "BLUEWAKE_RELS_DIR": str(self.out / "game/rels"),
            "BLUEWAKE_DISC": str(self.iso),
            "BLUEWAKE_DSP_IROM": str(self.recompcore / "Data/Sys/GC/dsp_rom.bin"),
            "BLUEWAKE_DSP_COEF": str(self.recompcore / "Data/Sys/GC/dsp_coef.bin"),
            "BLUEWAKE_MAX_RETRACES": str(tour_end), "BLUEWAKE_WALL_PACE": "0",
            "BLUEWAKE_PLAYER_PROBE": "1", "BLUEWAKE_PAD_BUTTONS": "0x0100",
            # The player-control milestone below waits on the overlap phase
            # this observation latches; the Windows app turns it off for play.
            "BLUEWAKE_OVERLAP_OBSERVATION": "1",
            "BLUEWAKE_PAD_PULSE_ON_TITLE_READY": "1", "BLUEWAKE_PAD_PULSE_LENGTH": "2",
            "BLUEWAKE_PAD_CONFIRM_EVENT": "any",
            "BLUEWAKE_PAD_SCRIPT": ",".join([f"{n}:0x0100:2" for n in range(17800, 22001, 150)] + self.TRAINING_RUN +
                                            tour_moves),
        })
        env["DOL_AURORA_FRAME_INTERP"] = "0"
        for option, name in (("direct_calls", "BLUEWAKE_DIRECT_CALLS"),
                             ("gather_pipe", "BLUEWAKE_GATHER_PIPE"),
                             ("native_j3d", "BLUEWAKE_NATIVE_J3D"),
                             ("native_vec", "BLUEWAKE_NATIVE_VEC"),
                             ("native_math", "BLUEWAKE_NATIVE_MATH"),
                             ("native_skin", "BLUEWAKE_NATIVE_SKIN"),
                             ("native_game_math", "BLUEWAKE_NATIVE_GAME_MATH")):
            env[name] = "1" if getattr(self.args, option) else "0"
        if warps:
            env["BLUEWAKE_TEST_WARP"] = ",".join(warps)
        if headless:
            env["BLUEWAKE_RENDERER"] = "headless"
        if mods:
            env["BLUEWAKE_MODS"] = mods
        # A playback is a game on this PC: it takes the scenario runner's
        # machine-wide lock (one game at a time) and stays off the desktop
        # (the spare monitor, never focused, no sound), as scenario.py's runs do.
        env.setdefault("BLUEWAKE_WINDOW_POS", os.environ.get("BLUEWAKE_TEST_WINDOW_POS", "4060,40"))
        env["BLUEWAKE_WINDOW_NOACTIVATE"] = "1"
        env["SDL_AUDIO_DRIVER"] = "dummy"
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from machine_lock import machine_lock
        with machine_lock("scenario"):
            log = self.run(f"training-playback-{run.name[4:]}", [exe, "--module", module], env=env)
        text = log.read_text(errors="replace")
        if "[player-milestone] control-admitted" not in text:
            die(f"the training playback did not reach player control; profile rejected (see {log})")
        raw = sorted(run.glob("*.profraw"))
        if not raw:
            die(f"the training playback wrote no profile (see {log})")
        return raw

    def hashed_profile(self, profile):
        folder = self.out / "profiles"
        folder.mkdir(exist_ok=True)
        target = folder / f"composite-{sha256_file(profile)[:16]}.profdata"
        if not target.exists() or sha256_file(target) != sha256_file(profile):
            shutil.copy2(profile, target)
        return target

    # --- 9 app ---------------------------------------------------------------
    def build_app(self):
        self.run("app-build", ["cmake", "--build", self.app_build, "--target", "BlueWake", "-j", self.args.jobs],
                 ninja=True)
        exe = self.app_build / "boat-of-daphnes.exe"
        if not exe.exists():
            die("boat-of-daphnes.exe was not produced")
        return exe

    # --- 10 package ----------------------------------------------------------
    def package(self, module):
        app = self.out / "boat-of-daphnes"
        (app / "game").mkdir(parents=True, exist_ok=True)
        (app / "dsp").mkdir(exist_ok=True)
        copied = []
        for f in sorted(self.app_build.iterdir()):
            if f.suffix.lower() in (".exe", ".dll") and f.name != "bluewake_disc_extract.exe" or \
                    f.name == "initial_pipeline_cache.db":
                shutil.copy2(f, app / f.name)
                copied.append(f.name)
        nodtool = ROOT / "build/tools/nodtool/bin/nodtool.exe"
        if nodtool.is_file():
            shutil.copy2(nodtool, app / "nodtool.exe")
            copied.append("nodtool.exe")
        shutil.copy2(module, app / MODULE)
        # The Visual C++ runtime, app-local, so the folder also runs on a PC
        # without the redistributable installed (the UCRT ships with Windows).
        redist = Path(self.env.get("VCToolsRedistDir", "")) / "x64"
        crt = sorted(redist.glob("Microsoft.VC*.CRT")) if redist.is_dir() else []
        if crt:
            for name in ("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
                if (crt[-1] / name).exists():
                    shutil.copy2(crt[-1] / name, app / name)
        else:
            print("note: the Visual C++ runtime DLLs were not found to copy; the app needs the "
                  "Visual C++ Redistributable on other PCs")
        shutil.copy2(self.out / "game/main.dol", app / "game/main.dol")
        rels = app / "game/rels"
        shutil.rmtree(rels, ignore_errors=True)
        shutil.copytree(self.out / "game/rels", rels)
        if self.rando:
            # The RELs the randomizer variant changes or adds: the host reads them before the disc's (host_materialize_rel).
            target = app / "game/rando/rels"
            shutil.rmtree(app / "game/rando", ignore_errors=True)
            shutil.copytree(self.out / "rando/rels", target)
            print(f"randomizer RELs: {len(list(target.glob('*.rel')))} in {target}")
        for name in ("dsp_rom.bin", "dsp_coef.bin"):
            shutil.copy2(self.recompcore / "Data/Sys/GC" / name, app / "dsp" / name)
        self.place(self.iso, app / "game/GZLE01.iso")
        self.swift_sail(app)
        self.link_colors(app)
        self.bunny_hood(app)
        self.randomizer(app)
        dirty = bool(self.git("status", "--porcelain"))
        provenance = {
            "profile": "bluewake-windows",
            "containsTranslatedGameCode": True,
            "source_commit": self.git("rev-parse", "HEAD"),
            "source_modified": dirty,
            "lean_blocks": getattr(self.args, "lean_blocks", False),
            "composite_digest": (self.out / "composite-src.digest").read_text().strip(),
            "mods": bool(self.mods),
            "rando_variant": self.rando,
            "march": self.args.march,
            "prepared_blocks": self.args.prepared_blocks,
            "fixed_cpu": self.args.fixed_cpu,
            "fixed_mem1": self.args.fixed_mem1,
            "inline_fp": self.args.inline_fp,
            "gather_pipe": self.args.gather_pipe,
            "direct_calls": self.args.direct_calls,
            "inline_gpr": self.args.inline_gpr,
            "native_j3d": self.args.native_j3d,
            "native_vec": self.args.native_vec,
            "native_math": self.args.native_math,
            "native_skin": self.args.native_skin,
            "native_game_math": self.args.native_game_math,
            "local_training": self.profile is not None,
            "composite_profile_sha256": sha256_file(self.profile) if self.profile else "",
            "compiler": self.clang_version,
            "module_sha256": sha256_file(app / MODULE),
            "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        (app / "BuilderProvenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        (app / "README.txt").write_text(README)
        return app

    def swift_sail(self, app):
        """The Swift Sail's red sail, icon and item-get textures, made from this
        disc (scripts/windows/swift_sail.py, wwrando's look) into
        assets/swift_sail. The app loads them while Swift Sail is on."""
        target = app / "assets" / "swift_sail"
        shutil.rmtree(target, ignore_errors=True)
        result = subprocess.run([self.helper_python(), str(ROOT / "scripts/windows/swift_sail.py"), str(self.iso), str(target)],
                                capture_output=True, text=True)
        made = sorted(target.glob("*.png")) if target.is_dir() else []
        if result.returncode != 0 or len(made) != 3:
            shutil.rmtree(target, ignore_errors=True)
            reason = (result.stderr or result.stdout).strip().splitlines()
            print("note: the Swift Sail textures were not made"
                  + (f" ({reason[-1]})" if reason else "") + "; with Swift Sail on, the sail keeps its colours")
        else:
            print(f"Swift Sail textures: {len(made)} in {target}")

    def link_colors(self, app):
        """Link's textures from this disc and wwrando's color masks (the
        player's wwrando download) into assets/link_colors, for the Cosmetics
        Editor's live Link colors (scripts/windows/link_colors.py)."""
        target = app / "assets" / "link_colors"
        shutil.rmtree(target, ignore_errors=True)
        command = [self.helper_python(), str(ROOT / "scripts/windows/link_colors.py"), str(self.iso), str(target)]
        masks = os.environ.get("BLUEWAKE_WWRANDO_MASKS")
        if masks:
            command += ["--masks", masks]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or not (target / "index.json").is_file():
            shutil.rmtree(target, ignore_errors=True)
            reason = (result.stderr or result.stdout).strip().splitlines()
            print("note: Link's color data was not made" + (f" ({reason[-1]})" if reason else "")
                  + "; Link's colors in the Cosmetics Editor will not show")
        else:
            print(f"Link color data: {target}")

    def bunny_hood(self, app):
        """The Bunny Hood's model and icon from this disc (scripts/rando/bunny_hood.py, run with the randomizer
        runtime's Python because it needs gclib) into assets/bunnyhood."""
        target = app / "assets" / "bunnyhood"
        shutil.rmtree(target, ignore_errors=True)
        python = self.rando_python(self.out / "rando_runtime")
        if not python.is_file():
            print("note: no randomizer runtime, so the Bunny Hood's model was not made")
            return
        result = subprocess.run([str(python), str(ROOT / "scripts/rando/bunny_hood.py"), str(self.iso), str(target)],
                                capture_output=True, text=True)
        if result.returncode != 0 or not (target / "bunnyhood.arc").is_file():
            shutil.rmtree(target, ignore_errors=True)
            reason = (result.stderr or result.stdout).strip().splitlines()
            print("note: the Bunny Hood's model was not made" + (f" ({reason[-1]})" if reason else ""))
        else:
            print(f"Bunny Hood: {target}")

    def randomizer(self, app):
        """The randomizer's seed generator beside the game: wwrando's generator with its embedded Python
        (build/windows/rando_runtime, made by scripts/windows/rando_runtime.py) in rando/, and wwrando's
        options for the Randomizer menu in assets/randomizer/options.json."""
        runtime = self.out / "rando_runtime"
        if not self.rando_python(runtime).is_file():
            print("note: no randomizer runtime (python scripts/windows/rando_runtime.py makes it); "
                  "the Randomizer menu cannot generate seeds")
            return
        target = app / "rando"
        for part in self.rando_runtime_parts():
            shutil.copytree(runtime / part, target / part, dirs_exist_ok=True, symlinks=True)
        shutil.copy2(runtime / "manifest.json", target / "manifest.json")
        # The generator's scripts, and what first launch runs to make this package's disc-derived files again for another
        # disc (prepare_extras.py, swift_sail.py, link_colors.py, the reference options): scripts/windows/make_dist.py.
        sys.path.insert(0, str(ROOT / "scripts/windows"))
        import make_dist
        make_dist.install_extras(target)
        options = app / "assets" / "randomizer" / "options.json"
        env = dict(os.environ, RANDO_RUNTIME=str(runtime))
        result = subprocess.run([str(self.rando_python(runtime)), str(ROOT / "scripts/rando/describe_options.py"),
                                 str(options)], capture_output=True, text=True, env=env)
        if result.returncode != 0:
            print("note: the randomizer's options were not written: "
                  + ((result.stderr or result.stdout).strip().splitlines() or ["?"])[-1])
        else:
            print(f"randomizer: {target} and {options}")

    @staticmethod
    def place(source, target):
        """The disc goes beside the game: a hard link when it can, else a copy."""
        if target.exists():
            if os.path.samefile(source, target) or \
                    (target.stat().st_size == source.stat().st_size and
                     target.stat().st_mtime_ns >= source.stat().st_mtime_ns):
                return
            target.unlink()
        try:
            os.link(source, target)
        except OSError:
            print(f"copying {source.name} into the app folder ({source.stat().st_size >> 20} MB)")
            shutil.copy2(source, target)

    # --- the pipeline ------------------------------------------------------------
    def build(self):
        args = self.args
        self.mods = not args.no_mods
        self.mods_pending = self.mods
        self.rando = bool(args.rando_variant)
        self.rando_key = ""
        print(f"Building The Legend of Zelda: The Wind Waker (GameCube USA GZLE01 rev 0) for Windows from {args.disc}")
        step("1/10 tools")
        self.check_tools()
        step("2/10 dependencies")
        self.dependencies()
        step("3/10 disc")
        self.disc()
        if args.check_only:
            print("\ntools, dependencies and disc are ready.")
            return
        step("4/10 extract the game from the disc")
        self.dolrecomp = self.build_dolrecomp()
        self.configure_app()
        game = self.out / "game"
        stamp = self.out / "game.json"
        key = {"iso": str(self.iso), "size": self.iso.stat().st_size, "mtime": self.iso.stat().st_mtime_ns,
               "extractor": sha256_file(ROOT / "runtime/host/src/disc_import.c")}
        if (game / "main.dol").exists() and stamp.exists() and json.loads(stamp.read_text()) == key:
            print(f"reusing main.dol and the RELs in {game}")
        else:
            self.extract(self.iso, game, self.accept_sha1)
            stamp.write_text(json.dumps(key))
            print(f"main.dol and 415 RELs in {game}")
        step("5/10 translate")
        self.translate(game / "main.dol", self.out / "translated", game / "rels")
        chunks = len(list((self.out / "translated/dol/generated/chunks").glob("*.c")))
        print(f"translated: {chunks} DOL chunks and 415 RELs")
        if self.rando:
            step("5b rando-patch: wwrando's reference game, its option sites and seed hooks")
            self.rando_patch()
        step("6/10 generate the composite source")
        self.generate()
        if args.source_only:
            print(f"\nsource check passed: {self.out / 'composite-src'}. Rerun without --source-only to "
                  f"compile and build the app.")
            return
        step("7/10 mods")
        if not self.mods:
            print("skipped")
        elif not self.mods_pending:
            print("mods already in the composite source")
        else:
            self.build_mods()
        if args.variants_only:
            print(f"\nvariants built: {self.out / 'composite-src'}. Rerun without --variants-only to compile.")
            return
        self.prepare_blocks()
        if not (args.no_train or args.no_pgo):
            self.llvm_profdata = str(Path(self.clang).with_name("llvm-profdata.exe"))
            if not Path(self.llvm_profdata).is_file():
                die("llvm-profdata.exe is missing beside Visual Studio's clang; install its LLVM tools "
                    "or explicitly use --no-train for an untrained build")
            step("local optimization training (instrumented module and private opening playbacks)")
            self.profile = self.train()
        else:
            print("local training skipped: compiling without an optimization profile")
        step(f"8/10 compile the game module (-O{args.opt_level}, -march={args.march}; this is the long step)")
        start = time.monotonic()
        module = self.compile_module()
        print(f"game module: {module} ({int(time.monotonic() - start) // 60} min)")
        step("9/10 build the app")
        exe = self.build_app()
        print(f"app: {exe}")
        step("10/10 package")
        app = self.package(module)
        self.finished(app)

    def finished(self, app):
        print(f"\nboat-of-daphnes: {app}")
        print(f"  run {app / 'boat-of-daphnes.exe'}")
        print("  It contains game code translated from your disc and a copy of the disc: keep it for yourself.")
        print("  Saves: Save\\ beside the executable (a rebuild never touches them).")


README = """boat-of-daphnes for Windows: The Legend of Zelda: The Wind Waker (GameCube USA),
statically recompiled from your own disc. See docs/WINDOWS.md in the source.

This folder is a personal build: gGZLE01_recomp.dll is code translated from
your disc and game\\ holds your disc image. Never share or upload it.

Run boat-of-daphnes.exe. Its menu (F1 or Esc) holds every setting, as in Ship of Harkinian: Settings,
Enhancements (Better Wind Waker's options, the Cosmetics Editor), Randomizer, Network (online play) and Dev
Tools. The picture fills the window at any shape, with the HUD at its edges.

Keyboard: arrows D-pad, J A, K B, U X, I Y, W/A/S/D control stick,
H/F/T/G C-stick, E/R L/R, Q Z, Return START. Game controllers work too.
F1 or Esc settings, F11 fullscreen, F9 frame rate.

Everything else is kept beside the executable, as in Ship of Harkinian:
  Save\\         the memory card (GZLE01.card), console settings, save states
  mods\\         mods: folders or .zip/.rar/.7z (Settings > Mod Menu); texture packs in mods\\textures\\
  Randomizer\\   randomizer seeds and spoiler logs (Randomizer > General > Generate Randomizer)
  logs\\         session logs (settings, scenes, events, frame rate, cutscene audio, online play), crash logs
  cache\\        shader and pipeline caches
Rebuilding replaces the program files only; these folders are never touched.
"""


# The optimizations Wind Waker Recomp's Windows builder always prepares (fixed
# CPU and RAM storage, inline floating point and gather-pipe writes, inlined
# register saves, prepaid blocks, direct calls and the certified natives). The
# app enables each one only where the module it loads was prepared with it.
WINDOWS_DEFAULT_OPTIMIZATIONS = ("fixed_cpu", "fixed_mem1", "inline_fp", "gather_pipe", "inline_gpr",
                                 "prepared_blocks", "direct_calls", "native_j3d", "native_vec", "native_math",
                                 "native_skin", "native_game_math", "lean_blocks", "lean_memory", "lean_deadlines",
                                 "behaviour_timing")


def main(builder_class=None, default_out=None, adjust_parser=None):
    """The command line. The Linux builder (scripts/linux/build.py) runs it with its own Builder, build folder and options."""
    default_out = default_out or ROOT / "build/windows"
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disc", type=Path, help="your GZLE01 revision 0 disc image (.iso, .gcm, .rvz, ...)")
    parser.add_argument("--out", type=Path, default=default_out,
                        help=f"build directory (default {default_out.relative_to(ROOT).as_posix()}; must be git-ignored "
                             "inside the checkout)")
    parser.add_argument("--jobs", type=int, default=None,
                        help="parallel compile jobs (default: the cores, limited by free memory)")
    parser.add_argument("--march", default="x86-64-v3",
                        help="CPU level for the game module (default x86-64-v3: AVX2, FMA, BMI2 and MOVBE, "
                             "any Intel Haswell or AMD Zen or newer; lowered automatically on older CPUs)")
    parser.add_argument("--opt-level", choices=("1", "2"), default="2", help="game module optimization level")
    parser.add_argument("--no-tiered", action="store_true",
                        help="compile every chunk at -O2, not only those the optimization training ran "
                             "(a build about 25 minutes longer)")
    parser.add_argument("--no-mods", action="store_true", help="skip the widescreen and Better Wind Waker variants")
    parser.add_argument("--no-train", action="store_true",
                        help="skip local optimization training; compile without a profile")
    parser.add_argument("--no-pgo", action="store_true", help="alias for --no-train")
    parser.add_argument("--retrain", action="store_true", help="record a new local profile instead of reusing one")
    parser.add_argument("--reuse-profile", action="store_true",
                        help="use the local profile even when its inputs changed since it was trained (a faster "
                             "rebuild after small changes; changed functions are compiled without counts)")
    parser.add_argument("--prepared-blocks", action="store_true",
                        help="opt into experimental prepaid-block optimization (off by default; Windows timing pending)")
    parser.add_argument("--fixed-cpu", action="store_true",
                        help="opt into experimental fixed-address CPU state; requires a supporting app")
    parser.add_argument("--fixed-mem1", action="store_true",
                        help="opt into module-owned RAM; requires --fixed-cpu and a supporting app")
    parser.add_argument("--inline-fp", action="store_true",
                        help="opt into experimental inline floating-point helpers (off by default)")
    parser.add_argument("--gather-pipe", action="store_true",
                        help="opt into experimental gather/inline-memory wrappers (off by default; host writer setup is separate)")
    parser.add_argument("--inline-gpr", action="store_true",
                        help="inline certified register saves/restores; requires --direct-calls")
    parser.add_argument("--direct-calls", action="store_true",
                        help="opt into direct-call preparation (off by default; compatible host selection required)")
    parser.add_argument("--native-j3d", action="store_true",
                        help="prepare certified native J3D transforms; off by default, compatible host opt-in required")
    parser.add_argument("--native-vec", action="store_true",
                        help="prepare certified native vector functions; off by default, compatible host opt-in required")
    parser.add_argument("--native-game-math", action="store_true",
                        help="certify optional native game-math entry hooks (off by default)")
    parser.add_argument("--native-skin", action="store_true",
                        help="certify and enable optional native skinning preparation (off by default)")
    parser.add_argument("--native-math", action="store_true",
                        help="prepare certified native matrix functions; off by default, compatible host opt-in required")
    parser.add_argument("--lean-blocks", action="store_true",
                        help="Elliott Tate's original prepaid block copies (on by default; implies --prepared-blocks)")
    parser.add_argument("--no-lean-blocks", action="store_true",
                        help="the conservative prepaid block copies instead: smaller module, about 5 to 12%% slower")
    parser.add_argument("--lean-memory", action="store_true",
                        help="Wind Waker Recomp's lean loads and stores in prepaid copies (on by default; "
                             "needs --prepared-blocks)")
    parser.add_argument("--no-lean-memory", action="store_true",
                        help="no lean loads and stores (and so no --lean-deadlines): about 5%% slower")
    parser.add_argument("--lean-deadlines", action="store_true",
                        help="the deadline test after a lean access only on its way out of line (on by default; "
                             "needs --lean-memory)")
    parser.add_argument("--no-lean-deadlines", action="store_true",
                        help="keep the deadline test after every lean load and store: about 4%% slower")
    parser.add_argument("--behaviour-timing", action="store_true",
                        help="game-behaviour timing: every block runs its prepaid copy, a timer or interrupt lands at "
                             "the next block's start, the turn's budget is checked at loops and calls (on by default; "
                             "needs --prepared-blocks)")
    parser.add_argument("--no-behaviour-timing", action="store_true",
                        help="check the turn's budget after every block: about 9%% slower")
    parser.add_argument("--native-entries", action="store_true",
                        help="Wind Waker Recomp's certified native entries, second and third sets (off by default; "
                             "needs --direct-calls, --gather-pipe and --native-vec)")
    parser.add_argument("--console", action="store_true", help="build boat-of-daphnes.exe as a console program")
    parser.add_argument("--conservative", action="store_true",
                        help="build the plain translation, without the optimizations prepared by default "
                             "(the individual --... options then add them one at a time)")
    parser.add_argument("--no-app-pgo", action="store_true",
                        help="build the app without its committed optimization profile and ThinLTO")
    parser.add_argument("--accept-new-composite", action="store_true",
                        help="continue if the generated source differs from the verified one")
    parser.add_argument("--rando-iso", action="store_true",
                        help="DISC is a randomizer seed's disc made by scripts/rando/patch_game.py --iso-out (game id "
                             "GZLE99): build the game from its patched executable and modules (docs/randomizer.md)")
    parser.add_argument("--rando-variant", action="store_true",
                        help="build the randomizer into the game module as one more mod (wwrando's patches: step "
                             "rando-patch, then its trees and combinations in step 7; the module compile grows by about "
                             "a third). It needs build/windows/rando_runtime (scripts/windows/rando_runtime.py) and "
                             "scripts/windows/pins.txt's disc, and plays any seed pack with no build per seed")
    parser.add_argument("--variants-only", action="store_true",
                        help="stop after step 7 (translations, composite source and variants), before the compile")
    parser.add_argument("--source-only", action="store_true",
                        help="stop after generating the source: checks tools, disc and translation in minutes")
    parser.add_argument("--check-only", action="store_true", help="check tools, dependencies and the disc only")
    if adjust_parser is not None:
        adjust_parser(parser)
    args = parser.parse_args()
    # Wind Waker Recomp's Windows builds prepare all of these every time; BlueWake
    # matches that by default. --conservative builds the plain translation.
    if not args.conservative:
        for name in WINDOWS_DEFAULT_OPTIMIZATIONS:
            setattr(args, name, True)
    if args.no_lean_blocks:
        args.lean_blocks = False
    if args.no_lean_memory:
        args.lean_memory = False
    if args.no_lean_memory or args.no_lean_deadlines:
        args.lean_deadlines = False
    if args.no_behaviour_timing:
        args.behaviour_timing = False
    if args.lean_blocks:
        args.prepared_blocks = True
    if args.rando_variant and args.no_mods:
        parser.error("--rando-variant is a mod: it cannot be combined with --no-mods")
    if args.rando_variant and args.rando_iso:
        parser.error("--rando-variant and --rando-iso are two routes to the randomizer: choose one")
    if args.inline_gpr and not args.direct_calls:
        parser.error("--inline-gpr requires --direct-calls")
    if args.fixed_mem1 and not args.fixed_cpu:
        parser.error("--fixed-mem1 requires --fixed-cpu")
    if args.lean_memory and not args.prepared_blocks:
        parser.error("--lean-memory requires --prepared-blocks")
    if args.lean_memory and not args.gather_pipe:
        parser.error("--lean-memory requires --gather-pipe (its accesses call gather_pipe.h's helpers)")
    if args.behaviour_timing and not args.prepared_blocks:
        parser.error("--behaviour-timing requires --prepared-blocks")
    if args.lean_deadlines and not args.lean_memory:
        parser.error("--lean-deadlines requires --lean-memory")
    if args.native_entries and not (args.direct_calls and args.gather_pipe and args.native_vec):
        parser.error("--native-entries requires --direct-calls, --gather-pipe and --native-vec")
    if args.jobs is None:
        args.jobs = default_jobs()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.out = args.out.resolve()
    try:
        rel = args.out.relative_to(ROOT)
    except ValueError:
        rel = None
    if rel is not None:
        if str(rel) == ".":
            parser.error(f"--out must not be the checkout itself; use {default_out.relative_to(ROOT).as_posix()}")
        ignored = subprocess.run(["git", "check-ignore", "-q", str(args.out) + os.sep], cwd=ROOT).returncode == 0
        if not ignored:
            parser.error(f"--out inside this checkout must be git-ignored; use {default_out.relative_to(ROOT).as_posix()}")
    args.out.mkdir(parents=True, exist_ok=True)
    try:
        (builder_class or Builder)(args).build()
    except BuildError as error:
        print(f"\nbuilder: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nbuilder: interrupted; rerun the same command to continue", file=sys.stderr)
        sys.exit(130)


if __name__ == "__main__":
    main()
