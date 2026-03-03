#!/usr/bin/env bash
# Run the HemoFlow preprocessor on Frank's input data.
#
# Prerequisites:
#   - .venv at project root with dependencies installed:
#       uv pip install numpy-stl vtk scipy pynrrd --python .venv/bin/python
#       uv pip install -e preprocessor/ --no-deps --python .venv/bin/python
#
# Usage:
#   cd <project-root>/data-will-not-commit/data-from-frank-input/
#   bash run_preprocessor.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."
PYTHON="$PROJECT_ROOT/.venv/bin/python"
DATA_DIR="$PROJECT_ROOT/data-will-not-commit/data-from-frank"
OUTPUT_DIR="$PROJECT_ROOT/output"
REF_NPZ="$DATA_DIR/input/geometry_w_coil_c.npz"

# Symlink config into data dir so relative paths (input/*.stl) resolve correctly
ln -sf "$PROJECT_ROOT/preprocessor_config.json" "$DATA_DIR/preprocessor_config.json"

mkdir -p "$OUTPUT_DIR"
"$PYTHON" -m preprocessor "$DATA_DIR/preprocessor_config.json" --output-dir "$OUTPUT_DIR"

"$PYTHON" "$SCRIPT_DIR/verify_npz.py" "$OUTPUT_DIR/geometry_w_coil_c.npz" "$REF_NPZ"

