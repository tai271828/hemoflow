#!/usr/bin/env python3
"""
Script to inspect opening information from NPZ geometry files.
This helps verify what opening metadata is stored in the preprocessor output.

Usage:
    python inspect_npz_openings.py <geometry_file.npz>
"""

import sys
import numpy as np
import json

def inspect_npz_openings(npz_file):
    """Load and display all opening information from NPZ file."""

    print("="*70)
    print("NPZ Geometry File Opening Inspector")
    print("="*70)
    print(f"File: {npz_file}\n")

    # Load NPZ file
    try:
        data = np.load(npz_file)
    except Exception as e:
        print(f"ERROR: Could not load NPZ file: {e}")
        return

    print("Available arrays in NPZ:")
    for key in data.files:
        array = data[key]
        print(f"  - {key}: shape={array.shape}, dtype={array.dtype}")
    print()

    # Extract opening information
    if 'openingIndex' not in data.files:
        print("ERROR: No 'openingIndex' array found in NPZ file!")
        return

    opening_index = data['openingIndex']
    opening_radius = data['openingRadius'] if 'openingRadius' in data.files else None
    opening_center = data['openingCenter'] if 'openingCenter' in data.files else None
    opening_normal = data['openingNormal'] if 'openingNormal' in data.files else None
    opening_qratio = data['openingNormalizedQRatio'] if 'openingNormalizedQRatio' in data.files else None
    geometry_flag = data['geometryFlag'] if 'geometryFlag' in data.files else None
    dx = data['dx'][0] if 'dx' in data.files else None

    num_openings = len(opening_index)

    print("="*70)
    print(f"NUMBER OF OPENINGS: {num_openings}")
    print("="*70)
    print()

    if dx is not None:
        print(f"Resolution (dx): {dx:.6e} meters ({dx*1000:.4f} mm)\n")

    # Display detailed information for each opening
    for i in range(num_openings):
        label = opening_index[i]

        print(f"{'='*70}")
        print(f"OPENING {i}: Label = {label}")
        print(f"{'='*70}")

        # Radius
        if opening_radius is not None:
            radius_m = opening_radius[i]
            radius_mm = radius_m * 1000
            diameter_mm = radius_mm * 2
            print(f"  Radius:    {radius_m:.6e} m ({radius_mm:.4f} mm)")
            print(f"  Diameter:  {diameter_mm:.4f} mm")

        # Center coordinates (in voxel space)
        if opening_center is not None:
            center = opening_center[i]
            print(f"  Center (voxel):  [{center[0]:.2f}, {center[1]:.2f}, {center[2]:.2f}]")
            if dx is not None:
                center_phys = center * dx
                print(f"  Center (meters): [{center_phys[0]:.6f}, {center_phys[1]:.6f}, {center_phys[2]:.6f}]")

        # Normal direction
        if opening_normal is not None:
            normal = opening_normal[i]
            normal_mag = np.linalg.norm(normal)
            print(f"  Normal:    [{normal[0]:.4f}, {normal[1]:.4f}, {normal[2]:.4f}]")
            print(f"  Normal magnitude: {normal_mag:.4f}")

        # Murray ratio
        if opening_qratio is not None:
            qratio = opening_qratio[i]
            print(f"  Murray Q-ratio: {qratio:.6f} ({qratio*100:.2f}%)")

        # Count voxels with this label
        if geometry_flag is not None:
            voxel_count = np.count_nonzero(geometry_flag == label)
            print(f"  Voxels with label {label}: {voxel_count}")

            if voxel_count > 0:
                # Find voxel coordinate range
                indices = np.argwhere(geometry_flag == label)
                min_coords = indices.min(axis=0)
                max_coords = indices.max(axis=0)
                print(f"  Voxel bounding box:")
                print(f"    X: [{min_coords[0]}, {max_coords[0]}]")
                print(f"    Y: [{min_coords[1]}, {max_coords[1]}]")
                print(f"    Z: [{min_coords[2]}, {max_coords[2]}]")

        print()

    # Summary statistics
    print("="*70)
    print("SUMMARY")
    print("="*70)

    if geometry_flag is not None:
        total_voxels = geometry_flag.size
        domain_shape = geometry_flag.shape
        fluid_voxels = np.count_nonzero(geometry_flag == 2)
        wall_voxels = np.count_nonzero(geometry_flag == 1)
        outside_voxels = np.count_nonzero(geometry_flag == 0)

        print(f"Domain shape: {domain_shape[0]} x {domain_shape[1]} x {domain_shape[2]}")
        print(f"Total voxels: {total_voxels:,}")
        print(f"  Outside (0): {outside_voxels:,} ({100*outside_voxels/total_voxels:.1f}%)")
        print(f"  Walls (1):   {wall_voxels:,} ({100*wall_voxels/total_voxels:.1f}%)")
        print(f"  Fluid (2):   {fluid_voxels:,} ({100*fluid_voxels/total_voxels:.1f}%)")

        # Opening voxels
        opening_voxels = 0
        for label in opening_index:
            opening_voxels += np.count_nonzero(geometry_flag == label)
        print(f"  Openings (10+): {opening_voxels:,} ({100*opening_voxels/total_voxels:.1f}%)")

    if opening_radius is not None:
        total_area = sum([np.pi * r**2 for r in opening_radius])
        print(f"\nTotal opening area: {total_area:.6e} m²")
        print("Opening radius distribution:")
        print(f"  Min: {opening_radius.min()*1000:.4f} mm")
        print(f"  Max: {opening_radius.max()*1000:.4f} mm")
        print(f"  Mean: {opening_radius.mean()*1000:.4f} mm")

    print()

    # Export to JSON for easy parsing
    export_file = npz_file.replace('.npz', '_openings.json')
    opening_data = {
        'num_openings': int(num_openings),
        'dx_meters': float(dx) if dx is not None else None,
        'openings': []
    }

    for i in range(num_openings):
        opening_info = {
            'index': i,
            'label': int(opening_index[i]),
            'radius_m': float(opening_radius[i]) if opening_radius is not None else None,
            'radius_mm': float(opening_radius[i]*1000) if opening_radius is not None else None,
            'center_voxel': opening_center[i].tolist() if opening_center is not None else None,
            'normal': opening_normal[i].tolist() if opening_normal is not None else None,
            'murray_qratio': float(opening_qratio[i]) if opening_qratio is not None else None,
        }

        if geometry_flag is not None:
            label = opening_index[i]
            voxel_count = int(np.count_nonzero(geometry_flag == label))
            opening_info['voxel_count'] = voxel_count

            # Extract all voxel coordinates for this opening
            if voxel_count > 0:
                voxel_coords = np.argwhere(geometry_flag == label)
                opening_info['voxel_coordinates'] = voxel_coords.tolist()
            else:
                opening_info['voxel_coordinates'] = []

        opening_data['openings'].append(opening_info)

    with open(export_file, 'w') as f:
        json.dump(opening_data, f, indent=2)

    print(f"Opening data exported to: {export_file}")
    print()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python inspect_npz_openings.py <geometry_file.npz>")
        sys.exit(1)

    inspect_npz_openings(sys.argv[1])
