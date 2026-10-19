#!/usr/bin/env bash
# Builds the Linux x86_64 Release and packages dist/HardCorpsUprisingRecompiled-v<version>-x86_64.AppImage.
# Local only: nothing is published, tagged or uploaded. Run on Linux or WSL.
#
#   tools/package-linux.sh            incremental Release build, then package
#   tools/package-linux.sh --clean    clean Release build first
#   tools/package-linux.sh --out-dir DIR    write the AppImage and its .sha256 to DIR instead of dist (test builds)
#
# Needs clang-20, clang++-20, lld-20, cmake 3.25+, ninja, rsync, git, wget or curl, unzip or python3, binutils and the
# GCC 13+ C++ runtime. Release builds are made on Ubuntu 20.04 (tools/setup-linux-build-env.sh prepares one): the AppImage
# needs the build machine's glibc.
# The ReXGlue runtime and Vulkan GPU plugin are built from source with the project's patches
# (tools/build-patched-rexruntime.sh) the first time and whenever the patches change, which also needs network
# access and the packages in docs/rexglue-runtime-patch.md.
# The AppImage runtime (the part that mounts the AppImage before AppRun runs) is built from the pinned AppImage/type2-runtime
# source with the project's patch (tools/build-appimage-runtime.sh): that needs root or sudo, network access the first time,
# and is skipped while the pins and patches are unchanged.
# Needs generated/default (tools/codegen.ps1) and game/extracted/default.xex: the build re-runs codegen
# when its inputs look changed, and it never packages either of them.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# On the Linux filesystem: much faster than a Windows drive under /mnt, and case-sensitive.
WORK="${HCU_WORK:-$HOME/hcu-linux-build}"
PRESET=linux-amd64-release
MAX_GLIBC=2.31

SDK_VERSION=0.10.0
SDK_SHA256=0388edb3f8fb1444f1b0ad5d440cb02d9690ac20a76b04fd6b7dcc19f59328a6
XEX_SHA256=530ede8ed5271051a2ab19c077a97fb167e4f30b094c292e8f59d37d0d2069dc
APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

