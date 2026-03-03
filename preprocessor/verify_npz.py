#!/usr/bin/env python3
"""Verify preprocessor NPZ output for validity.

Usage:
    python verify_npz.py <npz_file> [reference_npz_file]
"""
import sys
import numpy as np


def verify(npz_path, ref_path=None):
    print(f"=== Verifying: {npz_path} ===\n")
    data = np.load(npz_path, allow_pickle=True)

    # --- List all arrays ---
    print("Arrays in NPZ:", list(data.keys()))
    print()

    # --- 1. Geometry flag checks ---
    gf = data["geometryFlag"]
    print(f"[geometryFlag] shape: {gf.shape}, dtype: {gf.dtype}")
    unique, counts = np.unique(gf, return_counts=True)
    print(f"  Unique values: {dict(zip(unique, counts))}")

    valid_base = {0, 1, 2, 10, 11}  # 12+ are velocity outlets
    unexpected = [v for v in unique if v not in valid_base and v < 12]
    if unexpected:
        print(f"  WARNING: unexpected flag values: {unexpected}")

    total = np.prod(gf.shape)
    n_fluid = np.count_nonzero(gf == 2)
    n_wall = np.count_nonzero(gf == 1)
    n_unused = np.count_nonzero(gf == 0)
    n_openings = np.count_nonzero(gf >= 10)
    fluid_ratio = n_fluid / total
    print(f"  Total voxels:  {total}")
    print(f"  Unused (0):    {n_unused}  ({n_unused/total*100:.1f}%)")
    print(f"  Wall (1):      {n_wall}  ({n_wall/total*100:.1f}%)")
    print(f"  Fluid (2):     {n_fluid}  ({n_fluid/total*100:.1f}%)")
    print(f"  Openings (>=10): {n_openings}")
    print(f"  Fluid ratio:   {fluid_ratio:.4f}", end="")
    if 0.005 < fluid_ratio < 0.15:
        print("  OK (typical for vessel in bounding box)")
    else:
        print("  WARNING: unusual fluid ratio")
    print()

    # --- 2. dx check ---
    dx = data["dx"]
    print(f"[dx] value: {dx} [m]")
    dx_um = dx[0] * 1e6
    print(f"  = {dx_um:.1f} um", end="")
    if 10 < dx_um < 500:
        print("  OK (reasonable voxel size)")
    else:
        print("  WARNING: unusual voxel size")
    print()

    # --- 3. Opening index ---
    oi = data["openingIndex"]
    print(f"[openingIndex] values: {oi}")
    n_inlets = np.count_nonzero(oi == 10)
    n_pressure = np.count_nonzero(oi == 11)
    n_velocity = np.count_nonzero(oi >= 12)
    print(f"  Inlets (10): {n_inlets}, Pressure outlets (11): {n_pressure}, Velocity outlets (12+): {n_velocity}")
    if n_inlets < 1:
        print("  WARNING: no inlet found")
    print()

    # --- 4. Opening radii ---
    orad = data["openingRadius"]
    print(f"[openingRadius] values [m]: {orad}")
    orad_mm = orad * 1000
    print(f"  In mm: {orad_mm}")
    for i, r in enumerate(orad_mm):
        status = "OK" if 0.1 < r < 20 else "WARNING: unusual"
        print(f"  Opening {i} (flag={oi[i]}): r = {r:.3f} mm  {status}")
    print()

    # --- 5. Q-ratio check ---
    qr = data["openingNormalizedQRatio"]
    print(f"[openingNormalizedQRatio] values: {qr}")
    # Outlets only (skip inlet at index 0)
    outlet_qr = qr[1:]
    qr_sum = np.sum(outlet_qr)
    print(f"  Sum of outlet Q-ratios: {qr_sum:.6f}", end="")
    if abs(qr_sum - 1.0) < 0.05:
        print("  OK (sums to ~1.0)")
    else:
        print(f"  WARNING: expected ~1.0")
    print()

    # --- 6. Tangent vectors ---
    ot = data["openingTangent"]
    print(f"[openingTangent] values:")
    for i, t in enumerate(ot):
        norm = np.linalg.norm(t)
        status = "OK" if abs(norm - 1.0) < 0.01 else f"WARNING: norm={norm:.4f}"
        print(f"  Opening {i} (flag={oi[i]}): tangent={t}  |t|={norm:.4f}  {status}")
    print()

    # --- 7. Opening centers - should be on domain boundary ---
    oc = data["openingCenter"]
    print(f"[openingCenter] values (voxel coords):")
    shape = gf.shape
    for i, c in enumerate(oc):
        on_boundary = any(
            abs(c[j]) < 1.5 or abs(c[j] - (shape[j] - 1)) < 1.5
            for j in range(3)
        )
        status = "OK (on boundary)" if on_boundary else "WARNING: not on domain boundary"
        print(f"  Opening {i} (flag={oi[i]}): center={c}  {status}")
    print()

    # --- 8. Stent ---
    stent = data["stent"]
    print(f"[stent] shape: {stent.shape}, dtype: {stent.dtype}")
    if stent.size == 0:
        print("  Empty (no stent) - OK for config with no stent")
    else:
        print(f"  Stent voxels: {np.count_nonzero(stent)}")
    print()

    # --- 9. Wall enclosure check (spot check) ---
    # Fluid should not touch domain boundary except at openings
    print("[Wall enclosure spot check]")
    faces = {
        "x=0":    gf[0, :, :],
        "x=max":  gf[-1, :, :],
        "y=0":    gf[:, 0, :],
        "y=max":  gf[:, -1, :],
        "z=0":    gf[:, :, 0],
        "z=max":  gf[:, :, -1],
    }
    all_ok = True
    for name, face in faces.items():
        fluid_on_face = np.count_nonzero(face == 2)
        opening_on_face = np.count_nonzero(face >= 10)
        if fluid_on_face > 0:
            print(f"  {name}: {fluid_on_face} bare fluid voxels on boundary  WARNING (should be 0 or opening-labeled)")
            all_ok = False
        elif opening_on_face > 0:
            print(f"  {name}: {opening_on_face} opening voxels  OK")
        else:
            print(f"  {name}: clean (no fluid/openings)  OK")
    if all_ok:
        print("  All faces OK")
    print()

    # --- 10. Compare with reference ---
    if ref_path:
        print(f"=== Comparing with reference: {ref_path} ===\n")
        ref = np.load(ref_path, allow_pickle=True)

        for key in sorted(set(list(data.keys()) + list(ref.keys()))):
            in_new = key in data
            in_ref = key in ref
            if not in_ref:
                print(f"  [{key}] only in new file")
                continue
            if not in_new:
                print(f"  [{key}] only in reference")
                continue

            arr_new = data[key]
            arr_ref = ref[key]

            if arr_new.shape != arr_ref.shape:
                print(f"  [{key}] SHAPE MISMATCH: new={arr_new.shape} vs ref={arr_ref.shape}")
                continue

            if arr_new.dtype.kind == 'f':
                if np.allclose(arr_new, arr_ref, rtol=1e-5, atol=1e-8):
                    print(f"  [{key}] MATCH (float, shape={arr_new.shape})")
                else:
                    diff = np.abs(arr_new - arr_ref)
                    print(f"  [{key}] DIFFER: max_diff={diff.max():.6e}, mean_diff={diff.mean():.6e}")
            else:
                if np.array_equal(arr_new, arr_ref):
                    print(f"  [{key}] MATCH (exact, shape={arr_new.shape})")
                else:
                    n_diff = np.count_nonzero(arr_new != arr_ref)
                    print(f"  [{key}] DIFFER: {n_diff} voxels differ ({n_diff/arr_new.size*100:.2f}%)")
        print()

    print("=== Verification complete ===")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <npz_file> [reference_npz_file]")
        sys.exit(1)

    npz_path = sys.argv[1]
    ref_path = sys.argv[2] if len(sys.argv) > 2 else None
    verify(npz_path, ref_path)
