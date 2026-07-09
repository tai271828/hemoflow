# HemoFlow V&V Implementation Plan

Executable plan for building the comprehensive verification & validation test
suite. The rationale behind this plan is in `vv_rationale.md`. This document is
written to be executed by an AI coding agent (or a developer) with no prior
context: every needed fact is either stated (with the source file it was verified
against) or turned into an explicit inspection step.

---

## 0. Ground rules for the executing agent

1. **Work phase by phase, in order.** Each phase ends with acceptance criteria;
   do not start the next phase until they pass.
2. **Verify before assuming.** Facts below marked *(verified)* were read from the
   code at plan-writing time — re-check them if the referenced file changed.
   Steps marked **CHECKPOINT** are mandatory inspections whose outcome may adjust
   the details that follow.
3. **Paths are relative to the repository root** unless stated otherwise.
4. **Do not commit generated binary artifacts** (npz, VTI, large STL). Recent
   history added input files to `.gitignore`; generators + configs live in git,
   artifacts are regenerated. Small text baselines (JSON metrics) are committed.
5. **Record every measured number.** When a check passes, write the measured
   value into the case's `baselines.json` (Phase 5) so tolerances can be
   tightened into regression bounds.
6. When a solver run misbehaves, first check the preprocessor log for the
   detected openings (count, radius, normal) — most failures are geometry
   preparation failures, not solver failures.

---

## 1. Key facts about the system *(all verified at plan time)*

### 1.1 Pipeline

```
STL surface + centerline VTP
        │  python -m preprocessor <config.json>       (package: preprocessor/)
        ▼
vox_<name>_c.npz   (geometry flags + opening metadata)
        │  [mpirun -np N] ./build/hemoFlow <config.xml>
        ▼
output/**/*.vti    (velocity [m/s], density [Pa] point data)
```

- Preprocessor entry point: `python -m preprocessor config.json` or the
  `preprocessor` console script (`preprocessor/pyproject.toml`). The old
  `preprocessor/main.py` referenced by existing `voxelize.sh` scripts **no longer
  exists** (see defect list, §2.3).
- Solver usage: `./hemoFlow ./input.xml ./checkpoint [OPTIONS]`
  (`hemoFlow.cpp:174`); MPI-capable. Build with CMake per `README.md`.
- Conda env for the preprocessor: `conda env create -f preprocessor/environment.yml`
  (env name `lbmpre`).

### 1.2 Preprocessor input conventions

- Config is JSON (`preprocessor/preprocessor/config.py`). Relevant keys:
  `geometry_original_stl`, `centerline_vtp`, `stent_mesh_base` (may be `""`),
  `target_elements` **or** `target_dx` (one of them, other empty), `output_base_name`,
  `cutWidth` (default 1), `distance` (opening-detection threshold in voxels,
  default 4), `si_factor` (default 0.001 → **geometry files are in millimeters**).
- Centerline VTP requirements (`preprocessor/preprocessor/centerline.py`):
  - `vtkPolyData` with one **polyline per outlet**, each running **from the inlet
    to that outlet**.
  - A point-data array named exactly `Radius` (local vessel radius, mm).
  - The inlet is taken from the *start* of line 0; tangents use the first two /
    last-and-third-from-last points, so give each line ≥ 4 well-spaced points and
    avoid duplicate consecutive points.
  - Openings are **sorted by radius, descending** before labeling.
- Voxel labels (`preprocessor/preprocessor/constants.py`): `WALL=1`, `FLUID=2`,
  `INLET=10`, `OUTLET=11`, `OUTLET_REST=12`. Don't hardcode label↔opening
  mapping in tests — read `openingIndex` from the npz.
- Output npz keys (`preprocessor/preprocessor/io_utils.py:75-85`):
  `geometryFlag`, `dx` (meters), `openingIndex`, `openingRadius` (voxels),
  `openingNormalizedQRatio`, `openingCenter` (voxel coords), `openingNormal`
  (+ `stent`, `linear`, `quadratic` when a stent is present).

### 1.3 Solver XML conventions