CLEAN=0
OUT_DIR="$ROOT/dist"
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) CLEAN=1 ;;
    --out-dir) [ $# -ge 2 ] || { echo "--out-dir needs a folder" >&2; exit 2; }; OUT_DIR="$(realpath -m "$2")"; shift ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

fail() { echo "ERROR: $*" >&2; exit 1; }

download() {  # url file sha256
  if [ ! -f "$2" ] || ! echo "$3  $2" | sha256sum -c --status -; then
    rm -f "$2"
    if command -v wget >/dev/null; then wget -q -O "$2" "$1"; else curl -fsSL -o "$2" "$1"; fi
  fi
  echo "$3  $2" | sha256sum -c --status - || fail "$2 does not match its pinned SHA-256"
}

VERSION="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*hard_corps_uprising[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "project VERSION not found in CMakeLists.txt"
APP_ID="$(sed -nE 's/^[[:space:]]*constexpr char kApplicationId\[\] = "([^"]+)";.*/\1/p' "$ROOT/src/platform.cpp")"
[[ "$APP_ID" =~ ^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$ ]] ||
  fail "src/platform.cpp must declare kApplicationId exactly once, as a dotted name on one line (found: '$APP_ID')"
[ -f "$ROOT/packaging/linux/$APP_ID.desktop" ] || fail "packaging/linux/$APP_ID.desktop is missing: the desktop file is named after kApplicationId"
NAME="HardCorpsUprisingRecompiled-v$VERSION-x86_64"
echo "Hard Corps: Uprising Recompiled $VERSION - Linux AppImage (local only)"
SOURCE="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if CHANGES="$(git -C "$ROOT" status --porcelain 2>/dev/null)"; then
  [ -z "$CHANGES" ] || SOURCE="$SOURCE plus uncommitted changes"
else
  SOURCE="$SOURCE, working tree state unknown"
fi
echo "Source: $SOURCE"

echo "== Tools"
for tool in clang-20 clang++-20 ld.lld-20 cmake ninja rsync sha256sum strip readelf objdump ldd; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done
command -v wget >/dev/null || command -v curl >/dev/null || fail "wget or curl not found"
command -v unzip >/dev/null || command -v python3 >/dev/null || fail "unzip or python3 not found"
clang++-20 --version | sed -n 1p
cmake --version | sed -n 1p
HOST_GLIBC="$(getconf GNU_LIBC_VERSION | cut -d' ' -f2)"
echo "glibc $HOST_GLIBC"
[ "$(printf '%s\n' "$MAX_GLIBC" "$HOST_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] ||
  echo "WARNING: this AppImage will need glibc $HOST_GLIBC; release builds use Ubuntu 20.04 (glibc $MAX_GLIBC)" >&2

echo "== Inputs"
[ -f "$ROOT/generated/default/sources.cmake" ] || fail "generated/default is missing (run tools/codegen.ps1 first)"
[ -f "$ROOT/third_party/trg-launcher/CMakeLists.txt" ] || fail "run: git submodule update --init third_party/trg-launcher"
XEX="$ROOT/game/extracted/default.xex"
[ -f "$XEX" ] || fail "missing $XEX (extract your own XBLA package into game/extracted first)"
echo "$XEX_SHA256  $XEX" | sha256sum -c --status - || fail "unsupported $XEX"

echo "== Source -> $WORK"
mkdir -p "$WORK/game/extracted" "$WORK/third_party" "$WORK/tools"
for f in CMakeLists.txt CMakePresets.json hard_corps_uprising_manifest.toml LICENSE tools/build-patched-rexruntime.sh; do
  rsync -a "$ROOT/$f" "$WORK/$f"
done
for d in assets config src tests generated packaging patches third_party/trg-launcher; do
  mkdir -p "$WORK/$d"
  rsync -a --delete --exclude .git "$ROOT/$d/" "$WORK/$d/"
done
rsync -a "$XEX" "$WORK/game/extracted/default.xex"

echo "== ReXGlue SDK $SDK_VERSION (linux-amd64)"
SDK="$WORK/tools/rexglue/linux-amd64"
if [ ! -x "$SDK/bin/rexglue" ]; then
  mkdir -p "$WORK/tools/rexglue"
  ZIP="$WORK/tools/rexglue/rexglue-sdk-$SDK_VERSION-linux-amd64.zip"
  download "https://github.com/rexglue/rexglue-sdk/releases/download/v$SDK_VERSION/rexglue-sdk-$SDK_VERSION-linux-amd64.zip" \
    "$ZIP" "$SDK_SHA256"
  rm -rf "$SDK"
  if command -v unzip >/dev/null; then
    unzip -q -o "$ZIP" -d "$WORK/tools/rexglue"
  else
    python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$WORK/tools/rexglue"
    chmod +x "$SDK/bin/rexglue"
  fi
fi
# The stock rexglue tool needs a newer glibc than the build baseline, so the version is read from the CMake package.
grep -qF "set(PACKAGE_VERSION \"$SDK_VERSION\")" "$SDK/lib/cmake/rexglue/rexglueConfigVersion.cmake" ||
  fail "tools/rexglue/linux-amd64 is not ReXGlue $SDK_VERSION"

echo "== Patched ReXGlue runtime and GPU plugin"
PATCHED="$ROOT/tools/build-patched-rexruntime.sh"
if ! bash "$PATCHED" --check "$SDK" >/dev/null 2>&1; then
  bash "$PATCHED" "$SDK" "$WORK/rexglue-sdk-src"
fi
bash "$PATCHED" --check "$SDK"

if ! "$SDK/bin/rexglue" --version >/dev/null 2>&1; then
  echo "== rexglue code generator (the stock one needs a newer glibc than this machine has)"
  GEN_SRC="$WORK/rexglue-sdk-src"
  GEN_BUILD="$GEN_SRC/out/build/linux-amd64"
  cmake -S "$GEN_SRC" -B "$GEN_BUILD" "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,'\$ORIGIN/../lib'" >/dev/null
  cmake --build "$GEN_BUILD" --config Release --target rexglue --parallel
  cp "$GEN_SRC/out/linux-amd64/Release/rexglue" "$SDK/bin/rexglue"
  [ "$("$SDK/bin/rexglue" --version | tr -d '[:space:]')" = "$SDK_VERSION" ] || fail "the built rexglue is not ReXGlue $SDK_VERSION"
fi

echo "== Build ($PRESET)"
BIN="$WORK/out/build/$PRESET"
[ "$CLEAN" = 1 ] && rm -rf "$BIN"
(cd "$WORK" && cmake --preset "$PRESET" >/dev/null && cmake --build --preset "$PRESET")
for f in hard_corps_uprising librexruntime.so librexgpu-xenos.so hard_corps_uprising.toml assets/launcher/header-art.png; do
  [ -f "$BIN/$f" ] || fail "build output is missing $f"
done
bash "$PATCHED" --check "$SDK" --staged "$BIN" || fail "the build folder does not hold the patched ReXGlue libraries"

echo "== AppDir"
APPDIR="$WORK/AppDir"
DOC="$APPDIR/usr/share/doc/hard_corps_uprising"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$DOC"
cp "$BIN/hard_corps_uprising" "$BIN/librexruntime.so" "$BIN/librexgpu-xenos.so" "$BIN/hard_corps_uprising.toml" \
  "$APPDIR/usr/bin/"
mkdir -p "$APPDIR/usr/bin/assets/launcher"
cp "$BIN/assets/launcher/header-art.png" "$APPDIR/usr/bin/assets/launcher/"
# Debug sections carry the build machine's folders.
strip --strip-unneeded "$APPDIR/usr/bin/hard_corps_uprising"
# ReXGlue needs GLIBCXX_3.4.32 (GCC 13); AppRun uses this copy only on systems without it.
for lib in libstdc++.so.6 libgcc_s.so.1; do
  src="$(g++ -print-file-name=$lib 2>/dev/null || true)"
  [ -f "$src" ] || src="/usr/lib/x86_64-linux-gnu/$lib"
  cp -L "$src" "$APPDIR/usr/lib/$lib"
done
grep -q GLIBCXX_3.4.32 "$APPDIR/usr/lib/libstdc++.so.6" || fail "the build machine's libstdc++ is older than GCC 13"
cp "$WORK/LICENSE" "$WORK/packaging/licenses/ReXGlue-LICENSE.txt" "$WORK/packaging/licenses/THIRD-PARTY-LICENSES.txt" \
  "$WORK/packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt" "$DOC/"
for component in type2-runtime libfuse squashfuse Zstandard zlib mimalloc musl libgcc; do
  grep -qF "AppImage runtime: $component" "$DOC/THIRD-PARTY-LICENSES-LINUX.txt" ||
    fail "THIRD-PARTY-LICENSES-LINUX.txt has no notice for the AppImage runtime component $component"
done
CXX_COPYRIGHT=/usr/share/doc/libstdc++6/copyright
[ -f "$CXX_COPYRIGHT" ] || fail "missing $CXX_COPYRIGHT (the license of the bundled C++ runtime)"
cp "$CXX_COPYRIGHT" "$DOC/libstdc++-copyright.txt"
cp "$WORK/packaging/linux/AppRun" "$WORK/packaging/linux/$APP_ID.desktop" "$APPDIR/"
# The icon is the root file the .desktop names, the .DirIcon file managers show, and the theme the desktop integrates.
ICONS="$WORK/packaging/icons"
THEME="$APPDIR/usr/share/icons/hicolor"
for size in 16 24 32 48 64 128 256 512; do
  mkdir -p "$THEME/${size}x${size}/apps"
  cp "$ICONS/app-icon-$size.png" "$THEME/${size}x${size}/apps/hard_corps_uprising.png"
done
mkdir -p "$THEME/scalable/apps"
cp "$ICONS/app-icon.svg" "$THEME/scalable/apps/hard_corps_uprising.svg"
cp "$ICONS/app-icon-256.png" "$APPDIR/hard_corps_uprising.png"
cp "$ICONS/app-icon-256.png" "$APPDIR/.DirIcon"
chmod 755 "$APPDIR/AppRun" "$APPDIR/usr/bin/hard_corps_uprising"
chmod 644 "$APPDIR/"*.desktop "$APPDIR/"*.png "$APPDIR/.DirIcon" "$APPDIR/usr/bin/"*.so \
  "$APPDIR/usr/bin/hard_corps_uprising.toml" "$APPDIR/usr/bin/assets/launcher/header-art.png" \
  "$APPDIR/usr/lib/"* "$DOC/"*
find "$THEME" -type f -exec chmod 644 {} +
bash "$PATCHED" --check "$SDK" --staged "$APPDIR/usr/bin" || fail "the AppDir does not hold the patched ReXGlue libraries"

echo "== Release check"
problems=0
problem() { echo "  $*" >&2; problems=$((problems + 1)); }
while IFS= read -r -d '' item; do
  rel="${item#"$APPDIR"/}"
  base="$(basename "$item")"
  case "$base" in
    default.xex | DataXENON | game | game-install-temp | game-install-backup | user_data | user_data.migrating | content | cache | logs | \
      settings.toml | settings.toml.bak | ArcadeInfo.xml | nxeart | 58410A18) problem "forbidden name: $rel" ;;
    *.xex | *.log | *.dmp | *.pdb | *.xsh | *.xpso | *.bak | *.tmp | *.sav) problem "forbidden file type: $rel" ;;
  esac
  case "${base,,}" in
    *renderdoc* | *vklayer* | *.json | implicit_layer.d | explicit_layer.d) problem "RenderDoc or Vulkan layer file: $rel" ;;
  esac
  [ -L "$item" ] && problem "symbolic link: $rel"
  if [ -f "$item" ]; then
    case "$(head -c 4 "$item" | tr -d '\0')" in LIVE | PIRS | "CON ") problem "Xbox 360 package: $rel" ;; esac
    for path in "$ROOT" "$WORK" "$HOME"; do
      grep -qaF "$path" "$item" && problem "local path in file: $rel ($path)"
    done
    [ "$(head -c 2 "$item")" = MZ ] && problem "Windows binary: $rel"
  fi
