# HemoFlow verification suite

Automated verification of the HemoFlow pipeline (preprocessor + LB solver)
against analytical oracles. Design rationale: `doc/VV/vv_rationale.md`;
implementation plan: `doc/VV/vv_implementation_plan.md`; measured results:
`doc/VV/vv_report.md`.

## Layout

```
vvlib/                  shared library: geometry generators, oracles, metrics, check runner
vv_cases/<case>/        one directory per verification case:
    case.json           generator parameters + variant/sim table
    vox_{quick,full}.json   voxelizer configs
    sim_*.xml           solver configs (quick = pre-merge, full = nightly)
    check.py            metric extraction + assertions vs oracles & baselines
    baselines.json      recorded measurements (committed; regression guard)
    input/, output_*/   generated artifacts (git-ignored)
run_case.py             orchestrator: generate -> voxelize -> solve -> check
convergence_study.py    grid-convergence (observed order) study on the straight tube
test_preprocessor_integration.py   marker: preproc  (no solver needed)
test_solver_cases.py               markers: solver_quick / solver_full
0-pipe/, 1-flow_split/, 2-aneurisk_C0002/   historical manual cases
```

## Cases and oracles

| Case | Geometry | Checks |
|---|---|---|
| straight_tube | D=3 mm, L=30 mm pipe | mass balance, Hagen–Poiseuille Δp, parabolic profile (L2), peak/mean = 2, steadiness |
| side_opening_tube | D=4 mm main + D=2 mm lateral branch (T-junction) | mass balance (3 openings), resistance-network flow split, redeveloped profile |
| y_bifurcation | D=4 mm parent → D=3.2/2.6 mm daughters | Murray-law split (aneurysm mode) and resistance split (pressure outlets), mass balance, daughter profiles |

## Environment

```bash
# Python env (once):
cd preprocessor && uv venv --python 3.9 .venv && VIRTUAL_ENV=$PWD/.venv uv pip install -e ".[dev,vv]"
# Solver (once):
mkdir -p build && cd build && cmake .. && make -j 8
```

## Running

```bash
PY=preprocessor/.venv/bin/python

# fast tier, no solver (~1 min) — geometry generation + voxelization assertions
$PY -m pytest tests/verification -m preproc

# solver tier, minutes per case (4 MPI ranks by default; VV_MPI_NP overrides)
$PY -m pytest tests/verification -m solver_quick

# nightly tier: production resolution + grid-convergence order
$PY -m pytest tests/verification -m solver_full

# one case by hand:
$PY tests/verification/run_case.py tests/verification/vv_cases/straight_tube --variant quick --np 4
```

## Tolerances and baselines

Every check has a physics tolerance (documented in the case's `check.py`, wide
enough for the grid it runs on) and a regression guard: measured values are
compared against the committed `baselines.json` with ±3 % drift allowance.
After an intentional physics/numerics change, re-record with:

```bash
$PY tests/verification/run_case.py <case> --variant quick --update-baselines
```

and commit the diff. Never update baselines to silence a failure you cannot
explain.
