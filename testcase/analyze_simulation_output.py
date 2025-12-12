#!/usr/bin/env python3
"""
Analyze and compare simulation output (HDF5/XMF) with input geometry (NPZ).
This script helps understand:
- How geometry dimensions map to simulation output
- Where openings are located in the output
- Why dimensions might appear different
- Physical vs lattice coordinates
"""
import numpy as np
import h5py
import xml.etree.ElementTree as ET
import sys
from pathlib import Path

def analyze_npz_geometry(npz_file):
    """Analyze the input NPZ geometry file."""
    print("="*80)
    print("INPUT GEOMETRY ANALYSIS (NPZ)")
    print("="*80)

    data = np.load(npz_file)

    print(f"\nFile: {npz_file}")
    print(f"\nArrays in NPZ file:")
    for key in data.keys():
        arr = data[key]
        if hasattr(arr, 'shape'):
            print(f"  {key:20s}: shape={arr.shape}, dtype={arr.dtype}")
        else:
            print(f"  {key:20s}: {arr}")

    geom = data['geometryFlag']
    dx = data['dx'][0]

    print(f"\n--- Geometry Grid (Lattice/Voxel Space) ---")
    print(f"Grid dimensions (voxels): {geom.shape}")
    print(f"  Nx (x-direction): {geom.shape[0]} voxels")
    print(f"  Ny (y-direction): {geom.shape[1]} voxels")
    print(f"  Nz (z-direction): {geom.shape[2]} voxels")

    print(f"\n--- Physical Dimensions ---")
    print(f"Lattice spacing (dx): {dx*1e3:.4f} mm = {dx*1e6:.1f} μm = {dx} m")
    print(f"Physical size:")
    print(f"  X: {geom.shape[0]} voxels × {dx*1e3:.4f} mm/voxel = {geom.shape[0] * dx * 1e3:.2f} mm = {geom.shape[0] * dx:.6f} m")
    print(f"  Y: {geom.shape[1]} voxels × {dx*1e3:.4f} mm/voxel = {geom.shape[1] * dx * 1e3:.2f} mm = {geom.shape[1] * dx:.6f} m")
    print(f"  Z: {geom.shape[2]} voxels × {dx*1e3:.4f} mm/voxel = {geom.shape[2] * dx * 1e3:.2f} mm = {geom.shape[2] * dx:.6f} m")
    print(f"\n  → 100 voxels in Z-direction = 100 × {dx} m = {100 * dx} m = {100 * dx * 1e3:.2f} mm")

    print(f"\n--- Voxel Label Distribution ---")
    unique, counts = np.unique(geom, return_counts=True)
    label_names = {0: 'unused', 1: 'wall', 2: 'fluid'}
    for val, count in zip(unique, counts):
        if val < 10:
            name = label_names.get(val, 'unknown')
            print(f"  Label {val:3d} ({name:10s}): {count:8d} voxels ({count/geom.size*100:5.2f}%)")
        else:
            print(f"  Label {val:3d} (opening {val-10:2d}): {count:8d} voxels ({count/geom.size*100:5.2f}%)")

    print(f"\n--- Opening Details ---")
    if 'openingIndex' in data:
        openings = data['openingIndex']
        radii = data['openingRadius']
        normals = data['openingNormal']

        for i, label in enumerate(openings):
            coords = np.argwhere(geom == label)
            if len(coords) > 0:
                center_voxel = coords.mean(axis=0)
                center_physical = center_voxel * dx
                min_coords = coords.min(axis=0)
                max_coords = coords.max(axis=0)

                print(f"\nOpening {i} (Label {label}):")
                print(f"  Voxel count: {len(coords)}")
                print(f"  Center (voxel coordinates): [{center_voxel[0]:.1f}, {center_voxel[1]:.1f}, {center_voxel[2]:.1f}]")
                print(f"  Center (SI units): [{center_physical[0]:.6f}, {center_physical[1]:.6f}, {center_physical[2]:.6f}] m")
                print(f"  Center (mm): [{center_physical[0]*1e3:.2f}, {center_physical[1]*1e3:.2f}, {center_physical[2]*1e3:.2f}] mm")
                print(f"  Bounding box (voxels):")
                print(f"    X: {min_coords[0]} to {max_coords[0]} (span: {max_coords[0]-min_coords[0]+1})")
                print(f"    Y: {min_coords[1]} to {max_coords[1]} (span: {max_coords[1]-min_coords[1]+1})")
                print(f"    Z: {min_coords[2]} to {max_coords[2]} (span: {max_coords[2]-min_coords[2]+1})")
                print(f"  Bounding box (SI units, meters):")
                print(f"    X: {min_coords[0]*dx:.6f} to {max_coords[0]*dx:.6f} m")
                print(f"    Y: {min_coords[1]*dx:.6f} to {max_coords[1]*dx:.6f} m")
                print(f"    Z: {min_coords[2]*dx:.6f} to {max_coords[2]*dx:.6f} m")
                print(f"  Radius (from NPZ): {radii[i]:.6f} m = {radii[i]*1e3:.3f} mm")
                print(f"  Normal direction: [{normals[i,0]:.1f}, {normals[i,1]:.1f}, {normals[i,2]:.1f}]")

    return {
        'shape': geom.shape,
        'dx': dx,
        'geometry': geom,
        'physical_size': (geom.shape[0]*dx, geom.shape[1]*dx, geom.shape[2]*dx)
    }