done < <(find "$APPDIR" -mindepth 1 -print0)
EXPECTED=".DirIcon
AppRun
hard_corps_uprising.png
$APP_ID.desktop
usr/bin/assets/launcher/header-art.png
usr/bin/hard_corps_uprising
usr/bin/hard_corps_uprising.toml
usr/bin/librexgpu-xenos.so
usr/bin/librexruntime.so
usr/lib/libgcc_s.so.1
usr/lib/libstdc++.so.6
usr/share/doc/hard_corps_uprising/LICENSE
usr/share/doc/hard_corps_uprising/ReXGlue-LICENSE.txt
usr/share/doc/hard_corps_uprising/THIRD-PARTY-LICENSES-LINUX.txt
usr/share/doc/hard_corps_uprising/THIRD-PARTY-LICENSES.txt
usr/share/doc/hard_corps_uprising/libstdc++-copyright.txt
usr/share/icons/hicolor/128x128/apps/hard_corps_uprising.png
usr/share/icons/hicolor/16x16/apps/hard_corps_uprising.png
usr/share/icons/hicolor/24x24/apps/hard_corps_uprising.png
usr/share/icons/hicolor/256x256/apps/hard_corps_uprising.png
usr/share/icons/hicolor/32x32/apps/hard_corps_uprising.png
usr/share/icons/hicolor/48x48/apps/hard_corps_uprising.png
usr/share/icons/hicolor/512x512/apps/hard_corps_uprising.png
usr/share/icons/hicolor/64x64/apps/hard_corps_uprising.png
usr/share/icons/hicolor/scalable/apps/hard_corps_uprising.svg"
ACTUAL="$(cd "$APPDIR" && find . ! -type d | sed 's|^\./||' | LC_ALL=C sort)"
[ "$ACTUAL" = "$EXPECTED" ] || { problem "unexpected AppDir contents:"; diff <(echo "$EXPECTED") <(echo "$ACTUAL") >&2 || true; }
grep -qxF "StartupWMClass=$APP_ID" "$APPDIR/$APP_ID.desktop" || problem "StartupWMClass in $APP_ID.desktop is not $APP_ID"
grep -qaF "$APP_ID" "$APPDIR/usr/bin/hard_corps_uprising" || problem "the program does not contain the application id $APP_ID"
for f in hard_corps_uprising librexruntime.so librexgpu-xenos.so; do
  if readelf -d "$APPDIR/usr/bin/$f" | grep -i renderdoc >/dev/null; then problem "$f links RenderDoc"; fi
