# HemoFlow V&V Test Plan — Rationale

This document explains *why* the HemoFlow validation & verification plan
(`vv_implementation_plan.md`) is structured the way it is. It is the conceptual
companion to that plan; read this first if you want to understand or challenge the
design, read the plan if you want to implement it.

## 1. The framing: V&V for computational physics software

HemoFlow is numerical software: a lattice-Boltzmann (LB) blood-flow solver built on
Palabos, plus a Python preprocessor that turns surface meshes (STL) and centerlines
(VTP) into voxelized simulation domains. Testing such software is governed by a
well-established body of theory (Roache, *Verification and Validation in
Computational Science and Engineering*; Oberkampf & Roy, *Verification and
Validation in Scientific Computing*; ASME V&V 20). The core distinctions:

- **Code verification** — "are we solving the equations right?" Demonstrate that
  the implemented numerics converge to known exact solutions at the expected order
  of accuracy. Tools: exact analytical solutions (Poiseuille flow), conservation
  checks, grid-convergence / observed-order studies.
- **Solution verification** — "how large is the numerical error for the problems we
  actually run?" Tools: grid refinement, Richardson extrapolation, Grid Convergence
  Index (GCI), sensitivity studies over discretization parameters (`dx`, `dt`).
- **Validation** — "are we solving the right equations?" Compare against
  experimental / clinical data (e.g. PIV measurements, 4D-flow MRI). This requires
  reference data we do not currently hold, so the plan scopes validation as a
  placeholder phase and focuses on verification, which is fully actionable today.

A key consequence: since Palabos itself is a mature, verified LB framework (as the
existing `doc/VV/hemoflow_VV.ipynb` already argues), our verification effort
concentrates on **HemoFlow-specific features** — the voxelization pipeline, opening
detection, the arbitrary-orientation parabolic inlet profile, Murray-law outlet
distribution, pressure/free-flow outlets, and the porous stent model — rather than
re-verifying the LB collision kernel.

## 2. The framing: software-engineering test theory

The numerical-physics view above says *what* to check; software engineering says
*how* to organize the checks so they keep working:

- **Test pyramid.** Many fast, deterministic unit tests at the bottom
  (preprocessor geometry math — partially present in `preprocessor/tests/`);
  fewer integration tests in the middle (voxelize a synthetic geometry, assert the
  detected openings); few, slower end-to-end system tests at the top (run the
  solver, compare with analytical solutions). The current repository is inverted:
  it has only heavyweight, manual end-to-end scripts. The plan rebuilds the base
  of the pyramid because preprocessor-level tests catch most geometry-handling
  regressions in seconds instead of CPU-hours.
