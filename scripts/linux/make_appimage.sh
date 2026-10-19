#!/usr/bin/env bash
# Package a built Linux app folder as an AppImage.
#
#   scripts/linux/make_appimage.sh [BUILD_DIR] [OUT.AppImage] [--with-module]
#
# BUILD_DIR is scripts/linux/build.py's --out (default build/linux); the app folder BUILD_DIR/boat-of-daphnes is
# expected to exist. OUT defaults to BUILD_DIR/boat-of-daphnes-x86_64.AppImage.
#
# The AppImage carries the program, the DSP ROMs, the randomizer's runtime (wwrando, gclib, a python3.12 venv) and
# every shared library the program links except the glibc baseline. The player's own disc and the files made from it
# (main.dol, the RELs, Swift Sail's and Link's textures) are NEVER in it: on first launch the program asks for the disc
# and prepares what it needs into the player's data folder. The game module is code translated from the disc, so it is
# left out too, unless --with-module is given (a personal image, or the CI fixture's empty stand-in): the player's
# module is then expected in the data folder (build.py --install-module).
#
# Needs appimagetool and desktop-file-validate on PATH (APPIMAGETOOL names another appimagetool); zsyncmake makes the
# update file when it is there. APPIMAGE_RUNTIME_FILE names a pinned Type 2 runtime to embed instead of downloading one.
set -euo pipefail

with_module=0
positional=()
for arg in "$@"; do
    case "$arg" in
        --with-module) with_module=1 ;;
        *) positional+=("$arg") ;;
    esac
done

appimagetool=${APPIMAGETOOL:-appimagetool}
for tool in "$appimagetool" desktop-file-validate; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "make_appimage: $tool not found on PATH" >&2
        exit 1
    fi
done

root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "${positional[0]:-$root/build/linux}" && pwd)
app="$build_dir/boat-of-daphnes"
out="${positional[1]:-$build_dir/boat-of-daphnes-x86_64.AppImage}"
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")

if [ ! -x "$app/boat-of-daphnes" ]; then
    echo "make_appimage: $app/boat-of-daphnes not found; run scripts/linux/build.py first" >&2
    exit 1
fi
if [ "$with_module" = 1 ] && [ ! -f "$app/gGZLE01_recomp.so" ]; then
    echo "make_appimage: --with-module, but $app/gGZLE01_recomp.so is missing" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
appdir="$work/boat-of-daphnes.AppDir"
bin="$appdir/usr/bin"
mkdir -p "$bin/dsp" "$appdir/usr/lib" "$appdir/usr/share/icons/hicolor/256x256/apps" \
         "$appdir/usr/share/applications" "$appdir/usr/share/licenses/boat-of-daphnes"

cp "$app/boat-of-daphnes" "$bin/boat-of-daphnes"
if [ -f "$app/initial_pipeline_cache.db" ]; then
    cp "$app/initial_pipeline_cache.db" "$bin/"
fi
cp "$app/dsp/dsp_rom.bin" "$app/dsp/dsp_coef.bin" "$bin/dsp/"
if [ "$with_module" = 1 ]; then
    cp "$app/gGZLE01_recomp.so" "$bin/gGZLE01_recomp.so"
fi
# Only what is not made from a disc: the randomizer's menu data, and the generator with its first-launch tools. Not
# game/, assets/swift_sail, assets/link_colors or assets/bunnyhood, nor the builder's provenance record.
if [ -d "$app/assets/randomizer" ]; then
    mkdir -p "$bin/assets"
    cp -a "$app/assets/randomizer" "$bin/assets/randomizer"
fi
if [ -d "$app/rando" ]; then
    cp -a "$app/rando" "$bin/rando"
    find "$bin/rando" -name __pycache__ -type d -prune -exec rm -rf {} +
fi
for license in LICENSE RIGHTS_AND_LICENSES.md; do
    if [ -f "$root/$license" ]; then
        cp "$root/$license" "$appdir/usr/share/licenses/boat-of-daphnes/$license"
    fi
done

# Shared libraries the program links, minus the glibc baseline (bundling that breaks the image on distributions whose
# glibc differs from the build host's). The C++ runtime is linked into the program, not carried.
while IFS= read -r lib; do
    base=$(basename "$lib")
    case "$base" in
        libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|ld-linux*|libgcc_s.so*|libstdc++.so*|linux-vdso*)
            continue ;;
    esac
    cp -L "$lib" "$appdir/usr/lib/"
done < <(ldd "$app/boat-of-daphnes" | awk '/=> \// {print $3}' | sort -u)

# AppRun: the library path, then the program. The program finds its files beside itself; its player files (saves,
# settings, logs) go to ~/.local/share/boat-of-daphnes because the image's mount is read-only.
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/boat-of-daphnes" "$@"
EOF
chmod +x "$appdir/AppRun"

cat > "$appdir/boat-of-daphnes.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Boat of Daphnes
Comment=The Legend of Zelda: The Wind Waker, statically recompiled from your own disc
Exec=boat-of-daphnes
Icon=boat-of-daphnes
Categories=Game;
Terminal=false
EOF
cp "$appdir/boat-of-daphnes.desktop" "$appdir/usr/share/applications/boat-of-daphnes.desktop"

icon="$appdir/usr/share/icons/hicolor/256x256/apps/boat-of-daphnes.png"
python3 "$root/scripts/linux/ico_to_png.py" "$root/windows/resources/BlueWake.ico" "$icon"
cp "$icon" "$appdir/boat-of-daphnes.png"
ln -sf usr/bin/boat-of-daphnes "$appdir/boat-of-daphnes"
ln -sf usr/share/icons/hicolor/256x256/apps/boat-of-daphnes.png "$appdir/.DirIcon"

desktop-file-validate "$appdir/boat-of-daphnes.desktop"
desktop-file-validate "$appdir/usr/share/applications/boat-of-daphnes.desktop"

echo "make_appimage: squashing $out"
runtime_args=()
if [ -n "${APPIMAGE_RUNTIME_FILE:-}" ]; then
    runtime_args=(--runtime-file "$APPIMAGE_RUNTIME_FILE")
fi
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$appimagetool" "${runtime_args[@]}" "$appdir" "$out"

if command -v zsyncmake >/dev/null 2>&1; then
    zsyncmake -u "$(basename "$out")" -o "$out.zsync" "$out"
    echo "make_appimage: $out.zsync"
fi
echo "make_appimage: $out"
echo "make_appimage: run it with APPIMAGE_EXTRACT_AND_RUN=1 on a machine without FUSE"
