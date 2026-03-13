#!/usr/bin/env bash
# Reproduce preprocessing of data-from-frank dataset.
#
# Usage (from project root):
#   bash scripts/run_preprocessor_frank.sh
#
# Requires: .venv with numpy-stl, vtk, numpy installed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VENV_PYTHON="$PROJECT_ROOT/.venv/bin/python"
PREPROCESSOR="$PROJECT_ROOT/preprocessor/main.py"
CONFIG="$PROJECT_ROOT/input.frank.config"
VERIFY="$PROJECT_ROOT/scripts/verify_npz.py"
OUTPUT_DIR="$PROJECT_ROOT/output"
OUTPUT="$OUTPUT_DIR/geometry_w_coil_c.npz"
REFERENCE="$PROJECT_ROOT/data-will-not-commit/data-from-frank/input/geometry_w_coil_c.npz"

# Check prerequisites
if [ ! -x "$VENV_PYTHON" ]; then
    echo "ERROR: venv python not found at $VENV_PYTHON"
    echo "  Create it with: uv venv && uv pip install numpy-stl vtk numpy"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "ERROR: config not found at $CONFIG"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Run preprocessor
# cd to project root so config-relative paths resolve correctly.
# PYTHONPATH includes preprocessor/ so local imports (voxelizeStl, readCL, etc.) work.
echo "=== Running preprocessor ==="
cd "$PROJECT_ROOT"
PYTHONPATH="$PROJECT_ROOT/preprocessor${PYTHONPATH:+:$PYTHONPATH}" \
    "$VENV_PYTHON" "$PREPROCESSOR" "$CONFIG"
echo ""

# Verify output
if [ -f "$OUTPUT" ]; then
    echo "=== Verifying output ==="
    if [ -f "$REFERENCE" ]; then
        "$VENV_PYTHON" "$VERIFY" "$OUTPUT" "$REFERENCE"
    else
        echo "(no reference NPZ found, running without comparison)"
        "$VENV_PYTHON" "$VERIFY" "$OUTPUT"
    fi
else
    echo "ERROR: expected output not found at $OUTPUT"
    exit 1
fi
