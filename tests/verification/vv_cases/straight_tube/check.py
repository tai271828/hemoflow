#!/usr/bin/env python
"""Verification checks for the straight-tube (Poiseuille) case.

Oracles: Hagen-Poiseuille pressure drop, parabolic velocity profile,
mass conservation. See doc/VV/vv_implementation_plan.md, Case A.
"""

import json
import sys
from pathlib import Path

import numpy as np

CASE = Path(__file__).resolve().parent
sys.path.insert(0, str(CASE.parents[1]))  # tests/verification

from vvlib import metrics, oracles  # noqa: E402
from vvlib.checking import Check, main_wrapper  # noqa: E402

# Quick-variant profile tolerance calibrated on the initial run (measured 5.4 %
# at R/dx ~ 14; dominated by staircase wall error, halves at the full grid).
TOL = {
    "quick": {"mass": 0.02, "dp": 0.10, "profile": 0.08, "peak": 0.05},
    "full": {"mass": 0.01, "dp": 0.05, "profile": 0.04, "peak": 0.03},
}


def compute_checks(variant, sim):
    case = json.loads((CASE / "case.json").read_text())
    D = case["params"]["D"] / 1000.0
    R = D / 2.0
    tol = TOL[variant]
    outdir = CASE / case["variants"][variant]["sims"][sim]["output_dir"]

    npz = metrics.load_npz(CASE / "input" / f"vox_{variant}_c.npz")
    _, th = metrics.load_state(metrics.list_states(outdir)[-1])
    flows = metrics.opening_flows(th, npz)
    inlet = metrics.find_opening(flows, normal=[1, 0, 0])
    outlet = metrics.find_opening(flows, normal=[-1, 0, 0])
    q_in, q_out = inlet["Q_m3s"], -outlet["Q_m3s"]

    checks = [
        Check("steadiness", metrics.steadiness(outdir), 0.0, tol_abs=0.005),
        Check("mass_balance", (q_in - q_out) / q_in, 0.0, tol_abs=tol["mass"]),
    ]

    # Pressure drop between x = L/4 and x = 3L/4 vs Hagen-Poiseuille
    b = th.bounds
    x1 = b[0] + 0.25 * (b[1] - b[0])
    x2 = b[0] + 0.75 * (b[1] - b[0])
    p1 = metrics.plane_pressure_and_flow(th, x1)
    p2 = metrics.plane_pressure_and_flow(th, x2)
    q_mid = 0.5 * (p1["Q_m3s"] + p2["Q_m3s"])
    dp_expected = oracles.hagen_poiseuille_dp(q_mid, D, x2 - x1)
    checks.append(Check("pressure_drop_Pa", p1["p_mean_Pa"] - p2["p_mean_Pa"],
                        float(dp_expected), tol_rel=tol["dp"]))

    # Developed velocity profile at x = 3L/4 (line along z through the axis)
    origin = [x2, inlet["center_m"][1], inlet["center_m"][2]]
    offs, u = metrics.velocity_profile(th, origin, [0, 0, 1], R)
    u_ana = oracles.poiseuille_profile(offs, R, q_mid)
    checks.append(Check("profile_l2_error", metrics.profile_l2_error(offs, u, u_ana, 0.9 * R),
                        0.0, tol_abs=tol["profile"]))

    u_mean = q_mid / (np.pi * R**2)
    checks.append(Check("peak_velocity_ratio", float(u.max() / u_mean), 2.0,
                        tol_rel=tol["peak"]))
    return checks


if __name__ == "__main__":
    main_wrapper(compute_checks, CASE)
