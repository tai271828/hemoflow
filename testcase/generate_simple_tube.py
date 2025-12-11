#!/usr/bin/env python3
"""
Generate a simple tube geometry for fast hemoFlow testing.
Creates a straight tube with inlet and outlet.
"""

import numpy as np
import sys
import os

# Add preprocessor to path to use cnpy-like saving
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'preprocessor'))

def create_simple_tube(length=40, diameter=10, resolution=0.001):
    """
    Create a simple straight tube geometry.

    Parameters:
    - length: tube length in voxels (default 40)
    - diameter: tube diameter in voxels (default 10)
    - resolution: voxel size in meters (default 0.001m = 1mm)
    """

    radius = diameter // 2

    # Create domain with some padding
    padding = 5
    Nx = length + 2 * padding
    Ny = diameter + 2 * padding
    Nz = diameter + 2 * padding

    print(f"Creating tube geometry:")
    print(f"  Domain size: {Nx} x {Ny} x {Nz}")
    print(f"  Tube length: {length} voxels")
    print(f"  Tube diameter: {diameter} voxels")
    print(f"  Resolution: {resolution} m/voxel")

    # Initialize arrays
    geometryFlag = np.zeros((Nx, Ny, Nz), dtype=np.uint16)
    stent = np.zeros((1,), dtype=np.uint16)  # No stent

    # Center of the tube
    cy = Ny // 2
    cz = Nz // 2

    # Fill the domain
    # 0 = fluid
    # 1 = solid (wall)
    # 10 = inlet
    # 11 = outlet

    # Start with all solid
    geometryFlag[:, :, :] = 1

    # Create tube (hollow cylinder along x-axis)
    for x in range(Nx):
        for y in range(Ny):
            for z in range(Nz):
                dist = np.sqrt((y - cy)**2 + (z - cz)**2)
                if dist <= radius:
                    geometryFlag[x, y, z] = 0  # fluid

    # Mark inlet (first slice inside tube)
    inlet_x = padding
    for y in range(Ny):
        for z in range(Nz):
            dist = np.sqrt((y - cy)**2 + (z - cz)**2)
            if dist <= radius:
                geometryFlag[inlet_x, y, z] = 10  # inlet

    # Mark outlet (last slice inside tube)
    outlet_x = Nx - padding - 1
    for y in range(Ny):
        for z in range(Nz):
            dist = np.sqrt((y - cy)**2 + (z - cz)**2)
            if dist <= radius:
                geometryFlag[outlet_x, y, z] = 11  # outlet

    # Calculate opening properties
    inlet_area = np.sum(geometryFlag[inlet_x, :, :] == 10)
    outlet_area = np.sum(geometryFlag[outlet_x, :, :] == 11)

    inlet_radius = np.sqrt(inlet_area / np.pi)
    outlet_radius = np.sqrt(outlet_area / np.pi)

    print(f"  Inlet area: {inlet_area} voxels, radius: {inlet_radius:.2f} voxels")
    print(f"  Outlet area: {outlet_area} voxels, radius: {outlet_radius:.2f} voxels")

    # Opening data arrays
    openingIndex = np.array([10, 11], dtype=np.uint16)
    openingRadius = np.array([inlet_radius * resolution, outlet_radius * resolution], dtype=np.float64)

    # Normal vectors (inlet points in +x, outlet points in +x)
    openingNormal = np.array([
        [1.0, 0.0, 0.0],  # inlet normal (flow going in +x direction)
        [1.0, 0.0, 0.0]   # outlet normal (flow going out +x direction)
    ], dtype=np.float64)

    # Opening centers
    openingCenter = np.array([
        [inlet_x * resolution, cy * resolution, cz * resolution],
        [outlet_x * resolution, cy * resolution, cz * resolution]
    ], dtype=np.float64)

    dx_array = np.array([resolution], dtype=np.float64)

    # Save to npz file
    output_file = os.path.join(os.path.dirname(__file__), 'input', 'simple_tube.npz')

    # Create input directory if it doesn't exist
    os.makedirs(os.path.dirname(output_file), exist_ok=True)

    np.savez(output_file,
             geometryFlag=geometryFlag,
             stent=stent,
             openingIndex=openingIndex,
             openingRadius=openingRadius,
             openingNormal=openingNormal,
             openingCenter=openingCenter,
             dx=dx_array)

    print(f"\nGeometry saved to: {output_file}")
    print(f"Total voxels: {Nx * Ny * Nz}")
    print(f"Fluid voxels: {np.sum(geometryFlag == 0) + np.sum(geometryFlag == 10) + np.sum(geometryFlag == 11)}")
    print(f"Solid voxels: {np.sum(geometryFlag == 1)}")

if __name__ == "__main__":
    create_simple_tube()
