#!/bin/bash
#
# build_catalina.sh
#
# Builds GoatTracker 2 as a self-contained GoatTracker2.app that
# runs on macOS 10.15 Catalina and later. Homebrew and MacPorts are NOT needed:
# the script downloads the official SDL2 framework, compiles GoatTracker
# (with its Dear ImGui user interface) and puts everything into the app.
#
# Requirements:
#   - Xcode Command Line Tools   (install with: xcode-select --install)
#   - Internet access on the first run (about 2 MB, cached in build-macos/)
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
fetch "$SDL2_URL" "$SDL2_SHA256" "$SDL2_DMG"

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
# GoatTracker
# ---------------------------------------------------------------------------

step "Building GoatTracker2"

# src/makefile asks sdl2-config for compiler/linker flags. Provide one that
# points at the SDL2 framework extracted above. Paths are relative to src/
# (where make runs) so that a repository path containing spaces does not
# break the build. The framework headers include each other as <SDL2/...>
# (needs -F) and GoatTracker includes "SDL.h" (needs -I).
mkdir -p "$BUILD/bin"
rm -f "$BUILD/bin/sdl-config"
cat > "$BUILD/bin/sdl2-config" <<'EOF'
#!/bin/sh
# sdl2-config for the build_catalina.sh build (paths relative to src/)
for arg in "$@"; do
    case "$arg" in
        --cflags)  echo "-F../build-macos/deps -I../build-macos/deps/SDL2.framework/Headers -D_THREAD_SAFE" ;;
        --libs)    echo "-F../build-macos/deps -framework SDL2 -Wl,-rpath,@executable_path/../Frameworks" ;;
        --version) echo "2.32.10" ;;
        --prefix|--exec-prefix) echo "../build-macos/deps" ;;
    esac
done
EOF
chmod +x "$BUILD/bin/sdl2-config"
export PATH="$BUILD/bin:$PATH"

# reSID is C++98 code (it uses 'register', which C++17 rejects). Pin the
# dialect so newer Xcode versions compile it the same way Catalina's does.
# (The makefile compiles the Dear ImGui interface files as C++11.)
export CXXFLAGS="-std=gnu++98"

# Remove leftovers. The sf-v1.1 release zip ships Apple Silicon .o files and
# binaries that would otherwise be linked into (and break) an Intel build.
make -C "$ROOT/src" clean >/dev/null

# "-o goattrk2.dat -o goatdata.c": use the pre-generated data files from the
# repository instead of regenerating them (that needs Windows-only helper .exe
# files). Without this, file timestamps after a clone/unzip can trigger it.
make -C "$ROOT/src" -j"$JOBS" \
    -o goattrk2.dat -o goatdata.c \
    SDLCONFIG=sdl2-config \
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
ditto "$DEPS/SDL2.framework" "$APP/Contents/Frameworks/SDL2.framework"
cp "$ROOT/macos/GoatTracker2.icns" "$APP/Contents/Resources/GoatTracker2.icns"
# Interface fonts (Inter, JetBrains Mono; SIL Open Font License)
mkdir -p "$APP/Contents/Resources/fonts"
cp "$ROOT"/fonts/*.ttf "$ROOT"/fonts/*-OFL.txt "$APP/Contents/Resources/fonts/"

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
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>GoatTracker Song</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>LSHandlerRank</key>
            <string>Owner</string>
            <key>CFBundleTypeExtensions</key>
            <array><string>sng</string></array>
            <key>LSItemContentTypes</key>
            <array><string>net.sourceforge.goattracker2.song</string></array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key>
            <string>GoatTracker Instrument</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>LSHandlerRank</key>
            <string>Owner</string>
            <key>CFBundleTypeExtensions</key>
            <array><string>ins</string></array>
            <key>LSItemContentTypes</key>
            <array><string>net.sourceforge.goattracker2.instrument</string></array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key>
            <string>SID Music File</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
            <key>CFBundleTypeExtensions</key>
            <array><string>sid</string></array>
            <key>LSItemContentTypes</key>
            <array><string>org.c64.sid</string></array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key>
            <string>chipsynth C64 Preset</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
            <key>CFBundleTypeExtensions</key>
            <array><string>fermatax</string></array>
            <key>LSItemContentTypes</key>
            <array><string>com.plogue.fermatax</string></array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key>
            <string>MIDI File</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
            <key>LSItemContentTypes</key>
            <array><string>public.midi-audio</string></array>
        </dict>
    </array>
    <key>UTExportedTypeDeclarations</key>
    <array>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>net.sourceforge.goattracker2.song</string>
            <key>UTTypeDescription</key>
            <string>GoatTracker Song</string>
            <key>UTTypeConformsTo</key>
            <array><string>public.data</string></array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array><string>sng</string></array>
            </dict>
        </dict>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>net.sourceforge.goattracker2.instrument</string>
            <key>UTTypeDescription</key>
            <string>GoatTracker Instrument</string>
            <key>UTTypeConformsTo</key>
            <array><string>public.data</string></array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array><string>ins</string></array>
            </dict>
        </dict>
    </array>
    <key>UTImportedTypeDeclarations</key>
    <array>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>org.c64.sid</string>
            <key>UTTypeDescription</key>
            <string>SID Music File</string>
            <key>UTTypeConformsTo</key>
            <array><string>public.data</string><string>public.audio</string></array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array><string>sid</string></array>
            </dict>
        </dict>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>com.plogue.fermatax</string>
            <key>UTTypeDescription</key>
            <string>chipsynth Preset</string>
            <key>UTTypeConformsTo</key>
            <array><string>public.data</string></array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array><string>fermatax</string></array>
            </dict>
        </dict>
    </array>
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
    # gt2reloc links the same SDL2 framework as the tracker, so converting an
    # example song proves the bundled SDL2 loads correctly.
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
  Settings are stored in ~/.goattrk/ (goattrk2.cfg, the window layout in
  imgui.ini and the recent files)

Songs, SID, MIDI and chipsynth files open by double-clicking them in the
Finder (choose Open With > GoatTracker2 the first time) or by dropping them
on the window. CMD+1 to CMD+9 work as F1-F9 (see the Control menu). For
F10-F12 hold the fn key, or enable "Use F1, F2, etc. keys as standard
function keys" in System Preferences > Keyboard.
EOF
