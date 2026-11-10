#!/bin/bash
#
# build_catalina.sh
#
# Builds GoatTracker 2 as a self-contained GoatTracker2.app that
# runs on macOS 10.15 Catalina and later. Homebrew and MacPorts are NOT needed:
# the script downloads the official SDL2 framework and the sdl12-compat source,
# builds sdl12-compat, compiles GoatTracker and puts everything into the app.
#
# Requirements:
#   - Xcode Command Line Tools   (install with: xcode-select --install)
#   - Internet access on the first run (about 3 MB, cached in build-macos/)
#
# Usage:
#   ./build_catalina.sh
#
# Optional environment variables:
#   ARCH="x86_64"         CPU architecture(s). Default x86_64, the only one
#                         Catalina runs on. "x86_64 arm64" makes a universal app.
#   MACOS_MIN="10.15"     Oldest macOS version the app should run on.
#   JOBS=n                Parallel compile jobs (default: number of CPUs).
#
# Output:
#   GoatTracker2.app next to this script. The command line tools (gt2reloc,
#   ins2snd2, sngspli2, mod2sng, midi2sng) are inside GoatTracker2.app/Contents/MacOS.
#

set -euo pipefail

# Pinned dependencies (versions and SHA-256 checksums)
SDL2_VERSION="2.32.10"
SDL2_SHA256="4a7ac31640d70214e848f994be8a12849c0f97918a7e6c2e27a40036166d1a7f"
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.dmg"

SDL12COMPAT_VERSION="1.2.78"
SDL12COMPAT_SHA256="bdc5277cf3a8ae18c7493a7d4d0867c53439a9e5a661133dcf1f2eb0b9ddca4d"
SDL12COMPAT_URL="https://github.com/libsdl-org/sdl12-compat/releases/download/release-${SDL12COMPAT_VERSION}/sdl12-compat-${SDL12COMPAT_VERSION}.tar.gz"
SDL12_DYLIB_VERSION="12.78.0"   # dylib current_version, as sdl12-compat's CMake sets it

ARCH="${ARCH:-x86_64}"
MACOS_MIN="${MACOS_MIN:-10.15}"

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-macos"
DL="$BUILD/downloads"
DEPS="$BUILD/deps"
APP="$ROOT/GoatTracker2.app"
TOOLS="goattrk2 gt2reloc ins2snd2 sngspli2 mod2sng midi2sng"

die()  { printf '\nerror: %s\n' "$*" >&2; exit 1; }
step() { printf '\n==> %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------

[ "$(uname -s)" = "Darwin" ] || die "this script must be run on macOS."
[ -f "$ROOT/src/makefile.common" ] || die "run this script from the GoatTracker repository (src/makefile.common not found)."

if ! xcode-select -p >/dev/null 2>&1; then
    die "Xcode Command Line Tools are not installed.
       Run:  xcode-select --install
       (On Catalina you can also download 'Command Line Tools for Xcode 12.4'
        from https://developer.apple.com/download/all/ )"
fi

for tool in clang clang++ make curl shasum hdiutil ditto libtool lipo otool tar strip perl; do
    command -v "$tool" >/dev/null 2>&1 || die "required tool '$tool' not found."
done

# Windows git checkouts (core.autocrlf=true) give the makefiles CRLF line
# endings, which breaks make on macOS. Catch that early with a clear message.
if grep -q $'\r' "$ROOT/src/makefile" "$ROOT/src/makefile.common"; then
    die "the makefiles have Windows (CRLF) line endings.
       Clone the repository on the Mac, or fix this copy with:
         cd \"$ROOT\" && perl -pi -e 's/\\r\$//' src/makefile src/makefile.common"
fi

ARCH_FLAGS=()
for a in $ARCH; do
    case "$a" in
        x86_64|arm64) ARCH_FLAGS+=(-arch "$a") ;;
        *) die "unsupported ARCH '$a' (use x86_64 and/or arm64)." ;;
    esac