done
readelf -d "$APPDIR/usr/bin/hard_corps_uprising" | grep -q 'R.*PATH.*\[\$ORIGIN\]' || problem "the program's RPATH is not \$ORIGIN"
missing="$(ldd "$APPDIR/usr/bin/hard_corps_uprising" | grep 'not found' || true)"
[ -z "$missing" ] || problem "unresolved libraries: $missing"
[ "$problems" = 0 ] || fail "the AppDir holds files that must not be released"
echo "  clean"
for f in usr/bin/hard_corps_uprising usr/bin/librexruntime.so usr/bin/librexgpu-xenos.so usr/lib/libstdc++.so.6 usr/lib/libgcc_s.so.1; do
  glibc="$(readelf --dyn-syms -W "$APPDIR/$f" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)"
  echo "  $f needs $glibc"
  [ "$(printf '%s\n' "${glibc#GLIBC_}" "$MAX_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] ||
    fail "$f needs $glibc, above the glibc $MAX_GLIBC baseline"
done

echo "== AppImage"
TOOLS="$WORK/tools/appimage"
mkdir -p "$TOOLS"
TOOL="$TOOLS/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage"
RUNTIME="$TOOLS/runtime-hcu-x86_64"
download "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
  "$TOOL" "$APPIMAGETOOL_SHA256"
bash "$ROOT/tools/build-appimage-runtime.sh" "$RUNTIME" "$WORK/appimage-runtime"
chmod +x "$TOOL"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$NAME.AppImage"
rm -f "$OUT"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run --no-appstream --runtime-file "$RUNTIME" "$APPDIR" "$OUT" >/dev/null
chmod 755 "$OUT"
bash "$ROOT/tools/build-appimage-runtime.sh" --check "$OUT"
(cd "$OUT_DIR" && sha256sum "$NAME.AppImage" > "$NAME.sha256")

echo
echo "===================================================="
echo " APPIMAGE READY (nothing was published)"
echo "===================================================="
echo "  AppImage: $OUT ($(du -h "$OUT" | cut -f1))"
echo "  SHA-256:  $(cut -d' ' -f1 "$OUT_DIR/$NAME.sha256")"
echo "  Source:   $SOURCE"
echo "  Contents:"
echo "$EXPECTED" | sed 's/^/    /'
