#!/usr/bin/env python3
"""
Compare opening information between preprocessor NPZ and solver HDF5 output.
This verifies that the opening data flows correctly through the pipeline.

Usage:
    python compare_openings.py <preprocessor_openings.json> <solver_opening_analysis.json>
"""

import sys
import json
import numpy as np

def compare_openings(npz_json_file, h5_json_file):
    """Compare opening metadata and simulation results."""

    print("="*70)
    print("Opening Data Comparison: Preprocessor → Solver")
    print("="*70)
    print()

    # Load JSON files
    with open(npz_json_file, 'r') as f:
        npz_data = json.load(f)

    with open(h5_json_file, 'r') as f:
        h5_data = json.load(f)

    print(f"Preprocessor data: {npz_json_file}")
    print(f"Solver data:       {h5_json_file}")
    print()

    # Compare number of openings
    num_npz = npz_data['num_openings']
    num_h5 = len(h5_data['openings'])

    print(f"Number of openings:")
    print(f"  Preprocessor (NPZ): {num_npz}")
    print(f"  Solver (HDF5):      {num_h5}")

    if num_npz != num_h5:
        print("  ⚠️  WARNING: Mismatch in number of openings!")
    else:
        print("  ✓ Match")
    print()

    # Compare resolution
    dx_npz = npz_data.get('dx_meters')
    c_l = h5_data['metadata'].get('C_l')

    print(f"Resolution:")
    if dx_npz is not None:
        print(f"  Preprocessor dx: {dx_npz:.6e} m")
    else:
        print(f"  Preprocessor dx: Not available")

    if c_l is not None:
        print(f"  Solver C_l:      {c_l:.6e} m")
    else:
        print(f"  Solver C_l:      Not available")

    if dx_npz is not None and c_l is not None:
        if abs(dx_npz - c_l) < 1e-10:
            print("  ✓ Match")
        else:
            print(f"  ⚠️  Difference: {abs(dx_npz - c_l):.6e} m")
    elif dx_npz is None and c_l is None:
        print("  ⚠️  Neither resolution available")
    else:
        print("  ⚠️  Resolution available only in one file")
    print()

    # Compare each opening
    print("="*70)
    print("OPENING-BY-OPENING COMPARISON")
    print("="*70)
    print()

    comparison_summary = []

    for npz_opening in npz_data['openings']:
        label = npz_opening['label']
        print(f"{'='*70}")
        print(f"Opening Label {label}")
        print(f"{'='*70}")

        # Find corresponding opening in H5 data
        h5_opening = h5_data['openings'].get(str(label))

        if h5_opening is None:
            print(f"  ❌ ERROR: Label {label} found in NPZ but NOT in HDF5 output!")
            print()
            continue

        # Compare radius
        radius_npz = npz_opening['radius_m']
        radius_h5 = h5_opening['radius_m']

        print(f"Radius:")
        print(f"  NPZ: {radius_npz*1000:.4f} mm")
        print(f"  H5:  {radius_h5*1000:.4f} mm")
        if abs(radius_npz - radius_h5) < 1e-6:
            print("  ✓ Match")
        else:
            print(f"  ⚠️  Difference: {abs(radius_npz - radius_h5)*1000:.4f} mm")

        # Compare center
        center_npz = npz_opening.get('center_voxel')
        center_h5 = h5_opening.get('center_voxel')
        center_diff = None

        if center_npz is not None and center_h5 is not None:
            center_npz = np.array(center_npz) if not isinstance(center_npz, np.ndarray) else center_npz
            center_h5 = np.array(center_h5) if not isinstance(center_h5, np.ndarray) else center_h5

            # Ensure arrays are at least 1D
            if center_npz.ndim == 0:
                center_npz = np.array([center_npz])
            if center_h5.ndim == 0:
                center_h5 = np.array([center_h5])

            print(f"\nCenter (voxel coordinates):")
            if len(center_npz) >= 3 and len(center_h5) >= 3:
                print(f"  NPZ: [{center_npz[0]:.2f}, {center_npz[1]:.2f}, {center_npz[2]:.2f}]")
                print(f"  H5:  [{center_h5[0]:.2f}, {center_h5[1]:.2f}, {center_h5[2]:.2f}]")
                center_diff = np.linalg.norm(center_npz - center_h5)
                if center_diff < 0.1:
                    print("  ✓ Match")
                else:
                    print(f"  ⚠️  Distance: {center_diff:.4f} voxels")
            else:
                print(f"  NPZ: {center_npz}")
                print(f"  H5:  {center_h5}")
                print("  ⚠️  Invalid center coordinate format")
        else:
            print(f"\nCenter: Not available in one or both files")

        # Compare normal
        normal_npz = npz_opening.get('normal')
        normal_h5 = h5_opening.get('normal')
        normal_diff = None

        if normal_npz is not None and normal_h5 is not None:
            normal_npz = np.array(normal_npz) if not isinstance(normal_npz, np.ndarray) else normal_npz
            normal_h5 = np.array(normal_h5) if not isinstance(normal_h5, np.ndarray) else normal_h5

            # Ensure arrays are at least 1D
            if normal_npz.ndim == 0:
                normal_npz = np.array([normal_npz])
            if normal_h5.ndim == 0:
                normal_h5 = np.array([normal_h5])

            print(f"\nNormal vector:")
            if len(normal_npz) >= 3 and len(normal_h5) >= 3:
                print(f"  NPZ: [{normal_npz[0]:.4f}, {normal_npz[1]:.4f}, {normal_npz[2]:.4f}]")
                print(f"  H5:  [{normal_h5[0]:.4f}, {normal_h5[1]:.4f}, {normal_h5[2]:.4f}]")
                normal_diff = np.linalg.norm(normal_npz - normal_h5)
                if normal_diff < 0.01:
                    print("  ✓ Match")
                else:
                    print(f"  ⚠️  Difference: {normal_diff:.4f}")
            else:
                print(f"  NPZ: {normal_npz}")
                print(f"  H5:  {normal_h5}")
                print("  ⚠️  Invalid normal vector format")
        else:
            print(f"\nNormal: Not available in one or both files")

        # Compare voxel coordinates
        voxel_coords_npz = npz_opening.get('voxel_coordinates')
        voxel_coords_h5 = h5_opening.get('voxel_coordinates')
        coords_match = False

        if voxel_coords_npz is not None and voxel_coords_h5 is not None:
            coords_npz_set = set(map(tuple, voxel_coords_npz))
            coords_h5_set = set(map(tuple, voxel_coords_h5))

            print(f"\nVoxel coordinates:")
            print(f"  NPZ: {len(voxel_coords_npz)} coordinates")
            print(f"  H5:  {len(voxel_coords_h5)} coordinates")

            if coords_npz_set == coords_h5_set:
                print("  ✓ All coordinates match")
                coords_match = True
            else:
                only_npz = coords_npz_set - coords_h5_set
                only_h5 = coords_h5_set - coords_npz_set
                common = coords_npz_set & coords_h5_set

                print(f"  ⚠️  Coordinate mismatch:")
                print(f"    Common: {len(common)}")
                print(f"    Only in NPZ: {len(only_npz)}")
                print(f"    Only in H5: {len(only_h5)}")
                if len(only_npz) > 0 and len(only_npz) <= 5:
                    print(f"    Example NPZ-only: {list(only_npz)[:5]}")
                if len(only_h5) > 0 and len(only_h5) <= 5:
                    print(f"    Example H5-only: {list(only_h5)[:5]}")
        else:
            print(f"\nVoxel coordinates: Not available in one or both files")
            coords_match = None

        # Compare voxel count
        voxel_count_npz = npz_opening.get('voxel_count', 0)
        voxel_count_h5 = h5_opening.get('voxel_count', 0)

        print(f"\nVoxel count:")
        print(f"  NPZ: {voxel_count_npz}")
        print(f"  H5:  {voxel_count_h5}")
        if voxel_count_npz == voxel_count_h5:
            print("  ✓ Match")
        else:
            print(f"  ⚠️  Difference: {abs(voxel_count_npz - voxel_count_h5)} voxels")

        # Display simulation results
        if 'velocity' in h5_opening:
            vel = h5_opening['velocity']
            print(f"\nSimulation Results:")
            print(f"  Velocity (mean): {vel['mean_magnitude_m_s']:.6f} m/s")
            print(f"  Flow rate: {vel['flow_rate_ml_s']:.4f} mL/s")

        if 'pressure' in h5_opening:
            pres = h5_opening['pressure']
            print(f"  Pressure (mean): {pres['mean_pa']:.2f} Pa ({pres['mean_mmhg']:.2f} mmHg)")

        # Summary
        match_status = "✓ MATCH" if (
            abs(radius_npz - radius_h5) < 1e-6 and
            (center_diff is None or center_diff < 0.1) and
            (normal_diff is None or normal_diff < 0.01) and
            voxel_count_npz == voxel_count_h5 and
            (coords_match is None or coords_match)
        ) else "⚠️ MISMATCH"

        comparison_summary.append({
            'label': label,
            'status': match_status,
            'radius_match': abs(radius_npz - radius_h5) < 1e-6,
            'center_match': center_diff is None or center_diff < 0.1,
            'normal_match': normal_diff is None or normal_diff < 0.01,
            'voxel_match': voxel_count_npz == voxel_count_h5,
            'coords_match': coords_match if coords_match is not None else True,
        })

        print(f"\n{match_status}")
        print()

    # Final summary
    print("="*70)
    print("SUMMARY")
    print("="*70)

    total_openings = len(comparison_summary)
    matched_openings = sum(1 for s in comparison_summary if s['status'] == "✓ MATCH")

    print(f"Total openings compared: {total_openings}")
    print(f"Fully matched:  {matched_openings}/{total_openings}")
    print(f"With issues:    {total_openings - matched_openings}/{total_openings}")
    print()

    if matched_openings == total_openings:
        print("✓ SUCCESS: All opening information matches between preprocessor and solver!")
    else:
        print("⚠️  WARNING: Some openings have mismatches. Review details above.")

    print()

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python compare_openings.py <preprocessor_openings.json> <solver_opening_analysis.json>")
        print("\nExample:")
        print("  # First, generate the JSON files:")
        print("  python inspect_npz_openings.py input/vox_NAP180_ane_PED_5x20_5M_c.npz")
        print("  python extract_opening_data.py output/hemoFlow_0.h5 input/vox_NAP180_ane_PED_5x20_5M_c.npz")
        print()
        print("  # Then compare:")
        print("  python compare_openings.py input/vox_NAP180_ane_PED_5x20_5M_c_openings.json \\")
        print("                              output/hemoFlow_0_opening_analysis.json")
        sys.exit(1)

    compare_openings(sys.argv[1], sys.argv[2])