done
[ ${#ARCH_FLAGS[@]} -gt 0 ] || die "ARCH is empty."

CC_CMD=(clang "${ARCH_FLAGS[@]}" -mmacosx-version-min="$MACOS_MIN")
CXX_CMD=(clang++ "${ARCH_FLAGS[@]}" -mmacosx-version-min="$MACOS_MIN")
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 2)}"

echo "GoatTracker2 macOS build"
echo "  architecture(s): $ARCH"
echo "  minimum macOS:   $MACOS_MIN"
echo "  compiler:        $(clang --version 2>/dev/null | sed -n 1p)"

mkdir -p "$DL" "$DEPS"

# ---------------------------------------------------------------------------
# Downloads (cached and checksum-verified)
# ---------------------------------------------------------------------------

sha256_of() { shasum -a 256 "$1" | awk '{print $1}'; }

fetch() {
    local url="$1" sum="$2" dest="$3"
    if [ -f "$dest" ] && [ "$(sha256_of "$dest")" = "$sum" ]; then
        echo "Using cached $(basename "$dest")"
        return
    fi
    echo "Downloading $url"
    rm -f "$dest.part"
    curl -fsSL --retry 3 --connect-timeout 30 -o "$dest.part" "$url" \
        || die "download failed: $url
       You can download it manually with a browser and place it at:
         $dest"
    mv "$dest.part" "$dest"
    [ "$(sha256_of "$dest")" = "$sum" ] || { rm -f "$dest"; die "checksum mismatch for $(basename "$dest")."; }
}

step "Fetching dependencies"
SDL2_DMG="$DL/SDL2-$SDL2_VERSION.dmg"
SDL12_TGZ="$DL/sdl12-compat-$SDL12COMPAT_VERSION.tar.gz"
fetch "$SDL2_URL" "$SDL2_SHA256" "$SDL2_DMG"
fetch "$SDL12COMPAT_URL" "$SDL12COMPAT_SHA256" "$SDL12_TGZ"

# ---------------------------------------------------------------------------
# SDL2: official framework from the release disk image (supports 10.11+)
# ---------------------------------------------------------------------------