def analyze_hdf5_output(h5_file, npz_info):
    """Analyze the HDF5 simulation output."""
    print("\n" + "="*80)
    print("SIMULATION OUTPUT ANALYSIS (HDF5)")
    print("="*80)

    print(f"\nFile: {h5_file}")

    with h5py.File(h5_file, 'r') as f:
        print(f"\nDatasets in HDF5 file:")
        for key in f.keys():
            dataset = f[key]
            print(f"  {key:20s}: shape={dataset.shape}, dtype={dataset.dtype}")

        # Analyze velocity field to understand dimensions
        if 'velocity_x' in f:
            vel_x = f['velocity_x']
            print(f"\n--- Output Grid Dimensions ---")
            print(f"HDF5 dataset shape: {vel_x.shape}")
            print(f"  Dimension 0 (Z): {vel_x.shape[0]}")
            print(f"  Dimension 1 (Y): {vel_x.shape[1]}")
            print(f"  Dimension 2 (X): {vel_x.shape[2]}")

            print(f"\n--- Comparison with Input Geometry ---")
            npz_shape = npz_info['shape']
            dx = npz_info['dx']

            print(f"NPZ geometry shape: {npz_shape} (Nx, Ny, Nz)")
            print(f"HDF5 output shape:  {vel_x.shape} (Nz, Ny, Nx)")
            print(f"\nNote: HDF5 uses (Z, Y, X) order while NPZ uses (X, Y, Z) order!")

            if vel_x.shape == (npz_shape[2], npz_shape[1], npz_shape[0]):
                print("✓ Dimensions match (accounting for axis reordering)")
            else:
                print("✗ Dimension mismatch!")

            # Load velocity data
            vx_data = vel_x[:]

            print(f"\n--- Velocity Field Statistics ---")
            print(f"Min velocity: {vx_data.min():.6f} m/s")
            print(f"Max velocity: {vx_data.max():.6f} m/s")
            print(f"Mean velocity: {vx_data.mean():.6f} m/s")
            print(f"Std velocity: {vx_data.std():.6f} m/s")

            # Find where flow is non-zero
            flow_mask = np.abs(vx_data) > 1e-6
            print(f"\nVoxels with significant flow: {flow_mask.sum()} / {vx_data.size} ({flow_mask.sum()/vx_data.size*100:.1f}%)")

        # Analyze opening metadata if present
        if 'opening_center_x' in f:
            print(f"\n--- Opening Metadata in HDF5 ---")
            center_x = f['opening_center_x'][:]
            center_y = f['opening_center_y'][:]
            center_z = f['opening_center_z'][:]
            dir_x = f['opening_direction_x'][:]
            dir_y = f['opening_direction_y'][:]
            dir_z = f['opening_direction_z'][:]

            print(f"Number of openings saved: {len(center_x)}")
            for i in range(len(center_x)):
                print(f"\nOpening {i}:")
                print(f"  Center: [{center_x[i]*1e3:.2f}, {center_y[i]*1e3:.2f}, {center_z[i]*1e3:.2f}] mm")
                print(f"  Direction: [{dir_x[i]:.3f}, {dir_y[i]:.3f}, {dir_z[i]:.3f}]")

                # Convert to voxel coordinates
                voxel_center = np.array([center_x[i], center_y[i], center_z[i]]) / npz_info['dx']
                print(f"  Center (voxels): [{voxel_center[0]:.1f}, {voxel_center[1]:.1f}, {voxel_center[2]:.1f}]")
        else:
            print(f"\n⚠ No opening metadata found in HDF5 file!")
            print("  The opening center/direction datasets were not written.")


def analyze_xmf_structure(xmf_file):
    """Analyze the XMF metadata file."""
    print("\n" + "="*80)
    print("XMF METADATA ANALYSIS")
    print("="*80)

    print(f"\nFile: {xmf_file}")

    tree = ET.parse(xmf_file)
    root = tree.getroot()

    # Find topology (grid dimensions)
    topology = root.find('.//Topology')
    if topology is not None:
        print(f"\nTopology Type: {topology.get('Type')}")
        dims = topology.get('Dimensions')
        if dims:
            print(f"Grid Dimensions: {dims}")
            dims_list = [int(x) for x in dims.split()]
            print(f"  Parsed: {dims_list} (Z, Y, X order)")

    # Find geometry (physical coordinates)
    geometry = root.find('.//Geometry')
    if geometry is not None:
        print(f"\nGeometry Type: {geometry.get('Type')}")
        origin_elem = geometry.find('.//DataItem[@Name="Origin"]')
        spacing_elem = geometry.find('.//DataItem[@Name="Spacing"]')

        if origin_elem is not None:
            origin = origin_elem.text.strip()
            print(f"Origin: {origin}")

        if spacing_elem is not None:
            spacing = spacing_elem.text.strip()
            print(f"Spacing: {spacing}")

    # List all attributes
    attributes = root.findall('.//Attribute')
    print(f"\n--- Available Attributes ({len(attributes)}) ---")
    for attr in attributes:
        name = attr.get('Name')
        attr_type = attr.get('AttributeType')
        print(f"  {name:30s} ({attr_type})")


