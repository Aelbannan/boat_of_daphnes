#!/usr/bin/env python3
"""boat-of-daphnes builder for Linux: turn your own game disc into your own game, on your PC.

    python3 scripts/linux/build.py DISC [--out build/linux] [--rando-variant] [options]

DISC is your own The Wind Waker (GameCube, USA GZLE01 revision 0) image: an .iso or .gcm, or a Dolphin
.rvz/.wia/.gcz/.ciso/.nfs image (converted to an ISO with nodtool, which is built from crates.io with Rust's cargo on
first use; or convert it in Dolphin first).

This is scripts/windows/build.py on Linux: the same steps, the same preparation passes, mods and options, with the
Linux tools in place of Visual Studio's. See docs/linux.md.

Steps, each logged under OUT/logs:
  1 tools        clang 17+ and lld, CMake 3.25+, Ninja, git, Python 3.10+ (python3.12 for the randomizer's generator)
  2 dependencies the pinned DolRecomp source (ref/dolrecomp) and the randomizer runtime (a python3.12 venv)
  3 disc         check the disc; convert a compressed image to an ISO
  4 extract      main.dol and the 415 RELs from the disc (the disc is verified)
  5 translate    the game's PowerPC code to C (DolRecomp, built here)
  6 generate     the composite source, compared with the verified digest
  7 mods         widescreen 16:9 and 16:10 and Better Wind Waker's options (--no-mods skips); with --rando-variant also
                 the randomizer (wwrando's patches as one more mod)
  8 compile      the game module, gGZLE01_recomp.so (the long step); no profile-guided optimization on Linux yet
  9 app          boat-of-daphnes, Aurora (Vulkan or OpenGL through Dawn), SDL3 and the DSP
 10 package      the app folder OUT/boat-of-daphnes, laid out like the Windows one, ready to run

--install-module also copies the module to the player's data folder ($XDG_DATA_HOME/boat-of-daphnes), where a packaged
app (the AppImage) looks for it; --appimage makes OUT/boat-of-daphnes-x86_64.AppImage from the app folder (without
your module or disc unless --with-module).

The app folder contains code translated from YOUR disc and a copy of the disc: it is yours alone. Never share or upload
it. Your saves live beside the program in Save/ (or in ~/.local/share/boat-of-daphnes when the program's folder is not
writable); rebuilding never touches them.
"""
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]

# The Windows builder's pipeline, run with Linux's tools: its Builder is subclassed below.
_spec = importlib.util.spec_from_file_location("bod_windows_build", ROOT / "scripts/windows/build.py")
wb = importlib.util.module_from_spec(_spec)
sys.modules["bod_windows_build"] = wb
_spec.loader.exec_module(wb)
wb.MODULE = "gGZLE01_recomp.so"
die = wb.die
BuildError = wb.BuildError
sha256_file = wb.sha256_file