step "Extracting SDL2.framework $SDL2_VERSION"
MNT="$(mktemp -d "${TMPDIR:-/tmp}/gt2-sdl2.XXXXXX")"
cleanup() {
    hdiutil detach "$MNT" -quiet >/dev/null 2>&1 || true
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT
hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$MNT" "$SDL2_DMG" >/dev/null \
    || die "could not open $SDL2_DMG"
rm -rf "$DEPS/SDL2.framework"
ditto "$MNT/SDL2.framework" "$DEPS/SDL2.framework"
cleanup
trap - EXIT

# ---------------------------------------------------------------------------
# sdl12-compat: SDL 1.2 API on top of SDL2 (what Homebrew's sdl12-compat is)
# ---------------------------------------------------------------------------

step "Building sdl12-compat $SDL12COMPAT_VERSION"
SDL12_SRC="$BUILD/sdl12-compat-$SDL12COMPAT_VERSION"
SDL12_OBJ="$BUILD/obj-sdl12-compat"
rm -rf "$SDL12_SRC" "$SDL12_OBJ" "$DEPS/lib" "$DEPS/include"
tar -xzf "$SDL12_TGZ" -C "$BUILD"
mkdir -p "$SDL12_OBJ" "$DEPS/lib" "$DEPS/include"

SDL12_CFLAGS=(-O2 -fPIC -DNDEBUG -D_THREAD_SAFE -DSDL_DISABLE_IMMINTRIN_H -w)

# The library itself is compiled against the SDL2 headers. sdl12-compat
# includes "SDL.h" (needs -I) and the framework's headers include each other
# as <SDL2/...> (needs -F, the framework search path).
SDL2_INC=(-F"$DEPS" -I"$DEPS/SDL2.framework/Headers")
"${CC_CMD[@]}" "${SDL12_CFLAGS[@]}" "${SDL2_INC[@]}" \
    -c "$SDL12_SRC/src/SDL12_compat.c" -o "$SDL12_OBJ/SDL12_compat.o"
"${CC_CMD[@]}" "${SDL12_CFLAGS[@]}" "${SDL2_INC[@]}" \
    -c "$SDL12_SRC/src/SDL12_compat_objc.m" -o "$SDL12_OBJ/SDL12_compat_objc.o"

# ...and loads SDL2 at runtime from the app's Contents/Frameworks folder.
"${CC_CMD[@]}" -dynamiclib -o "$DEPS/lib/libSDL-1.2.0.dylib" \
    "$SDL12_OBJ/SDL12_compat.o" "$SDL12_OBJ/SDL12_compat_objc.o" \
    -Wl,-install_name,@rpath/libSDL-1.2.0.dylib \
    -Wl,-compatibility_version,12.0.0 \
    -Wl,-current_version,"$SDL12_DYLIB_VERSION" \
    -Wl,-undefined,error \
    -framework AppKit
ln -sf libSDL-1.2.0.dylib "$DEPS/lib/libSDL.dylib"

# SDLmain provides main() and the Cocoa application wrapper for SDL 1.2 apps.
"${CC_CMD[@]}" -O2 -D_THREAD_SAFE -w -I"$SDL12_SRC/include/SDL" \
    -c "$SDL12_SRC/src/SDLmain/macosx/SDLMain.m" -o "$SDL12_OBJ/SDLMain.o"
libtool -static -o "$DEPS/lib/libSDLmain.a" "$SDL12_OBJ/SDLMain.o"

cp -R "$SDL12_SRC/include/SDL" "$DEPS/include/"

# ---------------------------------------------------------------------------
# GoatTracker
# ---------------------------------------------------------------------------

step "Building GoatTracker2"

# src/makefile asks sdl-config for compiler/linker flags. Provide one that
# points at the libraries built above. Paths are relative to src/ (where make
# runs) so that a repository path containing spaces does not break the build.
mkdir -p "$BUILD/bin"
cat > "$BUILD/bin/sdl-config" <<'EOF'
#!/bin/sh
# sdl-config for the build_catalina.sh build (paths relative to src/)
for arg in "$@"; do
    case "$arg" in
        --cflags)  echo "-I../build-macos/deps/include/SDL -D_THREAD_SAFE" ;;
        --libs)    echo "-L../build-macos/deps/lib -lSDLmain -lSDL -Wl,-framework,Cocoa -Wl,-rpath,@executable_path/../Frameworks" ;;
        --version) echo "1.2.15" ;;
        --prefix|--exec-prefix) echo "../build-macos/deps" ;;
    esac
done
EOF
chmod +x "$BUILD/bin/sdl-config"
export PATH="$BUILD/bin:$PATH"

# reSID is C++98 code (it uses 'register', which C++17 rejects). Pin the
# dialect so newer Xcode versions compile it the same way Catalina's does.
export CXXFLAGS="-std=gnu++98"

# Remove leftovers. The sf-v1.1 release zip ships Apple Silicon .o files and
# binaries that would otherwise be linked into (and break) an Intel build.
make -C "$ROOT/src" clean >/dev/null

# "-o goattrk2.dat -o goatdata.c": use the pre-generated data files from the
# repository instead of regenerating them (that needs Windows-only helper .exe
# files). Without this, file timestamps after a clone/unzip can trigger it.
make -C "$ROOT/src" -j"$JOBS" \
    -o goattrk2.dat -o goatdata.c \
    CC="${CC_CMD[*]}" CXX="${CXX_CMD[*]}"

for t in $TOOLS; do
    [ -x "$ROOT/linux/$t" ] || die "build did not produce linux/$t"
done

