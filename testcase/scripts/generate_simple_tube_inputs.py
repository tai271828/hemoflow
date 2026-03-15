#!/usr/bin/env python3
"""
Generate geometry_original_stl and centerline_vtp for the SimpleTube test case.

Reverse-engineered from testcase/input/simple_tube_straight.npz which was
created by scripts/create_simple_tube.py (voxel-space construction).

Reference geometry (voxel space, dx=0.25mm):
  - Straight cylinder: radius=10 vox, length=300 vox, 2-voxel walls
  - Grid: 30 x 30 x 300
  - Side opening: circular R=5 vox, at Z=150 (midpoint), Y=max side
    (fluid passage through wall at Y=26, opening at Y=27)
  - Openings:
      10 (inlet,  317 vox): Z=0   face, R_phys=2.511mm, normal=[0,0,1]
      11 (outlet, 317 vox): Z=299 face, R_phys=2.511mm, normal=[0,0,1]
      12 (side,    81 vox): Y=27,       R_phys=1.269mm, normal=[0,1,0]

Physical dimensions (mm):
  dx = 0.25 mm/voxel
  Main tube: R=2.5mm, L=75mm along Z
  Branch stub: R=1.25mm, along +Y at Z=37.5mm, extends ~1mm beyond tube surface

This script creates:
  - STL: watertight Y-junction surface mesh (main cylinder + branch stub)
  - VTP: VMTK-style centerline with MaximumInscribedSphereRadius
  - config.json: preprocessor configuration

Usage:
  python generate_simple_tube_inputs.py [--output-dir DIR]

Dependencies: trimesh, manifold3d, vtk, numpy
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
WALL_VOX = 2                         # wall thickness in voxels

R_MAIN = RADIUS_VOX * DX             # 2.5 mm
L_MAIN = LENGTH_VOX * DX             # 75.0 mm
R_SIDE = SIDE_R_VOX * DX             # 1.25 mm
Z_SIDE = L_MAIN / 2                  # 37.5 mm — side opening Z position

# Branch stub extends from inside the main tube to just beyond the wall.
# In the NPZ: fluid passage at Y=26 (1 voxel through wall), opening at Y=27.
# Physical: tube surface at 2.5mm from center, branch tip at ~3.0mm.
BRANCH_Y_LO = 1.0                    # start inside tube (for clean CSG overlap)
BRANCH_Y_HI = R_MAIN + WALL_VOX * DX # 2.5 + 0.5 = 3.0 mm (matches wall outer surface)

# Centerline sampling
CL_STEP = 0.5  # mm between centerline points


# ---------------------------------------------------------------------------
# STL generation — Y-junction (main cylinder + branch stub)
# ---------------------------------------------------------------------------
def create_stl(output_path: str) -> None:
    """
    Create watertight STL of the Y-junction lumen.

    Boolean union of:
      1. Main capped cylinder (R=2.5mm, L=75mm along Z)
      2. Branch capped cylinder (R=1.25mm, short stub along +Y at Z=37.5mm)
    """
    # Main cylinder along Z, from Z=0 to Z=L_MAIN
    main_cyl = trimesh.creation.cylinder(
        radius=R_MAIN,
        height=L_MAIN,
        sections=128,
    )
    main_cyl.apply_translation([0, 0, L_MAIN / 2])

    # Branch stub along +Y
    branch_height = BRANCH_Y_HI - BRANCH_Y_LO
    branch_center_y = (BRANCH_Y_LO + BRANCH_Y_HI) / 2

    branch_cyl = trimesh.creation.cylinder(
        radius=R_SIDE,
        height=branch_height,
        sections=64,
    )
    # Rotate: default Z axis -> Y axis
    rot = trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0])
    branch_cyl.apply_transform(rot)
    branch_cyl.apply_translation([0, branch_center_y, Z_SIDE])

    # Boolean union via manifold3d
    result = trimesh.boolean.union([main_cyl, branch_cyl], engine="manifold")

    assert result.is_watertight, "Mesh is NOT watertight after CSG!"
    assert result.is_volume, "Mesh does NOT enclose a volume!"

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    result.export(output_path)

    print(f"STL  -> {output_path}")
    print(f"       vertices={len(result.vertices)}  faces={len(result.faces)}")
    bb = result.bounds
    print(f"       bbox  min={bb[0]}  max={bb[1]}")
    return result


# ---------------------------------------------------------------------------
# VTP centerline generation
# ---------------------------------------------------------------------------
def create_vtp(output_path: str) -> None:
    """
    Create VMTK-style VTP centerline with MaximumInscribedSphereRadius.

    Two lines:
      Line 0: inlet (Z=0) -> outlet (Z=L_MAIN)    — main tube, R=2.5mm
      Line 1: inlet (Z=0) -> side opening (Y=max)  — path to branch tip
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
    # Path: along Z to midpoint, then along +Y to branch tip
    n_shared = int(Z_SIDE / CL_STEP) + 1
    line1_ids = []
    for i in range(n_shared):
        z = min(i * CL_STEP, Z_SIDE)
        pid = points.InsertNextPoint(0.0, 0.0, z)
        radii.InsertNextTuple1(R_MAIN)
        line1_ids.append(pid)

    # Branch along +Y from center to branch tip
    y_end = BRANCH_Y_HI  # must match STL bounding box edge
    n_branch = int(y_end / CL_STEP) + 1
    for i in range(1, n_branch + 1):
        y = min(i * CL_STEP, y_end)
        pid = points.InsertNextPoint(0.0, y, Z_SIDE)
        # Radius transitions from R_MAIN (at center) to R_SIDE (at tip)
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
    print("SimpleTube Y-junction geometry generator")
    print(f"  Main tube  : R={R_MAIN} mm, L={L_MAIN} mm, axis=Z")
    print(f"  Branch stub: R={R_SIDE} mm, at Z={Z_SIDE} mm, axis=+Y")
    print(f"  Branch Y   : [{BRANCH_Y_LO}, {BRANCH_Y_HI}] mm")
    print(f"  dx         : {DX} mm/voxel")
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
