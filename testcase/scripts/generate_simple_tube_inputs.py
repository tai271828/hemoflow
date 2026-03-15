#!/usr/bin/env python3
"""
Generate geometry_original_stl and centerline_vtp for the SimpleTube test case.

Reverse-engineered from testcase/input/simple_tube_straight.npz which was
created by scripts/create_simple_tube.py (voxel-space construction).

Reference geometry (voxel space, dx=0.25mm):
  - Straight cylinder: radius=10 vox, length=300 vox, 2-voxel walls
  - Grid: 30 x 30 x 300
  - Side opening: circular R=5 vox, at Z=150 (midpoint), Y=max side
  - Openings:
      10 (inlet,  317 vox): Z=0   face, R_phys=2.511mm, normal=[0,0,1]
      11 (outlet, 317 vox): Z=299 face, R_phys=2.511mm, normal=[0,0,1]
      12 (side,    81 vox): Y=27,       R_phys=1.269mm, normal=[0,1,0]

Physical dimensions (mm):
  dx = 0.25 mm/voxel
  Main tube: R=2.5mm, L=75mm along Z, centered at (cx,cy)=(3.75, 3.75)
  Side opening: R≈1.25mm, punched through the wall at Y=max, Z=37.5mm

This script creates:
  - STL: surface mesh of the lumen (main cylinder only — the side opening
    is a wall cutout, not a separate tube in the original geometry)
  - VTP: VMTK-style centerline with MaximumInscribedSphereRadius
  - config.json: preprocessor configuration

Usage:
  python generate_simple_tube_inputs.py [--output-dir DIR]

Dependencies: trimesh, vtk, numpy
"""

import argparse
import json
import os

import numpy as np
import trimesh
import vtk

# ---------------------------------------------------------------------------
# Geometry parameters (all in mm) — derived from the voxel construction
# ---------------------------------------------------------------------------
DX = 0.25                            # mm per voxel
RADIUS_VOX = 10                      # main tube radius in voxels
LENGTH_VOX = 300                     # tube length in voxels
SIDE_R_VOX = 5                       # side opening radius in voxels

R_MAIN = RADIUS_VOX * DX             # 2.5 mm
L_MAIN = LENGTH_VOX * DX             # 75.0 mm
R_SIDE = SIDE_R_VOX * DX             # 1.25 mm
Z_SIDE = L_MAIN / 2                  # 37.5 mm — side opening Z position

# The side opening is a hole punched through the 2-voxel wall.
# In the STL we just need the main cylinder; the side opening exists
# only as a wall feature in the voxel domain. For the preprocessor's
# centerline matching we still record it in the VTP.

# Physical center of tube in XY (grid is 30x30, center at 15,15 voxels)
CX = 15 * DX  # 3.75 mm
CY = 15 * DX  # 3.75 mm

# Centerline sampling
CL_STEP = 0.5  # mm between centerline points


# ---------------------------------------------------------------------------
# STL generation — simple capped cylinder (main tube lumen)
# ---------------------------------------------------------------------------
def create_stl(output_path: str) -> None:
    """
    Create watertight STL of the main tube lumen.

    The original geometry is a straight cylinder with a small side opening
    punched through the wall. The STL represents the inner lumen surface
    (a simple capped cylinder). The side opening is a voxel-level feature
    that doesn't need to appear in the STL.
    """
    cyl = trimesh.creation.cylinder(
        radius=R_MAIN,
        height=L_MAIN,
        sections=128,
    )
    # trimesh cylinder is centered at origin along Z; shift to Z=[0, L_MAIN]
    cyl.apply_translation([0, 0, L_MAIN / 2])

    assert cyl.is_watertight, "Cylinder mesh is not watertight!"

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    cyl.export(output_path)

    print(f"STL  -> {output_path}")
    print(f"       vertices={len(cyl.vertices)}  faces={len(cyl.faces)}")
    bb = cyl.bounds
    print(f"       bbox  min={bb[0]}  max={bb[1]}")