# ---------------------------------------------------------------------------
# App bundle
# ---------------------------------------------------------------------------

step "Assembling GoatTracker2.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

for t in $TOOLS; do
    cp "$ROOT/linux/$t" "$APP/Contents/MacOS/"
done
cp "$DEPS/lib/libSDL-1.2.0.dylib" "$APP/Contents/Frameworks/"
ditto "$DEPS/SDL2.framework" "$APP/Contents/Frameworks/SDL2.framework"
cp "$ROOT/macos/GoatTracker2.icns" "$APP/Contents/Resources/GoatTracker2.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>goattrk2</string>
    <key>CFBundleIconFile</key>
    <string>GoatTracker2</string>
    <key>CFBundleIdentifier</key>
    <string>net.sourceforge.goattracker2.silverfork</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>GoatTracker2</string>
    <key>CFBundleDisplayName</key>
    <string>GoatTracker 2</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>2.82</string>
    <key>CFBundleVersion</key>
    <string>2.82</string>
    <key>LSMinimumSystemVersion</key>
    <string>$MACOS_MIN</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
EOF
printf 'APPL????' > "$APP/Contents/PkgInfo"

# Ad-hoc signature: not required on Intel Macs, but keeps the bundle
# consistent (and is required for any arm64 code).
if ! codesign --force --deep --sign - "$APP" >/dev/null 2>&1; then
    echo "warning: ad-hoc code signing failed (harmless on Intel Macs)."
fi

# ---------------------------------------------------------------------------
# Report and self-test
# ---------------------------------------------------------------------------

step "Checking the result"
lipo -info "$APP/Contents/MacOS/goattrk2"
otool -l "$APP/Contents/MacOS/goattrk2" \
    | awk '/LC_VERSION_MIN_MACOSX|LC_BUILD_VERSION/{f=1} f&&/(version|minos) /{print "  minimum macOS: " $2; f=0}'
otool -L "$APP/Contents/MacOS/goattrk2" | grep -i sdl || true

can_run=0
for a in $ARCH; do
    if [ "$a" = "$(uname -m)" ]; then
        can_run=1
    elif [ "$a" = "x86_64" ] && arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
        can_run=1   # Intel binary on Apple Silicon with Rosetta
    fi
done

if [ "$can_run" = 1 ]; then
    # gt2reloc links the same SDL libraries as the tracker, so converting an
    # example song proves the bundled sdl12-compat + SDL2 load correctly.
    # perl's alarm() is a timeout: a missing SDL2 would show a modal dialog.
    # Short relative file names, since gt2reloc uses fixed-size path buffers.
    cp "$ROOT/examples/everlasting.sng" "$BUILD/selftest.sng"
    rm -f "$BUILD/selftest.prg"
    if (cd "$BUILD" && perl -e 'alarm shift; exec @ARGV or die "exec: $!\n"' 60 \
            "$APP/Contents/MacOS/gt2reloc" selftest.sng selftest.prg >selftest.log 2>&1) \
            && [ -s "$BUILD/selftest.prg" ]; then
        echo "Self-test passed (gt2reloc converted examples/everlasting.sng)."
    else
        cat "$BUILD/selftest.log" >&2 || true
        die "self-test failed: the bundled SDL libraries did not load or gt2reloc failed."
    fi
else
    echo "Skipping self-test: this Mac cannot run $ARCH binaries."
fi

cat <<EOF

Done: $APP

  Start it:     open "$APP"   (or double-click it in Finder)
  Install it:   drag GoatTracker2.app to /Applications
  CLI tools:    "$APP/Contents/MacOS/gt2reloc" song.sng song.sid
  Settings are stored in ~/.goattrk/goattrk2.cfg

CMD+1 to CMD+9 work as F1-F9 (see the Control menu). For F10-F12 hold the
fn key, or enable "Use F1, F2, etc. keys as
standard function keys" in System Preferences > Keyboard.
EOF
