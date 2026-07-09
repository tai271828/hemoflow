#!/usr/bin/env python
"""Verification checks for the Y-bifurcation case.

Two sub-cases run on the same voxelized geometry:
- sim "murray":   aneurysm mode -> daughter1 is a Murray outlet, daughter2 a
                  pressure outlet; the split must follow Murray's law Q_i ~ D_i^3.
- sim "pressure": both daughters are 0 Pa pressure outlets; the split must follow
                  the laminar resistance network (measured centerline lengths).

Plus mass conservation, steadiness, and redeveloped Poiseuille profiles in both
daughters near the outlets. See doc/VV/vv_implementation_plan.md, Case C.
"""

import json
import sys
from pathlib import Path

CASE = Path(__file__).resolve().parent
sys.path.insert(0, str(CASE.parents[1]))  # tests/verification

from vvlib import metrics, oracles  # noqa: E402
from vvlib.checking import Check, main_wrapper  # noqa: E402

# Daughter-profile tolerance calibrated on the initial quick run: daughter2 has
# only ~7 voxels per radius on the quick grid, so the staircase-wall L2 error is
# proportionally larger (measured 13 %; cf. 5.4 % at 14 voxels/radius in the
# straight tube). The flow-split checks are the strong oracles of this case.
TOL = {
    "quick": {"mass": 0.02, "murray": 0.05, "resistance": 0.15, "profile": 0.16},
    "full": {"mass": 0.01, "murray": 0.05, "resistance": 0.10, "profile": 0.08},
}


def compute_checks(variant, sim):
    case = json.loads((CASE / "case.json").read_text())
    p = case["params"]
    spec = json.loads((CASE / "input" / "y_bifurcation_spec.json").read_text())
    tol = TOL[variant]
    outdir = CASE / case["variants"][variant]["sims"][sim]["output_dir"]

    npz = metrics.load_npz(CASE / "input" / f"vox_{variant}_c.npz")
    _, th = metrics.load_state(metrics.list_states(outdir)[-1])
    flows = metrics.opening_flows(th, npz)
    inlet = metrics.find_opening(flows, normal=[1, 0, 0])
    # Daughters both exit through the x+ face; daughter1 (larger D) sits at
    # larger y by construction (geometry_gen.make_y_bifurcation, sign=+1).
    outlets = sorted((f for f in flows if f is not inlet),
                     key=lambda f: f["center_m"][1], reverse=True)
    d1, d2 = outlets
    q_in = inlet["Q_m3s"]
    q1, q2 = -d1["Q_m3s"], -d2["Q_m3s"]

    checks = [
        Check("steadiness", metrics.steadiness(outdir), 0.0, tol_abs=0.005),
        Check("mass_balance", (q_in - q1 - q2) / q_in, 0.0, tol_abs=tol["mass"]),
    ]

    if sim == "murray":
        frac = oracles.murray_split([p["D_d1"], p["D_d2"]])
        split_tol = tol["murray"]
    else:
        lengths = spec["centerline_path_lengths_mm"]
        frac = oracles.resistance_split(
            diameters=[p["D_d1"] / 1000.0, p["D_d2"] / 1000.0],
            lengths=[(lengths["daughter1"] - p["L_parent"]) / 1000.0,
                     (lengths["daughter2"] - p["L_parent"]) / 1000.0],
        )
        split_tol = tol["resistance"]
    checks.append(Check("daughter1_flow_fraction", q1 / (q1 + q2),
                        float(frac[0]), tol_rel=split_tol))
    checks.append(Check("daughter2_flow_fraction", q2 / (q1 + q2),
                        float(frac[1]), tol_rel=split_tol))

    # Redeveloped profiles in the daughters, 2 mm upstream of each outlet
    for name, outlet, dia_mm, q in (("daughter1", d1, p["D_d1"], q1),
                                    ("daughter2", d2, p["D_d2"], q2)):
        r = dia_mm / 2000.0
        origin = outlet["center_m"] + [-2e-3, 0.0, 0.0]
        offs, u = metrics.velocity_profile(th, origin, [0, 0, 1], r)
        u_ana = oracles.poiseuille_profile(offs, r, q)
        checks.append(Check(f"{name}_profile_l2_error",
                            metrics.profile_l2_error(offs, u, u_ana, 0.9 * r),
                            0.0, tol_abs=tol["profile"]))
    return checks


if __name__ == "__main__":
    main_wrapper(compute_checks, CASE)