def default_jobs():
    """All cores, but no more parallel compiles than memory allows: the large translated chunks take 1 to 3 GB each in
    clang, and running out of memory kills the compiler (compile_composite retries with fewer jobs)."""
    cores = os.cpu_count() or 8
    try:
        with open("/proc/meminfo") as mem:
            for line in mem:
                if line.startswith("MemAvailable:"):
                    available = int(line.split()[1]) * 1024
                    return max(1, min(cores, int(available // (2.5 * 2**30))))
    except OSError:
        pass
    return cores


wb.default_jobs = default_jobs

# The x86-64 levels as /proc/cpuinfo names the features.
MARCH_FLAGS = {"x86-64": set(), "x86-64-v2": {"sse4_2", "popcnt"},
               "x86-64-v3": {"sse4_2", "popcnt", "avx", "avx2", "fma", "bmi1", "bmi2", "movbe", "abm"}}


def gcc_install_dir(clang_major):
    """clang 18 and older cannot read libstdc++ 14's headers (std::function: "member pointer has incomplete base type"):
    point it at gcc 13's when there is one. Empty for newer clang or when gcc 13 is not installed."""
    if clang_major >= 19:
        return []
    for version in (13, 12, 11):
        for base in ("/usr/lib/gcc/x86_64-linux-gnu", "/usr/lib/gcc/x86_64-pc-linux-gnu", "/usr/lib64/gcc/x86_64-pc-linux-gnu"):
            if (Path(base) / str(version)).is_dir():
                return [f"--gcc-install-dir={base}/{version}"]
    return []


class LinuxBuilder(wb.Builder):
    # --- 1 tools -----------------------------------------------------
    def check_tools(self):
        if platform.system() != "Linux":
            die("this builder is for Linux; on Windows use scripts/windows/build.py")
        if platform.machine().lower() not in ("x86_64", "amd64"):
            die(f"an x86-64 Linux PC is required (this is {platform.machine()})")
        if sys.version_info < (3, 10):
            die("Python 3.10 or newer is required")
        self.env = dict(os.environ)
        for tool in ("git", "cmake", "ninja", "clang", "clang++", "ld.lld"):
            if shutil.which(tool) is None:
                die(f"missing {tool}: install the packages listed in docs/linux.md "
                    "(Ubuntu 24.04: sudo apt install clang lld cmake ninja-build git ...)")
        version = subprocess.check_output(["cmake", "--version"], text=True).split()[2]
        if tuple(int(x) for x in version.split(".")[:2]) < (3, 25):
            die(f"CMake 3.25 or newer is required (found {version})")
        self.clang = shutil.which("clang")
        clang_version = subprocess.check_output([self.clang, "--version"], text=True).splitlines()[0]
        self.clang_major = int(re.search(r"version (\d+)", clang_version).group(1))
        if self.clang_major < 17:
            die(f"clang 17 or newer is required ({clang_version})")
        self.clang_version = clang_version
        self.llvm_profdata = None
        if getattr(self.args, "appimage", False):
            self.check_appimage_tools()
        self.check_march()
        print(f"{clang_version}; cmake {version}; ninja "
              f"{subprocess.check_output(['ninja', '--version'], text=True).strip()}; "
              f"{self.args.jobs} jobs; -march={self.args.march}")

    def check_appimage_tools(self):
        appimagetool = os.environ.get("APPIMAGETOOL", "appimagetool")
        missing = [tool for tool in (appimagetool, "desktop-file-validate", "readelf") if shutil.which(tool) is None]
        if shutil.which("magick") is None and shutil.which("convert") is None:
            missing.append("ImageMagick (magick or convert)")
        if missing:
            die("--appimage is missing packaging tools: " + ", ".join(missing) + " (see docs/linux.md)")

    def check_march(self):
        """The game module is compiled for --march; refuse a level this CPU lacks."""
        wanted = MARCH_FLAGS.get(self.args.march)
        if wanted is None:
            return
        have = set()
        try:
            for line in open("/proc/cpuinfo"):
                if line.startswith("flags"):
                    have = set(line.split(":", 1)[1].split())
                    break
        except OSError:
            return
        if not wanted <= have:
            fallback = "x86-64-v2" if MARCH_FLAGS["x86-64-v2"] <= have else "x86-64"
            print(f"this CPU lacks {self.args.march} ({', '.join(sorted(wanted - have))}); using -march={fallback}")
            self.args.march = fallback

    # --- 2 dependencies ------------------------------------------------
    def dependencies(self):
        super().dependencies()
        # The randomizer's generator and its runtime (python3.12 in a venv, pip's wheels, wwrando and gclib at pinned
        # digests): the Randomizer menu needs it, and --rando-variant needs it to patch the reference game.
        runtime = self.out / "rando_runtime"
        command = [sys.executable, ROOT / "scripts/windows/rando_runtime.py", "--runtime", runtime]
        try:
            self.run("rando-runtime", command, env=os.environ)
        except BuildError:
            if getattr(self.args, "rando_variant", False):
                raise
            print("note: the randomizer runtime could not be made (see the log; it needs python3.12 with venv and network "
                  "access); the Randomizer menu will not generate seeds")

    @staticmethod
    def rando_python(runtime):
        return runtime / "python" / "bin" / "python3"

    def rando_runtime_parts(self):
        # The wheels are in the venv's own site-packages.
        return ("python", "wwrando", "gclib")

    def helper_python(self):
        """Swift Sail's and Link's colors need Pillow: the randomizer runtime has it."""
        python = self.rando_python(self.out / "rando_runtime")
        return str(python) if python.is_file() else sys.executable

    # --- 3 disc ----------------------------------------------------------
    def nodtool(self):
        root = ROOT / "build/tools/nodtool"
        exe = root / "bin/nodtool"
        if exe.exists():
            return exe
        if shutil.which("cargo") is None:
            die("this disc image is compressed; converting it needs Rust's cargo (https://rustup.rs) "
                "to build nodtool, or convert it to .iso yourself (Dolphin: right-click the game, "
                "Convert File..., format ISO) and pass the .iso")
        print(f"building nodtool {wb.NODTOOL_VERSION} from crates.io (once)")
        self.run("nodtool-install", ["cargo", "install", "nodtool", "--version", wb.NODTOOL_VERSION, "--locked",
                                     "--root", root], env=os.environ)
        return exe

    # --- tools built from source ---------------------------------------
    def build_dolrecomp(self):
        build = self.out / "dolrecomp"
        self.run("dolrecomp-configure", ["cmake", "-S", self.dolrecomp_src, "-B", build, "-G", "Ninja",
                                         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang",
                                         "-DCMAKE_CXX_COMPILER=clang++", "-DDOLRECOMP_WARNINGS_AS_ERRORS=OFF"])
        self.run("dolrecomp-build", ["cmake", "--build", build, "--target", "dolrecomp", "-j", self.args.jobs],
                 ninja=True)
        return build / "dolrecomp"

    def configure_app(self, build=None, instrument=False):
        """The app: Aurora on Dawn (the prebuilt static package) with a vendored static SDL3, compiled with clang at the
        same CPU level as the game module (the FIFO worker's matrix work for Smooth Motion needs AVX2 and FMA). The C++
        runtime is linked in, so the program asks nothing of the player's libstdc++ (the AppImage audit checks)."""
        self.app_build = build or self.out / "app"
        gcc = " ".join(gcc_install_dir(self.clang_major))
        c_flags = f"-march={self.args.march} -g1 {gcc}".strip()
        link = f"-fuse-ld=lld -static-libstdc++ -static-libgcc {gcc}".strip()
        rando_sites = self.out / "rando/sites/rando_sites.inc" if getattr(self.args, "rando_variant", False) else ""
        self.run("app-configure", [
            "cmake", "-S", ROOT / "windows", "-B", self.app_build, "-G", "Ninja",
            "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_C_FLAGS={c_flags}", f"-DCMAKE_CXX_FLAGS={c_flags}", "-DBUILD_TESTING=OFF",
            f"-DCMAKE_EXE_LINKER_FLAGS={link}", f"-DCMAKE_SHARED_LINKER_FLAGS={link}",
            "-DCMAKE_DISABLE_FIND_PACKAGE_SQLite3=TRUE", "-DCMAKE_DISABLE_FIND_PACKAGE_absl=TRUE",
            "-DAURORA_DAWN_PROVIDER=package", "-DAURORA_DAWN_LINKAGE=static",
            "-DAURORA_SDL3_PROVIDER=vendor", "-DAURORA_SDL3_LINKAGE=static",
            f"-DBLUEWAKE_RANDO_SITES={rando_sites}"])

    # --- 4 extract ---------------------------------------------------------
    def extract(self, iso, game, accept_sha1=None):
        self.run("disc-extract-build", ["cmake", "--build", self.app_build, "--target", "bluewake_disc_extract"],
                 ninja=True)
        env = dict(self.env)
        if accept_sha1:
            env["BLUEWAKE_ACCEPT_DOL_SHA1"] = accept_sha1
        self.run(f"disc-extract{'-' + game.parent.name if accept_sha1 else ''}",
                 [self.app_build / "bluewake_disc_extract", iso, game], env=env)
        rels = len(list((game / "rels").glob("*.rel")))
        if rels != 415 and not (accept_sha1 and rels >= 415):
            die(f"expected 415 RELs in {game / 'rels'}, found {rels}")

    # --- 9 app ---------------------------------------------------------------
    def build_app(self):
        self.run("app-build", ["cmake", "--build", self.app_build, "--target", "BlueWake", "-j", self.args.jobs],
                 ninja=True)
        exe = self.app_build / "boat-of-daphnes"
        if not exe.exists():
            die("boat-of-daphnes was not produced")
        return exe

    # --- 10 package ----------------------------------------------------------
    def package(self, module):
        app = self.out / "boat-of-daphnes"
        (app / "game").mkdir(parents=True, exist_ok=True)
        (app / "dsp").mkdir(exist_ok=True)
        shutil.copy2(self.app_build / "boat-of-daphnes", app / "boat-of-daphnes")
        if (self.app_build / "initial_pipeline_cache.db").is_file():
            shutil.copy2(self.app_build / "initial_pipeline_cache.db", app / "initial_pipeline_cache.db")
        nodtool = ROOT / "build/tools/nodtool/bin/nodtool"
        if nodtool.is_file():
            shutil.copy2(nodtool, app / "nodtool")
        shutil.copy2(module, app / wb.MODULE)
        shutil.copy2(self.out / "game/main.dol", app / "game/main.dol")
        rels = app / "game/rels"
        shutil.rmtree(rels, ignore_errors=True)
        shutil.copytree(self.out / "game/rels", rels)
        if self.rando:
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
        provenance = {
            "profile": "boat-of-daphnes-linux",
            "containsTranslatedGameCode": True,
            "source_commit": self.git("rev-parse", "HEAD"),
            "source_modified": bool(self.git("status", "--porcelain")),
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
            "lean_blocks": getattr(self.args, "lean_blocks", False),
            "local_training": False,
            "compiler": self.clang_version,
            "module_sha256": sha256_file(app / wb.MODULE),
            "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        (app / "BuilderProvenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        (app / "README.txt").write_text(README)
        return app

    def finished(self, app):
        print(f"\nboat-of-daphnes: {app}")
        print(f"  run {app / 'boat-of-daphnes'}")
        print("  It contains game code translated from your disc and a copy of the disc: keep it for yourself.")
        print("  Saves: Save/ beside the program (a rebuild never touches them).")

    def install_module(self, app):
        """The module into the player's data folder, where a packaged app (the AppImage) finds it."""
        data = os.environ.get("XDG_DATA_HOME")
        folder = Path(data) / "boat-of-daphnes" if data and data.startswith("/") else Path.home() / ".local/share/boat-of-daphnes"
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copy2(app / wb.MODULE, folder / wb.MODULE)
        print(f"module installed: {folder / wb.MODULE}")

    def build(self):
        # Without --no-mods, --rando-variant etc. the Windows pipeline's own order and checks apply.
        super().build()
        if self.args.check_only or self.args.source_only or self.args.variants_only:
            return
        app = self.out / "boat-of-daphnes"
        if getattr(self.args, "install_module", False):
            self.install_module(app)
        if getattr(self.args, "appimage", False):
            image = self.out / "boat-of-daphnes-x86_64.AppImage"
            command = [ROOT / "scripts/linux/make_appimage.sh", self.out, image]
            if getattr(self.args, "with_module", False):
                command.append("--with-module")
            self.run("appimage", command)
            self.run("appimage-abi", [sys.executable, ROOT / "scripts/linux/check_appimage_abi.py", image, "2.39"]
                     + (["--personal"] if getattr(self.args, "with_module", False) else []))
            print(f"AppImage: {image}")


README = """boat-of-daphnes for Linux: The Legend of Zelda: The Wind Waker (GameCube USA),
statically recompiled from your own disc. See docs/linux.md in the source.

This folder is a personal build: gGZLE01_recomp.so is code translated from
your disc and game/ holds your disc image. Never share or upload it.

Run ./boat-of-daphnes. Its menu (F1 or Esc) holds every setting, as in Ship of Harkinian: Settings,
Enhancements (Better Wind Waker's options, the Cosmetics Editor), Randomizer, Network (online play) and Dev
Tools. The picture fills the window at any shape, with the HUD at its edges.

Keyboard: arrows D-pad, J A, K B, U X, I Y, W/A/S/D control stick,
H/F/T/G C-stick, E/R L/R, Q Z, Return START. Game controllers work too.
F1 or Esc settings, F11 fullscreen, F9 frame rate.

Everything else is kept beside the program, as in Ship of Harkinian:
  Save/         the memory card (GZLE01.card), console settings, save states
  mods/         mods: folders or .zip/.rar/.7z (Settings > Mod Menu); texture packs in mods/textures/
  Randomizer/   randomizer seeds and spoiler logs (Randomizer > General > Generate Randomizer)
  logs/         session logs (settings, scenes, events, frame rate, online play), crash logs
  cache/        shader and pipeline caches
Rebuilding replaces the program files only; these folders are never touched. From a folder you cannot write to (an
AppImage), they are in ~/.local/share/boat-of-daphnes instead.
"""


def adjust_parser(parser):
    parser.add_argument("--install-module", action="store_true",
                        help="also copy gGZLE01_recomp.so to the player's data folder, where an AppImage finds it")
    parser.add_argument("--appimage", action="store_true",
                        help="also make OUT/boat-of-daphnes-x86_64.AppImage from the app folder")
    parser.add_argument("--with-module", action="store_true",
                        help="with --appimage: put your module in the image (a personal image: never share it)")
    # No profile-guided optimization on Linux yet: the instrumented module and its training tour are the Windows builder's.
    parser.set_defaults(no_train=True, no_pgo=True)


if __name__ == "__main__":
    wb.main(builder_class=LinuxBuilder, default_out=ROOT / "build/linux", adjust_parser=adjust_parser)
