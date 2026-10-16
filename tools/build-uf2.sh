#!/usr/bin/env bash
# Build the firmware and copy the UF2 somewhere easy to find.
#
#   ./tools/build-uf2.sh              Pico 2 W  -> dist/ds5-desktop-pico2w.uf2
#   ./tools/build-uf2.sh --waveshare  Waveshare -> dist/ds5-desktop-waveshare.uf2
#   ./tools/build-uf2.sh --clean      wipe the build directory first
#
# The first run downloads the Pico SDK into .deps/ and can take a while.
# Later runs only recompile what changed.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BOARD_ARGS=()
OUT_NAME="ds5-desktop-pico2w.uf2"
BUILD_DIR="build/wake"

usage() {
  cat <<'USAGE'
Usage: ./tools/build-uf2.sh [--waveshare] [--clean]

Writes the flashable firmware to dist/.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --waveshare)
      BOARD_ARGS+=(--waveshare)
      OUT_NAME="ds5-desktop-waveshare.uf2"
      BUILD_DIR="build/waveshare"
      shift
      ;;
    --clean)
      BOARD_ARGS+=(--clean)
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

"$ROOT/tools/build-macos.sh" "${BOARD_ARGS[@]}"

SRC="$ROOT/$BUILD_DIR/ds5-bridge.uf2"
if [[ ! -f "$SRC" ]]; then
  echo "error: build finished but $SRC is missing" >&2
  exit 1
fi

mkdir -p "$ROOT/dist"
cp "$SRC" "$ROOT/dist/$OUT_NAME"

echo
echo "Ready to flash:"
echo "  $ROOT/dist/$OUT_NAME"
