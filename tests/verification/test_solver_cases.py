"""End-to-end solver verification wrappers.

Each test drives run_case.py: generate geometry -> voxelize -> solve -> check
against analytical oracles and regression baselines.

Markers:
    solver_quick - minutes per case, pre-merge tier
    solver_full  - production resolution, nightly tier

MPI ranks default to 4; override with the VV_MPI_NP environment variable.
"""

import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOLVER = REPO / "build" / "hemoFlow"

CASES = ["straight_tube", "side_opening_tube", "y_bifurcation"]

needs_solver = pytest.mark.skipif(
    not SOLVER.exists(), reason="build/hemoFlow not built (see README.md)")


def _run(case, variant):
    subprocess.run(
        [sys.executable, str(HERE / "run_case.py"),
         str(HERE / "vv_cases" / case),
         "--variant", variant, "--np", os.environ.get("VV_MPI_NP", "4")],
        check=True)


@needs_solver
@pytest.mark.solver_quick
@pytest.mark.parametrize("case", CASES)
def test_case_quick(case):
    _run(case, "quick")


@needs_solver
@pytest.mark.solver_full
@pytest.mark.parametrize("case", CASES)
def test_case_full(case):
    _run(case, "full")


@needs_solver
@pytest.mark.solver_full
def test_grid_convergence_order():
    subprocess.run(
        [sys.executable, str(HERE / "convergence_study.py"),
         "--np", os.environ.get("VV_MPI_NP", "4")],
        check=True)
