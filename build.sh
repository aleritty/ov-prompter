#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
BUILD_DIR="$SCRIPT_DIR/build"
LIBS_DIR="$SCRIPT_DIR/libs"
EXT_DIR="$SCRIPT_DIR/ext_libs"
GENAI_RUNTIME="$EXT_DIR/genai-src/runtime/lib/intel64"

usage() {
    cat <<EOF
Usage:
  $0            # build only
  PREFIX=/usr DESTDIR=/tmp/pkg $0 install  # build + install via make install
EOF
}

ACTION="${1:-build}"
case "$ACTION" in
    build)
        ;;
    install)
        shift || true
        PREFIX="${PREFIX:-/usr/local}"
        DESTDIR="${DESTDIR:-}"
        echo "=== Delegating to make install (PREFIX=$PREFIX DESTDIR=$DESTDIR) ==="
        make -C "$SCRIPT_DIR" DESTDIR="$DESTDIR" PREFIX="$PREFIX" install
        exit 0
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown action: $ACTION" >&2
        usage >&2
        exit 1
        ;;
esac

# ---- pre-flight checks ----
missing=()
[ ! -d "$EXT_DIR/genai-src" ] && missing+=("ext_libs/genai-src (run ./download-ext-libs.sh)")
[ ! -d "$GENAI_RUNTIME" ] && missing+=("ext_libs/genai-src/runtime/lib/intel64")
[ ! -f "$GENAI_RUNTIME/libopenvino_genai.so" ] && missing+=("libopenvino_genai.so in runtime")
[ ! -f "$GENAI_RUNTIME/libopenvino.so" ] && missing+=("libopenvino.so in runtime")
[ ! -f "$GENAI_RUNTIME/libopenvino_tokenizers.so" ] && missing+=("libopenvino_tokenizers.so in runtime")

if (( ${#missing[@]} )); then
    printf "Error: missing required libraries: %s\n" "${missing[*]}" >&2
    echo "Tip: Run ./download-ext-libs.sh to download prebuilt libraries" >&2
    exit 1
fi

# ---- build ----
echo "=== Building ov-Prompter ==="
rm -rf "$BUILD_DIR"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
      -DENABLE_GGUF=ON \
      -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

# ---- assemble runtime libs ----
# libs/  : the canonical runtime bundle (shipped in releases)
# build/lib/ : also populated so ./build/prompter works without wrappers
echo "-- Assembling runtime libraries from GenAI package"
rm -rf "$LIBS_DIR"
mkdir -p "$LIBS_DIR"
mkdir -p "$BUILD_DIR/lib"

copy_lib() {
    cp "$1" "$LIBS_DIR"/
    cp "$1" "$BUILD_DIR/lib"/
}

# Copy all libraries from GenAI runtime (includes OpenVINO, GenAI, tokenizers, and plugins)
echo "  Copying all runtime libraries from genai-src/runtime/lib/intel64..."
for f in "$GENAI_RUNTIME"/*.so*; do
    [ -f "$f" ] && copy_lib "$f"
done

echo ""
echo "=== Build complete ==="
echo "  Binary:  $BUILD_DIR/ov-prompter"
echo "  Libs:    $LIBS_DIR  ($(ls "$LIBS_DIR" | wc -l) files)"
echo ""
echo "Run directly:"
echo "  ./build/ov-prompter --question \"your question\""