- Opening types: `1` velocity (parabolic profile, `parameter` = **average**
  velocity, internally doubled to peak, `hemoFlow.cpp:438`), `2` Murray outlet,
  `3` pressure, `4` freeflow.
- Two configuration modes (`hemoFlow.cpp:398-530`):
  - `<mode>aneurysm</mode>`: only `opening_0` (inlet velocity + scale function)
    must be defined; the solver auto-assigns: opening 0 → velocity inlet, last
    opening → pressure outlet, all middle openings → Murray. Order = npz order =
    radius-descending. Used by `tests/verification/1-flow_split/flow_split.xml`.
  - No mode: every opening needs an `<opening_N>` block with `name`, `type`,
    `label` (must match a value in npz `openingIndex`), `timeScaleFunction`,
    `parameter`. Used by `tests/verification/0-pipe/pipe.xml`.
- Steady inflow scale function: `input/stationary.txt` (constant 1.0).
- All XML physical parameters are SI.

### 1.4 Known geometric limitations (README.md)

- Every opening must lie on an **axis-aligned bounding-box face** of the geometry.
- An opening must **not** touch a bbox edge or corner (mis-detection risk).
- Design consequence for all synthetic geometries: tube ends must be cut flat,
  flush with a bbox face, perpendicular to an axis, and comfortably away from
  bbox edges.

### 1.5 Blood properties used by existing tests

ρ = 1055 kg/m³, ν = 3.3×10⁻⁶ m²/s (μ = ρν ≈ 3.48×10⁻³ Pa·s). Use these in all
analytical formulas. (Note defect: `pressure_drop.py` uses 3.33e-6 in one place
and 3.22e-6 in another — see §2.3.)

---

## 2. Current-state inventory (what we have / what's missing)

### 2.1 What exists

| Asset | Location | State |
|---|---|---|
| Straight-pipe Poiseuille verification | `tests/verification/0-pipe/` (pipe.xml, `pressure_drop.py`, prep STL/VTP) | Runs end-to-end manually; prints pressure-drop error and profile plot; **no assertions** |
| Flow-split / Murray verification | `tests/verification/1-flow_split/` (`Murray_check.py`, branch STL/VTP/STEP, aneurysm-mode XML) | Check script exists; **broken npz filename** (§2.3); prints, no assertions |
| Patient-specific case (Aneurisk C0002) | `tests/verification/2-aneurisk_C0002/` | Inputs + XML only; **no check script at all** |
| dx-sensitivity campaigns (EasyVVUQ) | `tests/sensitivity/{0-pipe,1-aneurisk}_sensitivity/` | Hard-coded absolute user paths; not runnable as-is |
| Preprocessor unit tests | `preprocessor/tests/` (`test_geometry.py`, `test_models.py`, `conftest.py`) | Good pytest tests for `geometry.py` utilities and dataclasses only |
| Manual smoke-test XMLs per opening type | `testcase/test_{Velocity,Pressure,Murray,Freeflow,Aneurysm}.xml` + patient geometries | Manual, undocumented, no oracle |
| Verification narrative | `doc/VV/hemoflow_VV.ipynb` | Pipe + Aneurisk sections; stent sections are TODO |

### 2.2 What is missing (gap list driving this plan)

