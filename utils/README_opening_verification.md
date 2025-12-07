# Opening Data Verification Tools

These scripts help verify that opening information flows correctly from the preprocessor to the solver.

## Workflow

### Step 1: Inspect NPZ Geometry File

Extract and display all opening information from the preprocessor output:

```bash
cd /home/tai/work-my-projects/workspace-hemoflow/hemoflow/preprocessor
python inspect_npz_openings.py ./testcase/input/vox_NAP180_ane_PED_5x20_5M_c.npz
```

**Output:**
- Console: Detailed opening information (labels, coordinates, radius, normal vectors)
- JSON file: `*_openings.json` with structured opening metadata

**What to check:**
- Number of openings matches expectations
- Opening labels (10, 11, 12, etc.)
- Radius values in mm
- Center coordinates in voxel space
- Normal direction vectors
- Number of voxels per opening

---

### Step 2: Extract Opening Data from Simulation Results

Extract velocity and pressure at opening locations from HDF5 output:

```bash
cd /home/tai/work-my-projects/workspace-hemoflow/hemoflow/utils
python extract_opening_data.py ../testcase/output/hemoFlow_0.h5 \
                                ../testcase/input/vox_NAP180_ane_PED_5x20_5M_c.npz
```

**Output:**
- Console: Opening statistics with velocity and pressure data
- JSON file: `*_opening_analysis.json` with simulation results

**What to check:**
- All opening labels from NPZ are found in simulation
- Velocity magnitudes are reasonable (not NaN, not extreme)
- Flow rates align with boundary conditions
- Pressure values are physical

---

### Step 3: Compare Preprocessor and Solver Data

Verify that opening geometry matches between NPZ and simulation:

```bash
cd /home/tai/work-my-projects/workspace-hemoflow/hemoflow/utils
python compare_openings.py ../preprocessor/input/vox_NAP180_ane_PED_5x20_5M_c_openings.json \
                           ../testcase/output/hemoFlow_0_opening_analysis.json
```

**Output:**
- Console: Side-by-side comparison of all opening properties

**What to check:**
- ✓ All radii match exactly
- ✓ Center coordinates match (< 0.1 voxel difference)
- ✓ Normal vectors match (< 0.01 difference)
- ✓ Voxel counts match
- ✓ Resolution (dx vs C_l) matches

---

## Expected Results

### ✓ Success Indicators

1. **Number of openings:** NPZ count == HDF5 count
2. **Labels:** All NPZ labels found in HDF5
3. **Geometry:** Radius, center, normal match within tolerance
4. **Voxels:** Same voxel count for each opening label
5. **Resolution:** dx (NPZ) == C_l (HDF5)

### ⚠️ Warning Signs

1. **Missing labels:** Opening in NPZ not found in HDF5
   - Check XML config: Does `<label>` match NPZ `openingIndex`?
   - Check error messages in solver output

2. **Geometry mismatch:** Radius or center differs
   - Possible unit conversion error
   - Possible coordinate transformation issue

3. **Zero velocity:** Opening shows no flow
   - Check boundary condition type in XML
   - Check `parameter` value in XML
   - Verify opening isn't blocked by geometry

4. **NaN values:** Velocity or pressure is NaN
   - Numerical instability (check convergence tolerance)
   - Check LBM parameters (omega, velocity magnitude)

---

## Troubleshooting

### Issue: "Label X found in config xml, but not in the geometry file!"

**Diagnosis:** XML opening label doesn't exist in NPZ `openingIndex`

**Solution:**
```bash
# Inspect NPZ to see actual labels
python inspect_npz_openings.py geometry.npz

# Update XML <label> to match NPZ openingIndex values
```

---

### Issue: Different number of openings

**Diagnosis:** XML defines different number of openings than NPZ contains

**Solution:**
- NPZ has N openings: XML must define exactly N `<opening_0>` to `<opening_N-1>` tags
- Check preprocessor output for how many openings were detected

---

### Issue: Flow rate is zero at inlet

**Diagnosis:** Velocity BC not applied correctly

**Check:**
1. Opening type in XML (should be `type="1"` for velocity BC)
2. Parameter value (should be > 0 for inlet)
3. Time scale function file exists and loads correctly
4. Console output shows "Processing opening" for this label

---

### Issue: Unrealistic flow distribution

**Diagnosis:** Murray's law outlets not working correctly

**Check:**
1. Murray outlets use `type="2"`
2. At least one velocity inlet (`type="1"`) exists
3. Opening radii in NPZ are correct
4. Console shows Murray calculation: "scaledVFR" values

---

## Files Generated

```
preprocessor/input/
  └── vox_NAP180_ane_PED_5x20_5M_c_openings.json    ← Opening metadata from NPZ

testcase/output/
  └── hemoFlow_0_opening_analysis.json              ← Simulation results at openings
```

Both JSON files can be imported into analysis scripts or visualization tools.

---

## Quick Verification Example

```bash
# Full pipeline verification
cd /home/tai/work-my-projects/workspace-hemoflow/hemoflow

# 1. Inspect preprocessor output
python preprocessor/inspect_npz_openings.py \
    testcase/input/vox_NAP180_ane_PED_5x20_5M_c.npz

# 2. Run simulation (if not already run)
cd build
./hemoFlow ../testcase/test_Freeflow.xml
cd ..

# 3. Extract simulation data at openings
python utils/extract_opening_data.py \
    testcase/output/hemoFlow_0.h5 \
    testcase/input/vox_NAP180_ane_PED_5x20_5M_c.npz

# 4. Compare preprocessor vs solver
python utils/compare_openings.py \
    testcase/input/vox_NAP180_ane_PED_5x20_5M_c_openings.json \
    testcase/output/hemoFlow_0_opening_analysis.json
```

Look for the final message:
- ✓ `SUCCESS: All opening information matches between preprocessor and solver!`
- ⚠️  `WARNING: Some openings have mismatches. Review details above.`