# ---------------------------------------------------------------------------
# VTP centerline generation
# ---------------------------------------------------------------------------
def create_vtp(output_path: str) -> None:
    """
    Create VMTK-style VTP centerline with MaximumInscribedSphereRadius.

    Two lines:
      Line 0: inlet (Z=0) -> outlet (Z=L_MAIN)    — main tube, R=2.5mm
      Line 1: inlet (Z=0) -> side opening (Y=max)  — path to side opening
    """
    points = vtk.vtkPoints()
    radii = vtk.vtkFloatArray()
    radii.SetName("MaximumInscribedSphereRadius")
    radii.SetNumberOfComponents(1)

    # --- Line 0: inlet (Z=0) -> outlet (Z=L_MAIN), along tube axis ---
    n_main = int(L_MAIN / CL_STEP) + 1
    line0_ids = []
    for i in range(n_main):
        z = min(i * CL_STEP, L_MAIN)
        pid = points.InsertNextPoint(0.0, 0.0, z)
        radii.InsertNextTuple1(R_MAIN)
        line0_ids.append(pid)

    # --- Line 1: inlet (Z=0) -> side opening ---
    # Path: along Z to midpoint, then along +Y to the wall surface
    n_shared = int(Z_SIDE / CL_STEP) + 1
    line1_ids = []
    for i in range(n_shared):
        z = min(i * CL_STEP, Z_SIDE)
        pid = points.InsertNextPoint(0.0, 0.0, z)
        radii.InsertNextTuple1(R_MAIN)
        line1_ids.append(pid)

    # Branch along +Y from center to wall surface
    # The side opening center is at y = R_MAIN + wall_thickness*DX
    # wall_thickness = 2 voxels = 0.5mm
    y_end = R_MAIN + 2 * DX  # 2.5 + 0.5 = 3.0 mm
    n_branch = int(y_end / CL_STEP) + 1
    for i in range(1, n_branch + 1):
        y = min(i * CL_STEP, y_end)
        pid = points.InsertNextPoint(0.0, y, Z_SIDE)
        # Radius transitions from R_MAIN to R_SIDE
        t = y / y_end
        r = R_MAIN * (1.0 - t) + R_SIDE * t
        radii.InsertNextTuple1(r)
        line1_ids.append(pid)

    # --- Assemble VTK PolyData ---
    polydata = vtk.vtkPolyData()
    polydata.SetPoints(points)
    polydata.GetPointData().AddArray(radii)

    lines = vtk.vtkCellArray()

    line0 = vtk.vtkPolyLine()
    line0.GetPointIds().SetNumberOfIds(len(line0_ids))
    for i, pid in enumerate(line0_ids):
        line0.GetPointIds().SetId(i, pid)
    lines.InsertNextCell(line0)

    line1 = vtk.vtkPolyLine()
    line1.GetPointIds().SetNumberOfIds(len(line1_ids))
    for i, pid in enumerate(line1_ids):
        line1.GetPointIds().SetId(i, pid)
    lines.InsertNextCell(line1)

    polydata.SetLines(lines)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    writer = vtk.vtkXMLPolyDataWriter()
    writer.SetFileName(output_path)
    writer.SetInputData(polydata)
    writer.Write()

    print(f"VTP  -> {output_path}")
    print(f"       points={points.GetNumberOfPoints()}  lines={lines.GetNumberOfCells()}")
    print(f"       Line 0: {len(line0_ids)} pts  (inlet Z=0 -> outlet Z={L_MAIN})")
    print(f"       Line 1: {len(line1_ids)} pts  (inlet Z=0 -> side Y={y_end})")


# ---------------------------------------------------------------------------
# Config JSON generation
# ---------------------------------------------------------------------------
def create_config(output_path: str) -> None:
    config = {
        "geometry_original_stl": "input/simpletube.stl",
        "centerline_vtp": "input/simpletube.vtp",
        "stent_mesh_base": "",
        "output_base_name": "output/dev_simpletube_",
        "target_elements": "5000000",
        "cutWidth": "1",
        "distance": "4",
    }
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(config, f, indent=4)
    print(f"JSON -> {output_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Generate STL, VTP, and config for the SimpleTube test case."
    )
    parser.add_argument(
        "--output-dir",
        default=os.path.dirname(os.path.abspath(__file__)),
        help="Base directory for output files (default: script directory)",
    )
    args = parser.parse_args()
    base = args.output_dir

    input_dir = os.path.join(base, "input")
    os.makedirs(input_dir, exist_ok=True)
    os.makedirs(os.path.join(base, "output"), exist_ok=True)

    stl_path = os.path.join(input_dir, "simpletube.stl")
    vtp_path = os.path.join(input_dir, "simpletube.vtp")
    cfg_path = os.path.join(base, "preprocessor_dev.simpletube.config.json")

    print("=" * 60)
    print("SimpleTube geometry generator")
    print(f"  Main tube : R={R_MAIN} mm, L={L_MAIN} mm, axis=Z")
    print(f"  Side open : R={R_SIDE} mm, at Z={Z_SIDE} mm, Y=wall")
    print(f"  dx        : {DX} mm/voxel")
    print("=" * 60)

    create_stl(stl_path)
    print()
    create_vtp(vtp_path)
    print()
    create_config(cfg_path)

    print()
    print("To run the preprocessor:")
    print(f"  python <hemoflow>/preprocessor/main.py {cfg_path}")


if __name__ == "__main__":
    main()
