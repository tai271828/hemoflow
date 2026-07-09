#!/usr/bin/env python
"""Grid-convergence (solution verification) study on the straight-tube case.

Runs the case at three resolutions with a constant refinement ratio and
diffusive time-step scaling (dt ~ dx^2, so the LB relaxation time is identical
on every grid), measures the dimensionless flow resistance

    K = dp * pi * D_eff^4 / (128 mu L Q)   (exact value: 1)

and computes the observed order of convergence. Asserts a minimum observed
order and records everything under the "convergence" key of the case's
baselines.json.

D_eff is the effective diameter measured from the simulated cross-section area
(2*sqrt(A/pi)). Using the *nominal* diameter instead makes the error
non-monotone across grids: with (full-way) bounce-back walls the effective
wall position shifts by O(dx/2) depending on how the circle lands on each
grid, and D^4 amplifies that geometric jitter above the truncation error
being measured. With D_eff the error converges monotonically at the ~first
order expected of staircase walls (measured p ~ 1.1).

Usage:
    preprocessor/.venv/bin/python tests/verification/convergence_study.py [--np 4]
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CASE = HERE / "vv_cases" / "straight_tube"
SOLVER = REPO / "build" / "hemoFlow"

sys.path.insert(0, str(HERE))
from vvlib import metrics, oracles  # noqa: E402
from vvlib.constants import MU_BLOOD  # noqa: E402

# Three grids at refinement ratio 1.5; dt scales diffusively (dt ~ dx^2).
# Note: dx = 0.30 mm (10 voxels per diameter) is unstable (NaN at startup) —
# the practical lower bound for this setup is ~12 voxels per diameter.
DX_MM = [0.24, 0.16, 0.10667]
RATIO = 1.5
DT_AT_02 = 8e-5           # tau = 0.519 at dx = 0.2 mm (nu = 3.22e-6)
SIM_LENGTH = 0.5          # [s]; startup time constant ~0.12 s
MIN_ORDER = 0.8

SIM_XML = """
    <simulation>
        <dt>{dt}</dt>
        <blockSize>50</blockSize>
        <outputDir>{outdir}</outputDir>
        <simLength>{length}</simLength>
        <saveFrequency>0.1</saveFrequency>
    </simulation>

    <geometry>
        <file>input/vox_conv{tag}_c.npz</file>
        <opening_0>
            <name>Inlet</name>
            <type>1</type>
            <label>10</label>
            <timeScaleFunction>input/stationary.txt</timeScaleFunction>
            <parameter>0.11</parameter>
        </opening_0>
        <opening_1>
            <name>Outlet</name>
            <type>3</type>
            <label>11</label>
            <timeScaleFunction></timeScaleFunction>
            <parameter>0</parameter>
        </opening_1>
    </geometry>

    <flowdiverter>
        <linCoeff>0</linCoeff>
        <quadCoeff>0</quadCoeff>
    </flowdiverter>
"""


def run_resolution(dx_mm, np_ranks, reuse=False):
    tag = str(dx_mm).replace(".", "p")

    vox = {
        "geometry_original_stl": "input/straight_tube_Srf.stl",
        "centerline_vtp": "input/straight_tube_Ctl.vtp",
        "stent_mesh_base": "",
        "target_elements": "",
        "target_dx": str(dx_mm),
        "output_base_name": f"input/vox_conv{tag}_",
        "cutWidth": "1",
        "distance": "4",
        "rotation": {"enabled": False},
    }
    dt = DT_AT_02 * (dx_mm / 0.20) ** 2
    outdir = f"output_conv_{tag}"

    if not (reuse and (CASE / outdir).exists()):
        (CASE / f"vox_conv{tag}.json").write_text(json.dumps(vox, indent=2))
        subprocess.run([sys.executable, "-m", "preprocessor", f"vox_conv{tag}.json"],
                       cwd=CASE, check=True, capture_output=True, text=True)

        xml = CASE / f"sim_conv{tag}.xml"
        xml.write_text(SIM_XML.format(dt=dt, outdir=outdir, length=SIM_LENGTH, tag=tag))
        if (CASE / outdir).exists():
            shutil.rmtree(CASE / outdir)
        cmd = [str(SOLVER), xml.name]
        if np_ranks > 1:
            cmd = ["mpirun", "-np", str(np_ranks)] + cmd
        with open(CASE / f"solve_conv_{tag}.log", "w") as fh:
            subprocess.run(cmd, cwd=CASE, check=True, stdout=fh, stderr=subprocess.STDOUT)

    # Dimensionless resistance between x = L/4 and 3L/4, with the effective
    # diameter taken from the simulated cross-section area (see module docstring)
    _, th = metrics.load_state(metrics.list_states(CASE / outdir)[-1])
    b = th.bounds
    x1 = b[0] + 0.25 * (b[1] - b[0])
    x2 = b[0] + 0.75 * (b[1] - b[0])
    p1 = metrics.plane_pressure_and_flow(th, x1)
    p2 = metrics.plane_pressure_and_flow(th, x2)
    q = 0.5 * (p1["Q_m3s"] + p2["Q_m3s"])
    dp = p1["p_mean_Pa"] - p2["p_mean_Pa"]
    d_eff = 2.0 * np.sqrt(0.5 * (p1["area_m2"] + p2["area_m2"]) / np.pi)
    K = dp * np.pi * d_eff**4 / (128.0 * MU_BLOOD * (x2 - x1) * q)
    print(f"dx = {dx_mm} mm: dt = {dt:.3g} s, D_eff = {d_eff*1000:.4f} mm, "
          f"K = {K:.5f} (exact 1)")
    return float(K)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--np", type=int, default=4)
    ap.add_argument("--min-order", type=float, default=MIN_ORDER)
    ap.add_argument("--reuse", action="store_true",
                    help="re-postprocess existing output_conv_* runs without re-solving")
    args = ap.parse_args()

    if not SOLVER.exists():
        sys.exit(f"solver not found: {SOLVER}")

    # geometry + stationary.txt (reuse the case generator)
    subprocess.run([sys.executable, str(HERE / "run_case.py"), str(CASE),
                    "--variant", "quick", "--skip-voxelize", "--skip-solve",
                    "--sim", "main"], check=False, capture_output=True)
    ks = [run_resolution(dx, args.np, reuse=args.reuse) for dx in DX_MM]

    p = oracles.observed_order(ks[0], ks[1], ks[2], RATIO)
    errs = [abs(k - 1.0) for k in ks]
    print(f"K errors: {['%.4f' % e for e in errs]}")
    print(f"observed order of convergence: p = {p:.3f} (required >= {args.min_order})")

    baselines_path = CASE / "baselines.json"
    baselines = json.loads(baselines_path.read_text()) if baselines_path.exists() else {}
    baselines["convergence"] = {
        "dx_mm": DX_MM, "K": ks, "observed_order": float(p),
    }
    baselines_path.write_text(json.dumps(baselines, indent=2) + "\n")

    if not np.isfinite(p) or p < args.min_order:
        sys.exit(1)


if __name__ == "__main__":
    main()