1. **No automated pass/fail anywhere** — nothing asserts; nothing can gate CI.
2. **No bifurcation-geometry test** with a defined oracle (the flow-split case is
   a 4-opening branch aimed only at Murray outlets, and it's currently broken).
3. **No straight-tube-with-side-opening test** — the lateral-face opening code
   path and the documented edge/corner limitation are untested.
4. **No parametric/reproducible geometry** — all test geometries are opaque
   binary artifacts; some referenced files are missing from the repo.
5. **No preprocessor-level integration tests** (voxelize → assert openings);
   unit tests cover only pure-math helpers.
6. **No convergence-order verification** — single-resolution comparisons only.
7. **No mass-conservation check** as a first-class assertion.
8. **No regression baselines** for the patient-specific case.
9. **Stent/flow-diverter model unverified** (notebook TODO).
10. **No validation against experimental data** (out of scope here; phase stub).

### 2.3 Concrete defects found (fix in Phase 6)

| # | Defect | Evidence |
|---|---|---|
| D1 | `Murray_check.py` loads `./input/vox_murray_5M_c.npz` but the voxelizer config produces `vox_branch_5M_c.npz` | `tests/verification/1-flow_split/Murray_check.py:46` vs `input/input_branch_vox.config` (`output_base_name: vox_branch_5M_`) |
| D2 | All `voxelize.sh` scripts call `python .../preprocessor/main.py`, which was removed in the package refactor | e.g. `tests/verification/0-pipe/input/voxelize.sh`; entry point is now `python -m preprocessor` |
| D3 | `pressure_drop.py` inconsistent viscosity (3.33e-6 vs 3.22e-6), literal `3.14` instead of `np.pi`, misleading docstring (formula is correct for a circular pipe along a diameter, docstring says parallel plates) | `tests/verification/0-pipe/pressure_drop.py:9,70,74,22-44` |
| D4 | Sensitivity campaigns hard-code `/home/lsandor/00_software/hemoflow/` | `tests/sensitivity/*/**_campaign.py:7` |
| D5 | No `Radius`-array fallback (`MaximumInscribedSphereRadius`) though marked TODO — **all reference centerlines in the repo use the VMTK name**, so they crash the current reader | `preprocessor/preprocessor/centerline.py:47` |
| D6 | `io.cpp`/`io.h` use HighFive parallel-HDF5 symbols unconditionally; the build breaks with serial-only HDF5 even though CMake promises graceful degradation — guard with `#ifdef HDF5` | `io.cpp:200,203,241`, `io.h:13-17` (found in Phase 3) |
| D7 | `slice.calculateScaleAndShift` divides by `targetElements` before checking `target_dx`; configs setting only `target_dx` crash | `preprocessor/preprocessor/slice.py:110` (found in Phase 4) |
| D8 | `detect_inlets_outlets` appends the `None` returned for degenerate single-voxel opening candidates (the guard is commented out), crashing on coarse grids | `preprocessor/preprocessor/opening_detection.py:122` (found in Phase 4) |

---

## 3. Target test architecture

```
tests/
├── verification/
│   ├── vvlib/                      # NEW shared library (Phase 3)
│   │   ├── __init__.py
│   │   ├── geometry_gen.py         # parametric STL+VTP generators (Phase 1)
│   │   ├── metrics.py              # VTI loading, slicing, flow rates, profiles
│   │   └── oracles.py              # analytical formulas (§6.4)
│   ├── vv_cases/                   # NEW case definitions (NOT "cases/" — that
│   │   │                           #   name is matched by the repo .gitignore)
│   │   ├── straight_tube/          # Case A
│   │   ├── side_opening_tube/      # Case B
│   │   └── y_bifurcation/          # Case C
│   │       ├── generate.py         # writes input/ geometry from parameters
│   │       ├── vox_config.json
│   │       ├── sim_quick.xml       # CI-sized run
│   │       ├── sim_full.xml        # nightly-sized run
│   │       ├── check.py            # computes metrics dict + assertions
│   │       └── baselines.json      # measured values (committed)
│   ├── test_preprocessor_integration.py   # NEW Phase 2 (pytest, no solver)
│   ├── test_solver_cases.py               # NEW Phase 5 (pytest, needs built solver)
│   ├── run_case.py                        # NEW orchestrator: generate→vox→solve→check
│   ├── 0-pipe/ 1-flow_split/ 2-aneurisk_C0002/   # existing, repaired in Phase 6
├── sensitivity/                    # existing EasyVVUQ campaigns (repaired)
└── conftest.py                     # markers: preproc, solver_quick, solver_full
```

Test tiers (pytest markers):

| Marker | Needs | Budget | When |
|---|---|---|---|
| *(none — unit)* | numpy | seconds | every commit (existing `preprocessor/tests` + new) |
| `preproc` | conda env `lbmpre` | < 2 min total | every commit |
| `solver_quick` | built `hemoFlow` | ~ minutes/case | pre-merge / on demand |
| `solver_full` | built `hemoFlow`, MPI | hours | nightly / release |

---

## 4. Phase 1 — Parametric geometry generators

**Goal:** `tests/verification/vvlib/geometry_gen.py` producing, from numeric
parameters, a watertight-walled tube surface STL (open only at the flat opening
faces) and a matching centerline VTP with a `Radius` point array — for the three
geometries below.

**CHECKPOINT 1.1** — Before writing code, inspect the reference pair to copy its
conventions:

```bash
python - <<'EOF'
import pyvista as pv
s = pv.read('tests/verification/0-pipe/input/pipe_Srf_prep.stl')
c = pv.read('tests/verification/0-pipe/input/pipe_Ctl_prep.vtp')
print('STL bounds (mm):', s.bounds, 'open edges:', s.extract_feature_edges(boundary_edges=True, feature_edges=False, manifold_edges=False).n_points)
print('VTP arrays:', c.point_data.keys(), 'n lines:', c.n_lines)
print('VTP bounds:', c.bounds)
EOF
```

Record: are tube ends open (expected: yes — openings are holes on bbox faces)?
Do centerline endpoints reach the surface end planes or stop short? Match
whatever you observe.

### 4.1 Recommended construction method

Signed-distance-field + marching cubes (robust, no fragile mesh booleans):

1. Model each vessel segment as a **capsule/frustum SDF** along its axis
   (linearly interpolated radius for tapered segments).
2. Union SDFs with `min()` — this automatically blends junctions smoothly.
3. Sample the SDF on a fine grid (resolution ≈ D/40) covering exactly the target
   bounding box; extract the zero isosurface with
   `pyvista.ImageData(...).contour([0.0])` (or `skimage.measure.marching_cubes`).
4. Because the sample grid ends exactly at the bbox faces where tubes terminate,
   the extracted surface is naturally **open and flush** at those faces —
   matching the opening convention (§1.4). Keep every opening ≥ 2·D away from
   any bbox edge.
5. Export STL in **mm**; export centerline as `pyvista.PolyData` with `lines`
   cells (one polyline inlet→outlet per outlet, ≥ 20 points each) and point array
   `Radius` (mm); save with `.save('..._Ctl.vtp')`.

### 4.2 Geometry specifications (defaults; all mm)

**Case A — straight tube** (`make_straight_tube(D=3.0, L=30.0)`)
Axis +x, centered in y,z. Openings on x− and x+ faces. Bbox pad ≥ 6 in y,z.

**Case B — straight tube with side opening (T-junction)**
(`make_side_opening_tube(D_main=3.0, L_main=30.0, D_side=2.0, L_side=8.0, x_side=15.0)`)
Main tube as Case A; side branch along +y from the main axis at x=15, ending on
the y+ face. Three openings: x− (inlet, D=3), x+ (D=3), y+ (D=2). The two D=3
openings tie under radius-descending sort — **CHECKPOINT 4.2**: after
voxelization confirm which one got which label by comparing `openingCenter`
against the known end coordinates; make `check.py` resolve openings by position,
never by order.

**Case C — Y-bifurcation**
(`make_y_bifurcation(D_parent=4.0, L_parent=12.0, D_d1=3.2, D_d2=2.6, angle_deg=35, L_arm=10.0, L_straight=8.0)`)
Parent along +x from the x− face. At the junction each daughter runs `L_arm` at
±`angle_deg` in the xy-plane, then turns (arc, radius ≥ 2·D) back to +x and runs
`L_straight` to end **perpendicular on the x+ face**, well separated in y.
Unequal daughter diameters are deliberate (distinguishable Murray split).
Centerlines: two polylines, both starting at the parent inlet point, following
parent→junction→daughter paths. Radii: parent 2.0 → blend → daughter radius.

### 4.3 Acceptance criteria (Phase 1)

- For each geometry: `python -m preprocessor <case vox_config.json>` (from the
  case directory, `target_elements ≈ 3e5`, `cutWidth=1`, `distance=4`) completes
  without error and its log/npz reports exactly 2 / 3 / 3 openings respectively.
- Detected `openingRadius · dx` within 10 % of the specified radii; detected
  `openingNormal` within 10° of ±axis; opening centers on the expected faces.
- Generators are deterministic (same params → byte-identical STL) and covered by
  a smoke pytest (`test_geometry_gen.py`: watertight walls except N open ends,
  bounds as specified).

---

## 5. Phase 2 — Preprocessor integration tests (no solver)

**Goal:** `tests/verification/test_preprocessor_integration.py`, marker
`preproc`, one test per geometry, each < 60 s:

1. Generate the geometry into `tmp_path` (small: `target_elements = 2e5`).
2. Run the pipeline in-process
   (`preprocessor.pipeline.run_preprocessing_pipeline(PreprocessorConfig...)`) —
   fall back to `subprocess` `python -m preprocessor` if the API needs display or
   cwd tricks.
3. Load the npz and assert:
   - opening count (A: 2, B: 3, C: 3);
   - radii within 10 % of spec, sorted descending;
   - normals within 10° of the expected axes;
   - each opening's `openingCenter` lies on its expected bbox face (± `distance`
     voxels);
   - voxel volume: fluid fraction within 15 % of the analytic tube volume /
     bbox volume; every non-wall boundary voxel belongs to a labeled opening
     (no leaks: labels ∉ {0,1,2} on faces only).
