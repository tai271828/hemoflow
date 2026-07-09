# 1-flow_split (historical)

Four-opening branch geometry checking Murray-law outlet distribution. Kept as
the historical record.

Superseded by the automated, parametric case
`tests/verification/vv_cases/y_bifurcation/` (see
`tests/verification/README.md`), which covers both the Murray and the
pressure-outlet flow split with assertions and regression baselines.

Note: run `Murray_check.py <case dir>`; the geometry npz defaults to
`input/vox_branch_5M_c.npz` (matching `input/input_branch_vox.config`) and can
be overridden with `--npz`.