def visualize_slice(npz_info, h5_file, slice_axis='z', slice_idx=None):
    """Create a visual comparison of geometry and velocity."""
    print("\n" + "="*80)
    print("SLICE VISUALIZATION")
    print("="*80)

    geom = npz_info['geometry']

    # Choose middle slice if not specified
    if slice_idx is None:
        if slice_axis == 'x':
            slice_idx = geom.shape[0] // 2
        elif slice_axis == 'y':
            slice_idx = geom.shape[1] // 2
        else:  # z
            slice_idx = geom.shape[2] // 2

    print(f"\nExtracting slice: {slice_axis}={slice_idx}")

    # Extract geometry slice
    if slice_axis == 'x':
        geom_slice = geom[slice_idx, :, :]
    elif slice_axis == 'y':
        geom_slice = geom[:, slice_idx, :]
    else:  # z
        geom_slice = geom[:, :, slice_idx]

    print(f"Geometry slice shape: {geom_slice.shape}")
    print(f"\nLabels in this slice:")
    unique, counts = np.unique(geom_slice, return_counts=True)
    for val, count in zip(unique, counts):
        print(f"  Label {val}: {count} voxels")

    # Try to load velocity slice
    try:
        with h5py.File(h5_file, 'r') as f:
            if 'velocity_x' in f:
                vel = f['velocity_x'][:]
                # Remember: HDF5 is (Z, Y, X) while geom is (X, Y, Z)
                if slice_axis == 'x':
                    # X slice: need to slice from last dimension of HDF5
                    vel_slice = vel[:, :, slice_idx].T
                elif slice_axis == 'y':
                    vel_slice = vel[:, slice_idx, :].T
                else:  # z
                    # Z slice: from first dimension of HDF5
                    vel_slice = vel[slice_idx, :, :].T

                print(f"Velocity slice shape: {vel_slice.shape}")
                print(f"Velocity range: [{vel_slice.min():.6f}, {vel_slice.max():.6f}] m/s")

                # Check if shapes match
                if geom_slice.shape == vel_slice.shape:
                    print("✓ Slice shapes match")
                else:
                    print(f"✗ Shape mismatch! Geom: {geom_slice.shape}, Vel: {vel_slice.shape}")

    except Exception as e:
        print(f"Could not load velocity data: {e}")


def main():
    """Main analysis routine."""
    # File paths
    npz_file = "input/simple_tube_small.npz"
    output_dir = Path("/home/tai/work-my-projects/workspace-hemoflow/workspace-result-visualization/archive/testcase-mpi-67634-251212-145526/output_simple_tube")

    # Find first HDF5 file
    h5_files = sorted(output_dir.glob("output_*.h5"))
    if not h5_files:
        print("No HDF5 files found!")
        return

    h5_file = h5_files[0]  # Use first output
    xmf_file = h5_file.with_suffix('.xmf')

    print("Simple Tube Simulation Analysis")
    print("="*80)
    print(f"\nInput geometry: {npz_file}")
    print(f"Output file: {h5_file}")
    print(f"XMF file: {xmf_file}")

    # Run analyses
    npz_info = analyze_npz_geometry(npz_file)
    analyze_hdf5_output(h5_file, npz_info)

    if xmf_file.exists():
        analyze_xmf_structure(xmf_file)

    # Visualize slices
    visualize_slice(npz_info, h5_file, slice_axis='z', slice_idx=0)  # Inlet
    visualize_slice(npz_info, h5_file, slice_axis='z', slice_idx=50)  # Middle (side opening)
    visualize_slice(npz_info, h5_file, slice_axis='z', slice_idx=99)  # Outlet

    print("\n" + "="*80)
    print("KEY FINDINGS & EXPLANATIONS")
    print("="*80)
    print("""
1. DIMENSION ORDER:
   - NPZ geometry uses: (X, Y, Z) = (30, 30, 100)
   - HDF5 output uses: (Z, Y, X) = (100, 30, 30)
   - This is why the "length" appears in different positions!

2. TUBE LENGTH:
   - The tube IS 100 voxels long in the Z-direction
   - In NPZ: it's the 3rd dimension [2]
   - In HDF5: it's the 1st dimension [0]

3. SIDE OPENING LOCATION:
   - Located at z=50 (middle of tube)
   - To see it in HDF5, look at slice index 50 in the first dimension
   - Or search for label 12 in the geometry array

4. OPENING METADATA:
   - Check if 'opening_center_x/y/z' datasets exist in HDF5
   - These contain the physical coordinates (in meters) of each opening
   - Convert to voxels by dividing by dx (lattice spacing)
""")


if __name__ == "__main__":
    main()
