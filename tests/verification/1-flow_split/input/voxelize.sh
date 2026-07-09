#!/bin/sh
# Voxelize this case's geometry with the hemoflow-preprocessor package.
# Set up the environment once from the repo root:
#   cd preprocessor && uv venv --python 3.9 .venv && VIRTUAL_ENV=$PWD/.venv uv pip install -e ".[dev,vv]"
# HEMOFLOW_ROOT overrides the repository root (e.g. when this dir is copied by EasyVVUQ).
REPO="${HEMOFLOW_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
exec "$REPO/preprocessor/.venv/bin/python" -m preprocessor ./input_branch_vox.config