- **The oracle problem.** A test is only automatable if there is a computable
  expected answer. Every case in the plan is chosen to have one: analytical
  solutions (Hagen–Poiseuille), physical invariants (mass conservation), design
  laws implemented in the code itself (Murray's law), or resistance-network
  predictions for flow splits. Where no oracle exists (the patient-specific
  Aneurisk case), the plan falls back to **regression testing**: freeze today's
  output as a golden baseline and alert on change.
- **Assertions, not printouts.** The existing scripts (`pressure_drop.py`,
  `Murray_check.py`) print relative differences and rely on a human to judge them.
  The plan converts every check into a function returning metrics plus a
  pytest-style assertion against a documented tolerance, so the suite can gate CI.
- **Reproducibility.** Current test geometries are opaque binary artifacts
  (STL/VTP/STEP files of unknown provenance, some referenced files missing from
  the repo). The plan replaces them with **parametric geometry generators**
  (scripted tube/T-junction/bifurcation builders), so every geometry is
  regenerable from a few numbers, diffable, and adjustable for grid studies.
- **Determinism and cost tiers.** Tests are tagged by cost (unit / preprocessor
  integration / solver-quick / solver-full) so developers and CI can run the cheap
  tiers on every change and the expensive tiers nightly or on demand.

## 3. Why these three geometries

The geometry matrix (straight tube → tube with side opening → Y-bifurcation)
follows the classic V&V principle of **incremental complexity**: each case adds
exactly one new physical/code feature over the previous one, so a failure isolates
the feature that broke.

1. **Straight tube (Poiseuille flow).** The minimal end-to-end case. Exercises:
   voxelization of a smooth wall, the parabolic velocity inlet, one pressure
   outlet, bulk LB dynamics. Oracles: exact Hagen–Poiseuille pressure drop, exact
   parabolic velocity profile, inflow=outflow mass balance. This case already
   partially exists (`tests/verification/0-pipe`) and is retained/hardened.

2. **Straight tube with a side opening (T-junction).** Adds: an opening on a
   *lateral* face of the axis-aligned bounding box, with the main flow passing by
   it. This directly targets two documented code risks: the README states openings
   must lie on bounding-box faces and are mis-detected near edges/corners, and the
   opening-detection code (`preprocessor/preprocessor/opening_detection.py`)
   assigns openings to faces by normal/proximity heuristics. Oracles: mass
   conservation across three openings, and the flow split predicted by a laminar
   Poiseuille resistance network (R = 128 μ L / π D⁴ per segment, equal outlet
   pressures). No fully-developed analytical field exists for the junction region
   itself, which is fine — the checks are integral (flow rates), which converge
   fast and are insensitive to local junction details.

3. **Y-bifurcation tube.** Adds: multiple daughter branches of *different*
   diameters, non-trivial centerline topology (two centerlines sharing the inlet),
   and the Murray-law outlet type — the feature the draft explicitly calls out as
   untested ("testing the simulation by running with a bifurcation tube"). It is
   also the canonical idealization of the cerebral vasculature HemoFlow targets.
   Oracles: mass conservation; Murray-law split Qᵢ ∝ Dᵢ³ when outlets are type 2
   (this verifies the implemented outlet law, an internal-consistency check);
   resistance-network split when outlets are pressure type (an independent physics
   check); recovery of parabolic profiles in the daughters far from the junction.

Each geometry is used **twice**: once as a fast preprocessor-only integration test
(voxelize, then assert the number, position, radius, and normal of detected
openings — no solver needed), and once as a solver verification case. The
preprocessor tier is what makes bifurcation coverage cheap enough to run on every
commit, answering the draft's original complaint at the unit/integration level as
well as the system level.

## 4. Why grid-convergence studies are included

A single-resolution agreement number ("2.9% off analytical") cannot distinguish
"correct code at coarse resolution" from "wrong code with compensating errors".
Observed-order-of-convergence studies (run 3+ resolutions, fit the error slope)
are the standard discriminator (Roache). The repo already gestures at this with
EasyVVUQ sensitivity campaigns over `dx`; the plan folds a lightweight 3-point
convergence check into the straight-tube case with an asserted minimum observed
order, and keeps the heavier EasyVVUQ campaigns as optional deep studies.

## 5. Why the plan is written for a cheaper executing model

The implementation plan is deliberately over-specified — exact paths, commands,
parameter tables, tolerances, code sketches, and "inspect X before assuming Y"
checkpoints — because it is meant to be executed by a less capable model without
access to the analysis that produced it. Two design rules follow:

- **Every assumption is either stated with evidence or turned into a verification
  step.** E.g. the centerline VTP format expected by the preprocessor was read
  from `centerline.py` (polylines from inlet to each outlet, point-data array
  named `Radius`) and is stated as fact; solver behaviors not yet confirmed are
  flagged as "verify first".
- **Tolerances are two-stage.** Initial tolerances are set generously from theory,
  then the executing model is instructed to record measured baselines and tighten
  the tolerances to baseline + margin, converting verification runs into durable
  regression tests.

## 6. What this plan does *not* cover, and why

- **Method of Manufactured Solutions (MMS).** The gold standard for code
  verification, but it requires source-term injection into the LB solver — an
  invasive code change out of proportion to current project needs. Revisit if a
  deeper solver-kernel verification is ever required.
- **Experimental validation.** Requires reference data (PIV / MRI / literature
  benchmark digitization, e.g. the FDA nozzle benchmark) that must be sourced
  first; the plan reserves a phase stub for it.
- **Performance/scaling tests (MPI).** Orthogonal to correctness; not V&V.
- **Stent / flow-diverter porous model verification.** Acknowledged as a gap
  (the VV notebook lists it as TODO); the plan schedules it as a later phase
  using the straight-tube-with-stent pressure-drop setup already sketched in
  `tests/verification/0-pipe/input/` (stent mesh files exist), but it is
  lower priority than the geometry matrix.
