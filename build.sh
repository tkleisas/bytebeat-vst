#!/usr/bin/env bash
# Build the Bytebeat Machine MPC plugin, desktop VST2 and Linux VST3.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_REPO="${SDK_REPO:-https://github.com/inmusic-software/mpc-plugin-builder-sdk.git}"
SDK_COMMIT="${SDK_COMMIT:-a5949cf6fe2cbfb95bcbe0ddf1f7ef555e71f2be}"
PYTHON="${PYTHON:-python3}"
PLUGIN="$ROOT/mpc"
PLUGIN_NAME="Bytebeat Machine"
VERSION="0.2.0"

if [ -n "${SDK_DIR:-}" ]; then
    case "$SDK_DIR" in
        /*) ;;
        *) SDK_DIR="$ROOT/$SDK_DIR" ;;
    esac
else
    SDK_DIR="$ROOT/.sdk"
fi

echo "== Bytebeat Machine build =="
echo "   repo: $ROOT"
echo "   SDK:  $SDK_DIR @ $SDK_COMMIT"

if [ ! -d "$SDK_DIR/.git" ]; then
    echo "-- cloning MPC Plugin SDK"
    git clone "$SDK_REPO" "$SDK_DIR"
fi
git -C "$SDK_DIR" fetch --quiet origin "$SDK_COMMIT" || true
git -C "$SDK_DIR" checkout --quiet "$SDK_COMMIT"

rm -rf "$SDK_DIR/plugins/$PLUGIN_NAME"
mkdir -p "$SDK_DIR/plugins"
cp -a "$PLUGIN" "$SDK_DIR/plugins/$PLUGIN_NAME"

ZIG_ARGS=()
if [ -n "${ZIG:-}" ]; then ZIG_ARGS=(--zig "$ZIG"); fi

echo "-- building MPC (native + ARM32 + ARM64)"
"$PYTHON" "$SDK_DIR/tools/build.py" "$SDK_DIR/plugins/$PLUGIN_NAME" --target all "${ZIG_ARGS[@]}"

echo "-- building desktop VST2"
"$PYTHON" "$SDK_DIR/tools/build.py" "$SDK_DIR/plugins/$PLUGIN_NAME" --target desktop "${ZIG_ARGS[@]}"

echo "-- running host simulator test"
HOST_SIM="$SDK_DIR/.sdk-tools/host_sim"
NATIVE=""
for ext in so dylib dll; do
    f="$SDK_DIR/plugins/$PLUGIN_NAME/build/native/bytebeat-machine.$ext"
    if [ -f "$f" ]; then NATIVE="$f"; break; fi
done
if [ -z "$NATIVE" ]; then
    echo "error: native test library not found" >&2
    exit 1
fi
if [ ! -x "$HOST_SIM" ] && [ ! -f "$HOST_SIM" ]; then
    echo "error: host_sim not found at $HOST_SIM" >&2
    exit 1
fi
mkdir -p "$ROOT/dist"
TEST_WAV="$ROOT/dist/.host-sim-test.wav"
"$HOST_SIM" "$NATIVE" test --seconds 3 --notes 60,64,67 --wav "$TEST_WAV"
rm -f "$TEST_WAV"

echo "-- packaging MPC release asset"
mkdir -p "$ROOT/dist"
"$PYTHON" "$SDK_DIR/tools/package.py" "$SDK_DIR/plugins/$PLUGIN_NAME" --out "$ROOT/dist"

echo "-- packaging desktop VST2"
DESKTOP="$SDK_DIR/plugins/$PLUGIN_NAME/build/desktop/$PLUGIN_NAME"
if [ -d "$DESKTOP" ]; then
    rm -rf "$ROOT/dist/desktop-VST2"
    cp -a "$DESKTOP" "$ROOT/dist/desktop-VST2"
    cp "$ROOT/LICENSE" "$ROOT/NOTICE.md" "$ROOT/dist/" 2>/dev/null || true
    ( cd "$ROOT/dist" && rm -f "bytebeat-machine-$VERSION-linux-vst2.zip" && zip -qr "bytebeat-machine-$VERSION-linux-vst2.zip" desktop-VST2 LICENSE NOTICE.md )
fi

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
    echo "-- building VST3"
    cmake -S "$ROOT/vst3" -B "$ROOT/vst3/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/vst3/build" -j"$(nproc)"

    VST3_BUNDLE="$ROOT/vst3/build/BytebeatMachine_artefacts/Release/VST3/$PLUGIN_NAME.vst3"
    if [ ! -d "$VST3_BUNDLE" ]; then
        echo "error: VST3 bundle not found at $VST3_BUNDLE" >&2
        exit 1
    fi
    rm -rf "$ROOT/dist/$PLUGIN_NAME.vst3"
    cp -a "$VST3_BUNDLE" "$ROOT/dist/"
    ( cd "$ROOT/dist" && rm -f "bytebeat-machine-$VERSION-linux-vst3.zip" \
        && zip -qr "bytebeat-machine-$VERSION-linux-vst3.zip" "$PLUGIN_NAME.vst3" LICENSE NOTICE.md \
        && zip -qj "bytebeat-machine-$VERSION-linux-vst3.zip" "$ROOT/LICENSES/QuickJS-NG-MIT.txt" )
fi

echo
echo "Done. Release assets:"
ls -lh "$ROOT/dist" 2>/dev/null || true