4. Parametrize Case A over `target_elements ∈ {1e5, 4e5}` to catch
   resolution-dependent regressions in opening detection.

These tests are the cheap, always-on answer to "voxelization broke on a
bifurcation" — the original motivation for this task.

**Acceptance:** `pytest tests/verification -m preproc` green in the `lbmpre` env
from a clean checkout; no network, no solver, < 5 min.

---

## 6. Phase 3 — Solver verification cases

**Prerequisite:** built solver at `build/hemoFlow` (README.md §Setup; Palabos
must be present in `palabos/`). If it cannot be built in your environment, stop
and report — do not fake results.

### 6.1 Shared library `vvlib/metrics.py`

Refactor the working logic of `tests/verification/0-pipe/pressure_drop.py` and
`1-flow_split/Murray_check.py` (slice → `integrate_data`; opening-local sampling
via `openingCenter·dx` inset 8 voxels along `openingNormal`, box-clipped — copy
`make_box` from `Murray_check.py`) into functions returning plain floats:

```
load_final_state(case_dir) -> pyvista dataset       # newest output/**/*.vti
opening_flow_rates(state, npz) -> {opening_idx: Q}  # signed, m³/s
pressure_drop(state, p1, p2, normal) -> float       # area-averaged, Pa
axis_velocity_profile(state, origin, direction, D) -> (r, u)
```

