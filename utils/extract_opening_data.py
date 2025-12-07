#!/usr/bin/env python3
"""
Script to extract opening data from HemoFlow HDF5 output files.
Reads all necessary opening geometry directly from HDF5 file.

Usage:
    python extract_opening_data.py <hemoFlow_output.h5>
"""

import sys
import numpy as np
import h5py
import json

def load_opening_info_from_hdf5(hf):
    """Load opening metadata directly from HDF5 file."""

    openings = {}

    # Try to find geometry flag dataset
    geometry_flag = None
    for geom_name in ['geometryFlag', 'geometry_flag', 'geometry', 'flag']:
        if geom_name in hf.keys():
            geometry_flag = hf[geom_name][:]
            print(f"Found geometry flag: {geom_name}, shape={geometry_flag.shape}")
            break

    if geometry_flag is None:
        print("ERROR: No geometry flag found in HDF5 file!")
        return None, None, None

    # Read opening metadata arrays from HDF5
    opening_index = None
    opening_radius = None
    opening_center = None
    opening_normal = None

    for idx_name in ['openingIndex', 'opening_index']:
        if idx_name in hf.keys():
            opening_index = hf[idx_name][:]
            break

    for rad_name in ['openingRadius', 'opening_radius']:
        if rad_name in hf.keys():
            opening_radius = hf[rad_name][:]
            break

    for ctr_name in ['openingCenter', 'opening_center']:
        if ctr_name in hf.keys():
            opening_center = hf[ctr_name][:]
            break

    for nrm_name in ['openingNormal', 'opening_normal']:
        if nrm_name in hf.keys():
            opening_normal = hf[nrm_name][:]
            break

    # Get dx from attributes or datasets
    dx = hf.attrs.get('dx', None)
    if dx is None and 'dx' in hf.keys():
        dx_data = hf['dx'][:]
        dx = dx_data[0] if dx_data.size > 0 else None

    if opening_index is None:
        print("ERROR: No opening index array found in HDF5 file!")
        return None, None, None

    print(f"Found opening metadata:")
    print(f"  Opening indices: {len(opening_index)} openings")
    if opening_radius is not None:
        print(f"  Opening radius: Available")
    if opening_center is not None:
        print(f"  Opening center: Available")
    if opening_normal is not None:
        print(f"  Opening normal: Available")
    if dx is not None:
        print(f"  Resolution (dx): {dx:.6e} m")

    # Build opening dictionary
    for i, label in enumerate(opening_index):
        openings[int(label)] = {
            'index': i,
            'label': int(label),
            'radius_m': float(opening_radius[i]) if opening_radius is not None else None,
            'center_voxel': opening_center[i].tolist() if opening_center is not None else None,
            'normal': opening_normal[i].tolist() if opening_normal is not None else None,
        }

        # Find all voxels with this label
        indices = np.argwhere(geometry_flag == label)
        openings[int(label)]['voxel_indices'] = indices
        openings[int(label)]['voxel_count'] = len(indices)

    return openings, dx, geometry_flag

