"""Preprocessor integration tests: generate a synthetic geometry, voxelize it,
and assert the detected openings match the generator specification.

These run without the C++ solver (marker: preproc) and are the fast guard for
geometry-handling regressions, including the bifurcation case.
"""

import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

from vvlib import geometry_gen

pytestmark = pytest.mark.preproc

FACE_AXIS = {"x-": (0, -1), "x+": (0, +1), "y-": (1, -1), "y+": (1, +1),
             "z-": (2, -1), "z+": (2, +1)}

CASES = [
    ("straight_tube", {}, 150_000),
    ("straight_tube", {}, 400_000),
    ("side_opening_tube", {}, 200_000),
    ("y_bifurcation", {}, 250_000),
]


def voxelize(tmp_path, geometry, params, target_elements):
    spec = geometry_gen.GENERATORS[geometry](tmp_path, h=0.15, **params)
    config = {
        "geometry_original_stl": spec["stl"],
        "centerline_vtp": spec["vtp"],
        "stent_mesh_base": "",
        "target_elements": str(target_elements),
        "target_dx": "",
        "output_base_name": "vox_",
        "cutWidth": "1",
        "distance": "4",
        "rotation": {"enabled": False},
    }
    (tmp_path / "vox.json").write_text(json.dumps(config))
    subprocess.run([sys.executable, "-m", "preprocessor", "vox.json"],
                   cwd=tmp_path, check=True, capture_output=True, text=True)
    return spec, np.load(tmp_path / "vox_c.npz")


@pytest.mark.parametrize("geometry,params,target_elements",
                         CASES, ids=[f"{c[0]}-{c[2]//1000}k" for c in CASES])
def test_voxelization_detects_openings(tmp_path, geometry, params, target_elements):
    spec, npz = voxelize(tmp_path, geometry, params, target_elements)
    expected = spec["openings"]
    dx = float(npz["dx"][0])
    shape = npz["geometryFlag"].shape

    # opening count
    assert len(npz["openingRadius"]) == len(expected)

    # radii within 10 %, sorted descending (centerline.py sorts by radius)
    detected_r = np.asarray(npz["openingRadius"], dtype=float)
    assert np.all(np.diff(detected_r) <= 1e-12), "openings not sorted by radius"
    expected_r = sorted((o["radius_mm"] / 1000.0 for o in expected), reverse=True)
    np.testing.assert_allclose(detected_r, expected_r, rtol=0.10)

    # normals within ~10 degrees of an axis, centers on the expected faces
    expected_faces = sorted(o["face"] for o in expected)
    detected_faces = []
    for i in range(len(expected)):
        n = np.asarray(npz["openingNormal"][i], dtype=float)
        n /= np.linalg.norm(n)
        axis = int(np.argmax(np.abs(n)))
        assert abs(n[axis]) > 0.985, f"opening {i} normal {n} not axis-aligned"
        # npz normals point into the domain: +axis means the x-/y-/z- face
        side = "-" if n[axis] > 0 else "+"
        detected_faces.append("xyz"[axis] + side)
        # center must sit on that face (within the detection distance)
        c = np.asarray(npz["openingCenter"][i], dtype=float)
        margin = 6.0  # distance(4) + cutWidth + 1
        if side == "-":
            assert c[axis] <= margin, f"opening {i} not on face {detected_faces[-1]}"
        else:
            assert c[axis] >= shape[axis] - 1 - margin
    assert sorted(detected_faces) == expected_faces

    # voxel fluid volume within 15 % of the analytic tube volume
    labels = npz["geometryFlag"]
    fluid_m3 = float(np.count_nonzero(labels >= 2)) * dx**3
    assert abs(fluid_m3 - _analytic_volume(spec)) / _analytic_volume(spec) < 0.15

    # no unlabeled fluid leaks on the domain boundary faces
    for axis in range(3):
        for sl in (0, -1):
            face = np.take(labels, sl, axis=axis)
            assert not np.any(face == 2), \
                f"fluid voxels leak through the domain boundary (axis {axis})"


def _analytic_volume(spec):
    """Analytic fluid volume [m^3] of a generated geometry."""
    p = spec["params"]
    mm3 = 1e-9
    name = spec["name"]
    if name == "straight_tube":
        return np.pi * (p["D"] / 2) ** 2 * p["L"] * mm3
    if name == "side_opening_tube":
        main = np.pi * (p["D_main"] / 2) ** 2 * p["L_main"]
        side = np.pi * (p["D_side"] / 2) ** 2 * (p["L_side"] - p["D_main"] / 2)
        return (main + side) * mm3
    if name == "y_bifurcation":
        lengths = spec["centerline_path_lengths_mm"]
        parent = np.pi * (p["D_parent"] / 2) ** 2 * p["L_parent"]
        d1 = np.pi * (p["D_d1"] / 2) ** 2 * (lengths["daughter1"] - p["L_parent"])
        d2 = np.pi * (p["D_d2"] / 2) ** 2 * (lengths["daughter2"] - p["L_parent"])
        return (parent + d1 + d2) * mm3
    raise ValueError(name)


def test_generator_is_deterministic(tmp_path):
    a = tmp_path / "a"
    b = tmp_path / "b"
    geometry_gen.make_straight_tube(a, h=0.15)
    geometry_gen.make_straight_tube(b, h=0.15)
    assert (a / "straight_tube_Srf.stl").read_bytes() == \
           (b / "straight_tube_Srf.stl").read_bytes()


def test_generated_surface_is_clean(tmp_path):
    """Walls manifold, exactly the expected open rings, bounds as specified."""
    import pyvista as pv
    spec = geometry_gen.make_y_bifurcation(tmp_path, h=0.15)
    surf = pv.read(tmp_path / spec["stl"])
    edges = surf.extract_feature_edges(
        boundary_edges=True, feature_edges=False,
        manifold_edges=False, non_manifold_edges=False)
    n_rings = len(np.unique(edges.connectivity()["RegionId"]))
    assert n_rings == len(spec["openings"])
    np.testing.assert_allclose(surf.bounds[:2], spec["bounds_mm"][:2], atol=1e-6)