`vvlib/oracles.py`:

```
hagen_poiseuille_dp(Q, D, L, mu)     = 128*mu*L*Q/(pi*D**4)
poiseuille_profile(r, R, Q)          = 2*Q/(pi*R**2)*(1 - (r/R)**2)
murray_split(D_list, Q_in)           = Q_in * D_i**3 / sum(D_j**3)
resistance_split(D_list, L_list, Q_in)   # R_i = 128*mu*L_i/(pi*D_i**4); Q_i ∝ 1/R_i (equal outlet pressures)
observed_order(f_coarse, f_mid, f_fine, r) = ln((f3-f2)/(f2-f1))/ln(r)
```

### 6.2 Flow regime (all cases)

Keep laminar and cheap: inlet mean velocity `u = 0.11 m/s` for D=3 mm →
Re = uD/ν ≈ 100. Steady inflow (`stationary.txt`). Entrance length
L_e ≈ 0.06·Re·D ≈ 18 mm — but the solver imposes a parabolic inlet, so the flow
starts developed; measure ≥ 5·D away from junctions/openings anyway. Simulation
length: quick = 0.5 s, full = 2.5 s; **CHECKPOINT 6.2** — verify steadiness by
comparing the last two saved timesteps (max relative velocity change < 0.5 %);
if not steady, extend.

### 6.3 Case definitions and checks

