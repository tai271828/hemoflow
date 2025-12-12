#!/usr/bin/env python3
"""
Create a simple tube geometry with 3 openings for testing.
This creates a small, fast-to-simulate geometry.
"""
import numpy as np

# Tube dimensions (in voxels)
length = 1000  # z-direction
radius = 10   # tube radius

# Grid dimensions
Nx = 2 * radius + 10  # x: add padding
Ny = 2 * radius + 10  # y: add padding
Nz = length           # z: tube length

# Center of the tube in x-y plane
cx = Nx // 2
cy = Ny // 2

# Initialize geometry array (0 = unused, 1 = wall, 2 = fluid)
geom = np.zeros((Nx, Ny, Nz), dtype=np.uint16)

# Create the tube
for z in range(Nz):
    for x in range(Nx):
        for y in range(Ny):
            r = np.sqrt((x - cx)**2 + (y - cy)**2)

            if r <= radius:
                geom[x, y, z] = 2  # fluid
            elif r <= radius + 2:  # 2-voxel wall thickness
                geom[x, y, z] = 1  # wall

# Define openings
# Opening 0 (inlet): z = 0
# Opening 1 (outlet 1): z = Nz-1
# Opening 2 (outlet 2): z = Nz//2 (side opening)

# Opening 0: inlet at z=0
for x in range(Nx):
    for y in range(Ny):
        r = np.sqrt((x - cx)**2 + (y - cy)**2)
        if r <= radius and geom[x, y, 0] == 2:
            geom[x, y, 0] = 10  # label 10

# Opening 1: outlet at z=Nz-1
for x in range(Nx):
    for y in range(Ny):
        r = np.sqrt((x - cx)**2 + (y - cy)**2)
        if r <= radius and geom[x, y, Nz-1] == 2:
            geom[x, y, Nz-1] = 11  # label 11

# Opening 2: side outlet at z=Nz//2, y side (small opening)
z_mid = Nz // 2
side_opening_size = 5  # small opening
for dz in range(-side_opening_size, side_opening_size+1):
    for dx in range(-side_opening_size, side_opening_size+1):
        x = cx + dx
        y = cy + radius  # at the edge
        z = z_mid + dz
        if 0 <= x < Nx and 0 <= y < Ny and 0 <= z < Nz:
            if geom[x, y, z] == 1 or geom[x, y, z] == 2:
                geom[x, y, z] = 12  # label 12

# Calculate lattice spacing (physical dimensions)
# Assume tube diameter is ~5mm, radius is 2.5mm
physical_radius = 2.5e-3  # 2.5 mm in meters
dx = physical_radius / radius  # lattice spacing in meters

print(f"Geometry created:")
print(f"  Grid size: {Nx} x {Ny} x {Nz}")
print(f"  Total voxels: {Nx * Ny * Nz}")
print(f"  Fluid voxels: {np.sum(geom == 2)}")
print(f"  Wall voxels: {np.sum(geom == 1)}")
print(f"  Opening 0 (inlet): {np.sum(geom == 10)} voxels")
print(f"  Opening 1 (outlet): {np.sum(geom == 11)} voxels")
print(f"  Opening 2 (side): {np.sum(geom == 12)} voxels")
print(f"  Lattice spacing dx: {dx*1e3:.4f} mm")

# Prepare opening data
opening_labels = [10, 11, 12]
openingIndex = np.array(opening_labels, dtype=np.int16)
openingRadius = []
openingNormal = []

for label in opening_labels:
    coords = np.argwhere(geom == label)
    if len(coords) > 0:
        # Calculate radius (approximate from number of voxels)
        area = len(coords) * dx * dx
        radius_physical = np.sqrt(area / np.pi)
        openingRadius.append(radius_physical)

        # Calculate normal direction
        if label == 10:  # inlet at z=0
            normal = np.array([0.0, 0.0, -1.0])
        elif label == 11:  # outlet at z=Nz-1
            normal = np.array([0.0, 0.0, 1.0])
        else:  # side outlet
            normal = np.array([0.0, 1.0, 0.0])

        openingNormal.append(normal)

openingRadius = np.array(openingRadius, dtype=np.float64)
openingNormal = np.array(openingNormal, dtype=np.float64)

# Create stent array (no stent in this simple geometry)
stent = np.zeros_like(geom, dtype=np.int16)

# Save to NPZ file with correct format (uncompressed for compatibility)
output_file = "input/simple_tube_small.npz"
np.savez(output_file,  # Use savez instead of savez_compressed
                   geometryFlag=geom.astype(np.int16),
                   dx=np.array([dx], dtype=np.float64),
                   openingIndex=openingIndex,
                   openingRadius=openingRadius,
                   openingNormal=openingNormal,
                   stent=stent)

print(f"\nSaved to: {output_file}")

# Print opening information
print(f"\nOpening information:")
for i, label in enumerate(opening_labels):
    print(f"  Label {label}: radius={openingRadius[i]*1e3:.3f}mm, normal={openingNormal[i]}")