# Task 01 — Comprehensive Validation & Verification (V&V) test plan for HemoFlow

Extended from `task01-plan-draft.md`.

## Goal

HemoFlow (Palabos-based lattice-Boltzmann vessel flow solver + Python voxelization
preprocessor) still lacks systematic, automated V&V coverage — e.g. there is no test
that exercises the full pipeline on a bifurcation tube with pass/fail assertions.
This task produces a comprehensive, executable V&V test plan. The actual test
implementation is intentionally **deferred to a follow-up task** that a cheaper
generative-AI model can execute using the plan document produced here (per draft
requirement 4).

## Deliverables

| # | Deliverable | Location |
|---|-------------|----------|
| D1 | Inventory of existing V&V assets: what we have, what is missing | Section 2 of D3 (and summarized in D2) |
| D2 | Rationale document — the theory and thinking framework behind the plan | `doc/VV/vv_rationale.md` |
| D3 | Implementation plan — descriptive and practical enough for a cheaper genAI model to implement unattended | `doc/VV/vv_implementation_plan.md` |

## Steps

1. **Inspect current V&V assets** (draft req. 1). Survey and summarize:
   - `tests/verification/` (0-pipe, 1-flow_split, 2-aneurisk_C0002) — scripts, configs, gaps
   - `tests/sensitivity/` (EasyVVUQ campaigns)
   - `preprocessor/tests/` (pytest unit tests)
   - `testcase/` (manual smoke-test XMLs per opening type)
   - `doc/VV/hemoflow_VV.ipynb` (existing verification narrative)
   - Known solver limitations in `README.md` (openings must lie on axis-aligned
     bounding-box faces, not on edges/corners)
   Record concrete defects found (missing outlet definitions, filename mismatches,
   hard-coded paths, absence of assertions).

2. **Define the geometry test matrix** (draft req. 2). Three synthetic, parametric
   geometries of increasing complexity, each with analytical or first-principles
   oracles:
   - straight tube (Poiseuille / Hagen–Poiseuille)
   - straight tube with a side opening (T-junction; mass conservation + resistance-network flow split; exercises lateral-face opening detection)
   - Y-bifurcation tube (mass conservation, Murray-law and pressure-outlet flow
     splits, developed daughter-branch profiles)
   Each geometry serves two test layers: fast preprocessor-only integration tests
   (voxelize + assert detected openings) and full solver verification runs.

3. **Write the rationale doc** (draft req. 3) — ground the plan in:
   - V&V theory for computational physics (code verification vs. solution
     verification vs. validation; ASME V&V 20 / Oberkampf & Roy / Roache)
   - Software-engineering testing theory (test pyramid, oracles, regression,
     reproducibility, CI)
   - Why these three geometries and these specific checks.

4. **Write the implementation plan doc** (draft req. 4) — phased, with per-phase
   acceptance criteria, exact file paths, commands, parameter tables, tolerance
   values, code sketches, and explicit "verify this assumption against the code
   before proceeding" checkpoints so a cheaper model can execute it safely.

## Acceptance criteria

- `doc/VV/vv_rationale.md` and `doc/VV/vv_implementation_plan.md` exist and cover
  draft requirements 1–4.
- The plan doc alone is sufficient to implement the tests: no step requires
  information that is not either in the doc or discoverable by a stated inspection
  command.
- The inventory names concrete existing files and concrete defects, not
  generalities.

## Out of scope (→ follow-up task)

- Implementing the geometry generators, test harness, or CI wiring.
- Building the C++ solver or running simulations.
- Fixing the defects found during the inventory (they are catalogued in D3,
  Phase 4, for the executing model).
