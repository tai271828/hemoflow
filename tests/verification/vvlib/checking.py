"""Check evaluation, reporting and regression baselines for verification cases.

Every case's check.py builds a list of Check objects and hands them to
``evaluate``. A check passes when |measured - expected| / |expected| <= tol_rel
(or |measured| <= tol_abs for expected == 0). Measured values are additionally
compared against the case's committed baselines.json (regression guard); use
--update-baselines after an intentional change.
"""

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path


BASELINE_TOL_REL = 0.03  # drift allowed against a recorded baseline


@dataclass
class Check:
    name: str
    measured: float
    expected: float
    tol_rel: float = None      # relative tolerance vs expected
    tol_abs: float = None      # absolute tolerance (for expected == 0)
    info: dict = field(default_factory=dict)

    def error(self):
        if self.tol_abs is not None:
            return abs(self.measured)
        return abs(self.measured - self.expected) / abs(self.expected)

    def tol(self):
        return self.tol_abs if self.tol_abs is not None else self.tol_rel

    def passed(self):
        return self.error() <= self.tol()


def evaluate(checks, baselines_path, key, update_baselines=False, metrics_out=None):
    """Evaluate checks, compare against baselines, print a report.

    ``key`` identifies the variant/sim inside baselines.json
    (e.g. "quick/murray"). Returns True when everything passed.
    """
    baselines_path = Path(baselines_path)
    baselines = {}
    if baselines_path.exists():
        baselines = json.loads(baselines_path.read_text())
    recorded = baselines.get(key, {})

    ok = True
    rows = []
    new_record = {}
    for c in checks:
        status = "PASS" if c.passed() else "FAIL"
        if not c.passed():
            ok = False
        base = recorded.get(c.name)
        drift = ""
        if base is not None and abs(base) > 1e-30:
            rel = abs(c.measured - base) / abs(base)
            if rel > BASELINE_TOL_REL:
                status += " (baseline drift %.1f%%)" % (100 * rel)
                ok = False
        elif base is not None and abs(c.measured - base) > 1e-12:
            status += " (baseline drift)"
            ok = False
        new_record[c.name] = c.measured
        rows.append((c.name, c.measured, c.expected, c.error(), c.tol(), status))

    width = max(len(r[0]) for r in rows) + 2
    print(f"{'check':<{width}}{'measured':>14}{'expected':>14}{'error':>10}{'tol':>8}  status")
    for name, meas, exp, err, tol, status in rows:
        print(f"{name:<{width}}{meas:>14.6g}{exp:>14.6g}{err:>10.3%}{tol:>8.1%}  {status}")

    if update_baselines:
        baselines[key] = new_record
        baselines_path.write_text(json.dumps(baselines, indent=2) + "\n")
        print(f"baselines updated: {baselines_path} [{key}]")

    if metrics_out:
        Path(metrics_out).write_text(json.dumps(
            {c.name: {"measured": c.measured, "expected": c.expected,
                      "error": c.error(), "tol": c.tol(), "passed": c.passed(),
                      **({"info": c.info} if c.info else {})}
             for c in checks}, indent=2) + "\n")
    return ok


def main_wrapper(compute_checks, case_dir):
    """Shared CLI for the per-case check.py scripts."""
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="quick", choices=["quick", "full"])
    ap.add_argument("--sim", default="main", help="simulation tag inside the variant")
    ap.add_argument("--update-baselines", action="store_true")
    args = ap.parse_args()

    checks = compute_checks(args.variant, args.sim)
    ok = evaluate(
        checks,
        baselines_path=Path(case_dir) / "baselines.json",
        key=f"{args.variant}/{args.sim}",
        update_baselines=args.update_baselines,
        metrics_out=Path(case_dir) / f"metrics_{args.variant}_{args.sim}.json",
    )
    sys.exit(0 if ok else 1)