Every case: `generate.py` → `vox_config.json` → `sim_{quick,full}.xml` →
`check.py` (prints a metrics dict as JSON **and** asserts). XML: copy
`tests/verification/0-pipe/pipe.xml` as the explicit-openings template.
Quick variant: `target_elements ≈ 5e5`, `dt = 4e-5`; full: `2e6`, `dt = 2e-5`.

**Case A — straight tube.** Openings: velocity inlet (type 1, param 0.11) +
pressure outlet (type 3, param 0).

| Check | Oracle | Tolerance (quick / full) |
|---|---|---|
| A1 mass balance (Q_in + Q_out)/Q_in | 0 | < 2 % / < 1 % |
| A2 Δp between x=L/4 and x=3L/4 | Hagen–Poiseuille | < 10 % / < 5 % |
| A3 velocity profile at x=3L/4, L2 rel. error | parabola | < 5 % / < 3 % |
| A4 centerline peak u_max/u_mean | 2.0 | < 5 % / < 3 % |

**Case B — side-opening tube.** Openings: inlet (x−, type 1, 0.11), both
outlets pressure (type 3, param 0).

| Check | Oracle | Tolerance |
|---|---|---|
| B1 mass balance (3 openings) | 0 | < 2 % / < 1 % |
| B2 flow split Q_side/Q_main | resistance network: R_main uses the x_side→end segment (L=15), R_side uses L_side (measure actual centerline lengths) | < 15 % / < 10 % (junction minor losses are unmodeled — expect bias; record baseline and tighten to regression bound) |
| B3 developed profile in main tube downstream (x=27, ≥4·D past junction) | parabola with measured Q | < 8 % |

**Case C — Y-bifurcation.** Two sub-cases from the same geometry/npz:

- **C-murray**: `<mode>aneurysm</mode>` XML (inlet + auto Murray/pressure) — the
  bifurcation exercise of the solver's Murray feature. NOTE *(verified,
  `hemoFlow.cpp:452`)*: in aneurysm mode the *smallest* opening becomes a
  pressure outlet, so with only 3 openings you get 1 Murray + 1 pressure outlet.
  To make **both** daughters Murray-type instead, use explicit openings with
  type 2 — **CHECKPOINT 6.3**: read `hemoFlow.cpp` Murray handling to confirm
  type-2 openings are permitted without a pressure outlet; if a pressure anchor
  is required, keep the aneurysm-mode variant and check the Murray outlet only.
- **C-pressure**: explicit XML, both daughters type 3 at 0 Pa.

| Check | Oracle | Tolerance |
|---|---|---|
| C1 mass balance | 0 | < 2 % / < 1 % |
| C2 (C-murray) Q_i split | Murray: D_i³/ΣD_j³ (3.2³ vs 2.6³ → 65 % / 35 %) | < 5 % per outlet (this verifies the implemented law) |
| C3 (C-pressure) Q_i split | resistance network with measured centerline lengths | < 15 %, then regression-tighten |
| C4 daughter profiles at ≥ 4·D past junction | parabola with measured Q_i | < 8 % |

### 6.4 Acceptance criteria (Phase 3)

- `python tests/verification/run_case.py cases/<case> --variant quick` performs
  generate → voxelize → solve → check end-to-end and exits nonzero on failed
  assertions.
- All quick-variant checks pass at the stated initial tolerances (or, where the
  physics bias is real — B2/C3 — the measured value is recorded and the
  tolerance replaced by baseline ± 3 %).
- `baselines.json` committed for every case with measured metrics, grid size,
  dt, solver git hash, and date.

---

## 7. Phase 4 — Grid convergence (solution verification)

On Case A only (cheapest with an exact oracle):

1. Run three resolutions, constant refinement ratio r≈1.5 in dx (via `target_dx`:
   0.30, 0.20, 0.133 mm), fixed physical setup, `dt` scaled ∝ dx² (diffusive
   scaling — keeps LB Mach/viscosity behavior comparable).
2. Metric: A2 pressure drop (integral quantities converge cleanly).
3. Compute observed order p; assert **p ≥ 0.8** initially (staircase wall
   boundaries typically degrade LBM below its bulk 2nd order). Record p in
   baselines; after first run, tighten to measured − 0.2.
