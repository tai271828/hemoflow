#!/usr/bin/env python
"""End-to-end orchestrator for one verification case.

Stages: generate geometry -> voxelize -> solve -> check.

Usage (run with the preprocessor venv python):
    preprocessor/.venv/bin/python tests/verification/run_case.py \
        tests/verification/vv_cases/straight_tube --variant quick [--np 4]

Options let you skip stages that are already done (e.g. --skip-solve to
re-evaluate checks on existing output).
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent          # tests/verification
REPO = HERE.parents[1]
SOLVER = REPO / "build" / "hemoFlow"

sys.path.insert(0, str(HERE))
from vvlib import geometry_gen  # noqa: E402

STATIONARY = "2\n0.0\t1.0\n100.0\t1.0\n"


def generate(case_dir, case):
    input_dir = case_dir / "input"
    input_dir.mkdir(exist_ok=True)
    (input_dir / "stationary.txt").write_text(STATIONARY)
    spec = geometry_gen.GENERATORS[case["generator"]](input_dir, **case["params"])
    print(f"[generate] {case['generator']} -> {input_dir} "
          f"({len(spec['openings'])} openings)")


def voxelize(case_dir, case, variant):
    vox = case["variants"][variant]["vox"]
    print(f"[voxelize] {vox}")
    subprocess.run([sys.executable, "-m", "preprocessor", vox],
                   cwd=case_dir, check=True)


def solve(case_dir, case, variant, sim_tag, np_ranks):
    sim = case["variants"][variant]["sims"][sim_tag]
    outdir = case_dir / sim["output_dir"]
    if outdir.exists():
        shutil.rmtree(outdir)
    if not SOLVER.exists():
        sys.exit(f"solver not found: {SOLVER} (build it first, see README.md)")
    cmd = [str(SOLVER), sim["xml"]]
    if np_ranks > 1:
        cmd = ["mpirun", "-np", str(np_ranks)] + cmd
    print(f"[solve] {' '.join(cmd)} (cwd {case_dir})")
    log = case_dir / f"solve_{variant}_{sim_tag}.log"
    with open(log, "w") as fh:
        subprocess.run(cmd, cwd=case_dir, check=True, stdout=fh,
                       stderr=subprocess.STDOUT)


def check(case_dir, variant, sim_tag, update_baselines):
    cmd = [sys.executable, "check.py", "--variant", variant, "--sim", sim_tag]
    if update_baselines:
        cmd.append("--update-baselines")
    print(f"[check] {' '.join(cmd)}")
    subprocess.run(cmd, cwd=case_dir, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case_dir")
    ap.add_argument("--variant", default="quick", choices=["quick", "full"])
    ap.add_argument("--sim", default=None,
                    help="run only this sim tag (default: all sims of the variant)")
    ap.add_argument("--np", type=int, default=1, help="MPI ranks for the solver")
    ap.add_argument("--skip-generate", action="store_true")
    ap.add_argument("--skip-voxelize", action="store_true")
    ap.add_argument("--skip-solve", action="store_true")
    ap.add_argument("--update-baselines", action="store_true")
    args = ap.parse_args()

    case_dir = Path(args.case_dir).resolve()
    case = json.loads((case_dir / "case.json").read_text())
    sims = case["variants"][args.variant]["sims"]
    tags = [args.sim] if args.sim else list(sims)

    if not args.skip_generate:
        generate(case_dir, case)
    if not args.skip_voxelize:
        voxelize(case_dir, case, args.variant)
    failed = []
    for tag in tags:
        if not args.skip_solve:
            solve(case_dir, case, args.variant, tag, args.np)
        try:
            check(case_dir, args.variant, tag, args.update_baselines)
        except subprocess.CalledProcessError:
            failed.append(tag)  # keep running the remaining sims
    if failed:
        sys.exit(f"[done] FAILED checks for sims: {', '.join(failed)}")
    print("[done]")


if __name__ == "__main__":
    main()