def extract_opening_data(h5_file):
    """Extract velocity and pressure data at opening locations."""

    print("="*70)
    print("HemoFlow Opening Data Extractor")
    print("="*70)
    print(f"HDF5 file: {h5_file}\n")

    # Open HDF5 file
    try:
        hf = h5py.File(h5_file, 'r')
    except Exception as e:
        print(f"ERROR: Could not open HDF5 file: {e}")
        return

    print("HDF5 file structure:")
    def print_structure(name, obj):
        print(f"  {name}: {type(obj)}")
        if isinstance(obj, h5py.Dataset):
            print(f"    shape={obj.shape}, dtype={obj.dtype}")
    hf.visititems(print_structure)
    print()

    # Load opening information from HDF5
    print("Loading opening metadata from HDF5...")
    openings, dx, geometry_flag = load_opening_info_from_hdf5(hf)

    if openings is None:
        print("ERROR: No opening geometry data found in HDF5 file!")
        print("Make sure the HDF5 file was generated with the updated hemoFlow solver")
        print("that includes opening metadata (openingIndex, openingRadius, etc.)")
        hf.close()
        return

    print(f"Found {len(openings)} openings\n")

    # Read velocity and pressure fields
    velocity = None
    pressure = None

    # Try different possible names for velocity field
    for vel_name in ['velocity', 'Velocity', 'u', 'V']:
        if vel_name in hf.keys():
            velocity = hf[vel_name][:]
            print(f"Found velocity field: {vel_name}, shape={velocity.shape}")
            break

    # Try different possible names for pressure field
    for pres_name in ['pressure', 'Pressure', 'p', 'P']:
        if pres_name in hf.keys():
            pressure = hf[pres_name][:]
            print(f"Found pressure field: {pres_name}, shape={pressure.shape}")
            break

    # Read conversion factors if available
    c_l = hf.attrs.get('C_l', None)
    if c_l is None and dx is not None:
        c_l = dx  # Fallback to dx from HDF5
        print(f"  Note: C_l not found in HDF5 attributes, using dx: {c_l}")

    c_t = hf.attrs.get('C_t', None)
    c_p = hf.attrs.get('C_p', None)

    print(f"\nConversion factors:")
    print(f"  C_l (length): {c_l if c_l is not None else 'Not available'}")
    print(f"  C_t (time):   {c_t if c_t is not None else 'Not available'}")
    print(f"  C_p (pressure): {c_p if c_p is not None else 'Not available'}")
    print()

    # Extract data for each opening
    results = {
        'openings': {},
        'metadata': {
            'h5_file': h5_file,
            'c_l': float(c_l) if c_l is not None else None,
            'c_t': float(c_t) if c_t is not None else None,
            'c_p': float(c_p) if c_p is not None else None,
        }
    }

    for label, opening_info in openings.items():
        print("="*70)
        print(f"OPENING: Label={label} (Index {opening_info['index']})")
        print("="*70)

        # Get voxel indices for this opening
        voxel_indices = opening_info.get('voxel_indices')
        if voxel_indices is None or len(voxel_indices) == 0:
            print(f"  WARNING: No voxels found with label {label}")
            print()
            continue

        print(f"  Voxels: {len(voxel_indices)}")
        print(f"  Center (voxel): {opening_info['center_voxel']}")
        print(f"  Radius: {opening_info['radius_m']*1000:.4f} mm")
        print(f"  Normal: {opening_info['normal']}")
        print()

        opening_results = {
            'label': label,
            'voxel_count': len(voxel_indices),
            'center_voxel': opening_info['center_voxel'],
            'radius_m': opening_info['radius_m'],
            'normal': opening_info['normal'],
            'voxel_coordinates': voxel_indices.tolist(),
        }

        # Extract velocity at opening voxels
        if velocity is not None:
            vel_data = []
            for idx in voxel_indices:
                x, y, z = idx
                if velocity.ndim == 4:  # (Nx, Ny, Nz, 3)
                    vel = velocity[x, y, z, :]
                elif velocity.ndim == 5:  # (time, Nx, Ny, Nz, 3) - take last timestep
                    vel = velocity[-1, x, y, z, :]
                else:
                    continue
                vel_data.append(vel)

            vel_data = np.array(vel_data)

            # Convert to SI units
            if c_l is not None and c_t is not None:
                vel_si = vel_data * c_l / c_t
                units_str = "(m/s)"
            else:
                vel_si = vel_data
                units_str = "(LBM units)"
                print("  Warning: Missing conversion factors, using LBM units")

            # Statistics
            vel_magnitude = np.linalg.norm(vel_si, axis=1)

            print(f"  Velocity Statistics {units_str}:")
            print(f"    Mean magnitude: {vel_magnitude.mean():.6f}")
            print(f"    Max magnitude:  {vel_magnitude.max():.6f}")
            print(f"    Min magnitude:  {vel_magnitude.min():.6f}")
            print(f"    Mean components: Vx={vel_si[:,0].mean():.6f}, Vy={vel_si[:,1].mean():.6f}, Vz={vel_si[:,2].mean():.6f}")

            # Flow rate estimate (assuming perpendicular flow)
            if c_l is not None and c_t is not None:
                area = np.pi * opening_info['radius_m']**2
                avg_vel_normal = np.abs(np.dot(vel_si.mean(axis=0), opening_info['normal']))
                flow_rate = avg_vel_normal * area
                print(f"  Estimated flow rate: {flow_rate:.6e} m³/s ({flow_rate*1e6:.4f} mL/s)")
            else:
                print(f"  Flow rate: Cannot calculate (missing conversion factors)")

            opening_results['velocity'] = {
                'mean_magnitude_m_s': float(vel_magnitude.mean()),
                'max_magnitude_m_s': float(vel_magnitude.max()),
                'mean_vx_m_s': float(vel_si[:,0].mean()),
                'mean_vy_m_s': float(vel_si[:,1].mean()),
                'mean_vz_m_s': float(vel_si[:,2].mean()),
                'flow_rate_m3_s': float(flow_rate),
                'flow_rate_ml_s': float(flow_rate*1e6),
            }

        # Extract pressure at opening voxels
        if pressure is not None:
            pres_data = []
            for idx in voxel_indices:
                x, y, z = idx
                if pressure.ndim == 3:  # (Nx, Ny, Nz)
                    p = pressure[x, y, z]
                elif pressure.ndim == 4:  # (time, Nx, Ny, Nz) - take last timestep
                    p = pressure[-1, x, y, z]
                else:
                    continue
                pres_data.append(p)

            pres_data = np.array(pres_data)

            # Convert to SI units (Pa)
            if c_p is not None:
                pres_si = pres_data * c_p
                units_str = "(Pa)"
                mmhg_available = True
            else:
                pres_si = pres_data
                units_str = "(LBM units)"
                mmhg_available = False
                print("\n  Warning: Missing pressure conversion factor, using LBM units")

            print(f"\n  Pressure Statistics {units_str}:")
            print(f"    Mean: {pres_si.mean():.2f}")
            print(f"    Max:  {pres_si.max():.2f}")
            print(f"    Min:  {pres_si.min():.2f}")
            print(f"    Std:  {pres_si.std():.2f}")
            if mmhg_available:
                print(f"    Mean (mmHg): {pres_si.mean()/133.322:.2f}")

            opening_results['pressure'] = {
                'mean_pa': float(pres_si.mean()),
                'max_pa': float(pres_si.max()),
                'min_pa': float(pres_si.min()),
                'std_pa': float(pres_si.std()),
                'mean_mmhg': float(pres_si.mean()/133.322),
            }

        results['openings'][str(label)] = opening_results
        print()

    hf.close()

    # Export results to JSON
    export_file = h5_file.replace('.h5', '_opening_analysis.json')
    with open(export_file, 'w') as f:
        json.dump(results, f, indent=2)

    print("="*70)
    print(f"Results exported to: {export_file}")
    print("="*70)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python extract_opening_data.py <hemoFlow_output.h5>")
        print("\nThis script reads all opening geometry directly from the HDF5 file.")
        print("The HDF5 file must be generated with the updated hemoFlow solver.")
        print("\nExample:")
        print("  python extract_opening_data.py output/output_003360.h5")
        sys.exit(1)

    extract_opening_data(sys.argv[1])