4. Wire as marker `solver_full` test; also leave the EasyVVUQ campaigns (after
   the Phase 6 path fix) documented as the deeper optional study.

---

## 8. Phase 5 — Automation harness & regression baselines

1. `tests/verification/run_case.py`: argparse (`case dir`, `--variant`,
   `--np <mpi ranks>`, `--keep-output`); stages: generate (skip if inputs
   fresh), voxelize, solve, check; writes `metrics.json` next to outputs.
2. `tests/verification/test_solver_cases.py`: thin pytest wrappers invoking
   `run_case.py` per case; markers `solver_quick` / `solver_full`; skip cleanly
   (`pytest.skip`) when `build/hemoFlow` is absent.
3. `tests/conftest.py`: register the three markers.
4. Regression flow: `check.py` compares each metric to both the physics
   tolerance **and** `baselines.json ± 3 %`; a physics-pass but
   baseline-drift is reported as a warning-level failure with a
   `--update-baselines` escape hatch.
5. Aneurisk case (`2-aneurisk_C0002`): no oracle — add `check.py` computing
   mass balance (physics assert < 2 %) plus golden metrics (mean/max velocity,
   per-opening Q) as pure regression. `solver_full` only.
6. Document in `tests/verification/README.md`: environment setup, the three
   tiers, how to run each, how to update baselines.

**Acceptance:** from a clean checkout with built solver: unit + `preproc` tiers
green; `pytest -m solver_quick` green; README instructions reproduce it.

---

## 9. Phase 6 — Repair existing assets (defects from §2.3)

| Defect | Fix |
|---|---|
| D1 | Point `Murray_check.py` at the npz name from the vox config (make it a CLI arg with the correct default) |
| D2 | Update all `voxelize.sh` to `python -m preprocessor <config>` (run from the repo's `preprocessor/` install or with `PYTHONPATH=preprocessor`) |
| D3 | Single viscosity constant module-level (`nu = 3.3e-6`), `np.pi`, fix docstring; then port callers to `vvlib` (Phase 3 already does) |
| D4 | Replace hard-coded `SOFTWARE_PATH` with env var `HEMOFLOW_ROOT` defaulting to repo root derived from `__file__` |
| D5 | Implement the `centerline.py` TODO: fall back to `MaximumInscribedSphereRadius` when `Radius` is absent; add a unit test with a tiny synthetic VTP |

Keep `0-pipe` and `1-flow_split` directories working (they are the historical
record referenced by the VV notebook) but mark them superseded by `cases/` in
their READMEs.

**Acceptance:** each fixed script runs (voxelize.sh at least to a successful
preprocessor start; campaigns at least to campaign-directory creation without
path errors); new unit test for D5 passes.

---

## 10. Phase 7 — Documentation & reporting

1. Extend `doc/VV/hemoflow_VV.ipynb` **or** (preferred) add
   `doc/VV/vv_report.md` generated from the committed `baselines.json` files:
   per-case table of metric, oracle, measured, tolerance, pass/fail, plus the
   convergence-order result and the Case A profile plot.
2. Update `README.md`: tick the "Validate and verify" TODO, add a "Testing"
   section pointing at `tests/verification/README.md`.
3. Add stub section "Validation (future work)" listing candidate datasets (FDA
   nozzle benchmark, published bifurcation PIV) — do not implement.

---

## 11. Definition of done (whole plan)

- [ ] Phase 1: three parametric geometries generate + voxelize cleanly
- [ ] Phase 2: `pytest -m preproc` green, < 5 min, no solver needed
- [ ] Phase 3: Cases A/B/C quick variants pass physics checks end-to-end
- [ ] Phase 4: observed convergence order measured and asserted
- [ ] Phase 5: one-command orchestration + committed baselines + docs
- [ ] Phase 6: five defects fixed
- [ ] Phase 7: report + README updated

Rough budget: Phases 1–2 are pure Python (no solver) ≈ 1–2 agent-days; Phase 3
depends on solver build + CPU time (quick cases are sized for minutes on 4–8
cores); Phases 4–7 ≈ 1 agent-day plus one nightly-scale compute pass.
