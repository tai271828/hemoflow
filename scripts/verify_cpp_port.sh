#!/usr/bin/env bash
# Verify the C++ preprocessor port against the Python reference output.
#
# This script:
#   1. Builds the C++ preprocessor (hemoFlowPreprocessor)
#   2. Runs it on the test dataset
#   3. Compares the C++ output against the reference NPZ (produced by Python)
#
# Prerequisites:
#   - cmake, g++ (C++17), zlib-dev installed
#   - Test data at data-will-not-commit/data-from-frank/input/
#   - Reference NPZ at data-will-not-commit/data-from-frank/input/geometry_w_coil_c.npz
#   - Python .venv with numpy (for verify_npz.py):
#       uv pip install numpy --python .venv/bin/python
#
# Usage:
#   bash scripts/verify_cpp_port.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."
BUILD_DIR="$PROJECT_ROOT/build"
DATA_DIR="$PROJECT_ROOT/data-will-not-commit/data-from-frank"
CPP_OUTPUT_DIR="$DATA_DIR/output_cpp"
REF_NPZ="$DATA_DIR/input/geometry_w_coil_c.npz"
PYTHON="$PROJECT_ROOT/.venv/bin/python"
BINARY="$BUILD_DIR/hemoFlowPreprocessor"

# ── 1. Build ─────────────────────────────────────────────────────────────────
echo "=== Step 1: Building C++ preprocessor ==="
mkdir -p "$BUILD_DIR"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DENABLE_MPI=OFF -DBUILD_HDF5=OFF \
    -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
cmake --build "$BUILD_DIR" --target hemoFlowPreprocessor 2>&1 | tail -5

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Build failed — $BINARY not found"
    exit 1
fi
echo "Binary: $BINARY"
echo ""

# ── 2. Run C++ preprocessor ──────────────────────────────────────────────────
echo "=== Step 2: Running C++ preprocessor ==="

if [ ! -d "$DATA_DIR/input" ]; then
    echo "ERROR: Test data not found at $DATA_DIR/input/"
    exit 1
fi

# Symlink config into data dir so relative paths (input/*.stl) resolve correctly
ln -sf "$PROJECT_ROOT/preprocessor_config.json" "$DATA_DIR/preprocessor_config.json"

rm -rf "$CPP_OUTPUT_DIR"
mkdir -p "$CPP_OUTPUT_DIR"

cd "$DATA_DIR"
"$BINARY" preprocessor_config.json --output-dir output_cpp --no-debug
cd "$PROJECT_ROOT"
echo ""

# ── 3. Verify output ─────────────────────────────────────────────────────────
echo "=== Step 3: Verifying C++ output against Python reference ==="

CPP_NPZ="$CPP_OUTPUT_DIR/geometry_w_coil_c.npz"
if [ ! -f "$CPP_NPZ" ]; then
    echo "ERROR: C++ output not found at $CPP_NPZ"
    exit 1
fi
if [ ! -f "$REF_NPZ" ]; then
    echo "ERROR: Reference NPZ not found at $REF_NPZ"
    exit 1
fi

# Ensure numpy is available
if ! "$PYTHON" -c "import numpy" 2>/dev/null; then
    echo "Installing numpy into .venv via uv..."
    uv pip install numpy --python "$PYTHON"
fi

"$PYTHON" "$SCRIPT_DIR/verify_npz.py" "$CPP_NPZ" "$REF_NPZ"
