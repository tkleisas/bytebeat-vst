#!/usr/bin/env bash
# Build the Bytebeat Machine and Bytebeat Drums MPC/desktop plugins, plus the
# Bytebeat Machine Linux VST3.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_REPO="${SDK_REPO:-https://github.com/inmusic-software/mpc-plugin-builder-sdk.git}"
SDK_COMMIT="${SDK_COMMIT:-a5949cf6fe2cbfb95bcbe0ddf1f7ef555e71f2be}"
PYTHON="${PYTHON:-python3}"

MACHINE_NAME="Bytebeat Machine"
MACHINE_SRC="$ROOT/mpc"
MACHINE_SLUG="bytebeat-machine"
MACHINE_VERSION="0.2.0"

DRUMS_NAME="Bytebeat Drums"
DRUMS_SRC="$ROOT/drums/mpc"
DRUMS_SLUG="bytebeat-drums"
DRUMS_VERSION="0.1.0"

if [ -n "${SDK_DIR:-}" ]; then
    case "$SDK_DIR" in
        /*) ;;
        *) SDK_DIR="$ROOT/$SDK_DIR" ;;
    esac
else
    SDK_DIR="$ROOT/.sdk"
fi

echo "== Bytebeat build =="
echo "   repo: $ROOT"
echo "   SDK:  $SDK_DIR @ $SDK_COMMIT"

if [ ! -d "$SDK_DIR/.git" ]; then
    echo "-- cloning MPC Plugin SDK"
    git clone "$SDK_REPO" "$SDK_DIR"
fi
git -C "$SDK_DIR" fetch --quiet origin "$SDK_COMMIT" || true
git -C "$SDK_DIR" checkout --quiet "$SDK_COMMIT"

ZIG_ARGS=()
if [ -n "${ZIG:-}" ]; then ZIG_ARGS=(--zig "$ZIG"); fi

HOST_SIM="$SDK_DIR/.sdk-tools/host_sim"
if [ ! -x "$HOST_SIM" ] && [ ! -f "$HOST_SIM" ]; then
    echo "error: host_sim not found at $HOST_SIM" >&2
    exit 1
fi

mkdir -p "$ROOT/dist"

build_mpc_plugin()
{
    local src="$1" name="$2" slug="$3" notes="$4" version="$5"
    local plugin_dir="$SDK_DIR/plugins/$name"

    echo
    echo "== $name =="

    echo "-- copying plugin into the SDK"
    rm -rf "$plugin_dir"
    mkdir -p "$SDK_DIR/plugins"
    cp -a "$src" "$plugin_dir"

    # Both projects share the header-only core in mpc/src. The SDK build has
    # no outside include path, so put the core next to the plugin source.
    cp "$ROOT/mpc/src/BytebeatCompile.h" "$ROOT/mpc/src/BytebeatVM.h" \
       "$ROOT/mpc/src/BytebeatEngine.h" "$ROOT/mpc/src/FormulaLibrary.h" \
       "$plugin_dir/src/"

    echo "-- building MPC (native + ARM32 + ARM64)"
    "$PYTHON" "$SDK_DIR/tools/build.py" "$plugin_dir" --target all "${ZIG_ARGS[@]}"

    echo "-- building desktop VST2"
    "$PYTHON" "$SDK_DIR/tools/build.py" "$plugin_dir" --target desktop "${ZIG_ARGS[@]}"

    echo "-- running host simulator test"
    local native=""
    for ext in so dylib dll; do
        f="$plugin_dir/build/native/$slug.$ext"
        if [ -f "$f" ]; then native="$f"; break; fi
    done
    if [ -z "$native" ]; then
        echo "error: native test library not found" >&2
        exit 1
    fi
    local test_wav="$ROOT/dist/.host-sim-$slug.wav"
    "$HOST_SIM" "$native" test --seconds 3 --notes "$notes" --wav "$test_wav"
    rm -f "$test_wav"

    echo "-- packaging MPC release asset"
    "$PYTHON" "$SDK_DIR/tools/package.py" "$plugin_dir" --out "$ROOT/dist"

    echo "-- packaging desktop VST2"
    local desktop="$plugin_dir/build/desktop/$name"
    if [ -d "$desktop" ]; then
        local ddir="$ROOT/dist/$slug-desktop-VST2"
        rm -rf "$ddir"
        cp -a "$desktop" "$ddir"
        cp "$ROOT/LICENSE" "$ROOT/NOTICE.md" "$ROOT/dist/"
        ( cd "$ROOT/dist" && rm -f "$slug-$version-linux-vst2.zip" \
            && zip -qr "$slug-$version-linux-vst2.zip" "$slug-desktop-VST2" LICENSE NOTICE.md )
    fi
}

build_mpc_plugin "$MACHINE_SRC" "$MACHINE_NAME" "$MACHINE_SLUG" "60,64,67" "$MACHINE_VERSION"
build_mpc_plugin "$DRUMS_SRC" "$DRUMS_NAME" "$DRUMS_SLUG" "36,38,40" "$DRUMS_VERSION"

if [ "${SKIP_VST3:-0}" != "1" ] && [ -f "$ROOT/vst3/CMakeLists.txt" ]; then
    if [ ! -f "$ROOT/third_party/JUCE/CMakeLists.txt" ]; then
        if [ -d "$ROOT/.git" ]; then
            echo "-- fetching JUCE submodule"
            git -C "$ROOT" submodule update --init --recursive
        else
            echo "error: third_party/JUCE is missing; clone with --recursive or run git submodule update --init" >&2
            exit 1
        fi
    fi
    echo
    echo "== Bytebeat Machine VST3 =="
    cmake -S "$ROOT/vst3" -B "$ROOT/vst3/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/vst3/build" -j"$(nproc)"

    VST3_BUNDLE="$ROOT/vst3/build/BytebeatMachine_artefacts/Release/VST3/$MACHINE_NAME.vst3"
    if [ ! -d "$VST3_BUNDLE" ]; then
        echo "error: VST3 bundle not found at $VST3_BUNDLE" >&2
        exit 1
    fi
    rm -rf "$ROOT/dist/$MACHINE_NAME.vst3"
    cp -a "$VST3_BUNDLE" "$ROOT/dist/"
    ( cd "$ROOT/dist" && rm -f "$MACHINE_SLUG-$MACHINE_VERSION-linux-vst3.zip" \
        && zip -qr "$MACHINE_SLUG-$MACHINE_VERSION-linux-vst3.zip" "$MACHINE_NAME.vst3" LICENSE NOTICE.md \
        && zip -qj "$MACHINE_SLUG-$MACHINE_VERSION-linux-vst3.zip" "$ROOT/LICENSES/QuickJS-NG-MIT.txt" )

    echo
    echo "== Bytebeat Drums VST3 =="
    cmake -S "$ROOT/drums/vst3" -B "$ROOT/drums/vst3/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/drums/vst3/build" -j"$(nproc)"

    DRUMS_VST3_BUNDLE="$ROOT/drums/vst3/build/BytebeatDrums_artefacts/Release/VST3/$DRUMS_NAME.vst3"
    if [ ! -d "$DRUMS_VST3_BUNDLE" ]; then
        echo "error: drum VST3 bundle not found at $DRUMS_VST3_BUNDLE" >&2
        exit 1
    fi
    rm -rf "$ROOT/dist/$DRUMS_NAME.vst3"
    cp -a "$DRUMS_VST3_BUNDLE" "$ROOT/dist/"
    ( cd "$ROOT/dist" && rm -f "$DRUMS_SLUG-$DRUMS_VERSION-linux-vst3.zip" \
        && zip -qr "$DRUMS_SLUG-$DRUMS_VERSION-linux-vst3.zip" "$DRUMS_NAME.vst3" LICENSE NOTICE.md )
fi

echo
echo "Done. Release assets:"
ls -lh "$ROOT/dist"/*.zip 2>/dev/null || true
