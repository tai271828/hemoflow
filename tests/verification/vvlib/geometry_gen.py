"""Parametric synthetic vessel geometries for HemoFlow V&V.

Builds tube trees as a union of tapered-capsule signed-distance fields, extracts
the wall surface with marching cubes (open, flush tube ends on the axis-aligned
bounding-box faces, as the preprocessor requires) and writes the matching
centerline VTP with a ``Radius`` point-data array (one polyline per outlet,
running from the inlet to that outlet, per preprocessor/preprocessor/centerline.py).

All dimensions are millimeters (preprocessor si_factor = 0.001).

Conventions copied from the reference case tests/verification/0-pipe/input/:
- the surface STL is open at every opening, cut flat on a bbox face;
- centerline endpoints lie exactly on the opening planes;
- the wall may touch lateral bbox faces tangentially (the reference pipe does).
"""

import json
from pathlib import Path

import numpy as np
import pyvista as pv


# ---------------------------------------------------------------------------
# SDF construction
# ---------------------------------------------------------------------------

def _segment_sdf(pts, p0, p1, r0, r1):
    """Distance field of a tapered capsule from p0 (radius r0) to p1 (radius r1)."""
    p0 = np.asarray(p0, dtype=float)
    d = np.asarray(p1, dtype=float) - p0
    t = np.clip((pts - p0) @ d / (d @ d), 0.0, 1.0)
    proj = p0 + t[:, None] * d
    radius = r0 + t * (r1 - r0)
    return np.linalg.norm(pts - proj, axis=1) - radius


def _arc_path(center, start_angle, end_angle, radius, n=12):
    """Points on a circular arc in the xy-plane (angles in radians)."""
    ang = np.linspace(start_angle, end_angle, n)
    return np.stack(
        [center[0] + radius * np.cos(ang),
         center[1] + radius * np.sin(ang),
         np.full(n, center[2])], axis=1)


def _polyline_segments(points, r0, r1):
    """Turn a polyline into tapered-capsule segments with linearly blended radii."""
    points = np.asarray(points, dtype=float)
    seg_len = np.linalg.norm(np.diff(points, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg_len)])
    s /= s[-1]
    radii = r0 + s * (r1 - r0)
    return [(points[i], points[i + 1], radii[i], radii[i + 1])
            for i in range(len(points) - 1)]


def _resample_polyline(points, spacing=0.5):
    """Resample a polyline to (roughly) uniform point spacing in mm."""
    points = np.asarray(points, dtype=float)
    seg = np.linalg.norm(np.diff(points, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    n = max(int(np.ceil(s[-1] / spacing)) + 1, 8)
    si = np.linspace(0.0, s[-1], n)
    return np.stack([np.interp(si, s, points[:, k]) for k in range(3)], axis=1), si / s[-1]


# ---------------------------------------------------------------------------
# Surface + centerline output
# ---------------------------------------------------------------------------

def _extract_surface(segments, bounds, open_faces, h):
    """Sample the union SDF and extract the zero isosurface.

    ``bounds`` are the exact tube-tree bounds. Faces listed in ``open_faces``
    (e.g. ["x-", "x+", "y+"]) hold openings: the sample grid ends exactly on
    those faces, so marching cubes leaves the tube ends open there. All other
    faces are padded by one cell so tangent walls do not sit exactly on grid
    nodes (which would produce degenerate contour triangles).
    """
    bounds = np.asarray(bounds, dtype=float).reshape(3, 2).copy()
    for k, axis in enumerate("xyz"):
        if f"{axis}-" not in open_faces:
            bounds[k][0] -= h
        if f"{axis}+" not in open_faces:
            bounds[k][1] += h
    dims = [int(np.ceil((b[1] - b[0]) / h)) + 1 for b in bounds]
    grid = pv.ImageData(
        dimensions=dims,
        spacing=[(b[1] - b[0]) / (n - 1) for b, n in zip(bounds, dims)],
        origin=bounds[:, 0],
    )
    pts = grid.points.astype(float)
    sdf = np.full(pts.shape[0], np.inf)
    for p0, p1, r0, r1 in segments:
        np.minimum(sdf, _segment_sdf(pts, p0, p1, r0, r1), out=sdf)
    grid.point_data["sdf"] = sdf
    surf = grid.contour([0.0], scalars="sdf").triangulate()
    surf.clear_data()
    return surf


def _centerline_polydata(paths):
    """Build a PolyData with one polyline per (points, radii) path."""
    all_pts, all_radii, lines = [], [], []
    offset = 0
    for pts, radii in paths:
        pts = np.asarray(pts, dtype=float)
        lines.append(np.concatenate([[len(pts)], np.arange(offset, offset + len(pts))]))
        all_pts.append(pts)
        all_radii.append(np.asarray(radii, dtype=float))
        offset += len(pts)
    poly = pv.PolyData(np.vstack(all_pts), lines=np.concatenate(lines))
    poly.point_data["Radius"] = np.concatenate(all_radii)
    return poly


def _write_case(out_dir, name, segments, bounds, open_faces, paths, spec, h):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    surf = _extract_surface(segments, bounds, open_faces, h)
    stl_path = out_dir / f"{name}_Srf.stl"
    vtp_path = out_dir / f"{name}_Ctl.vtp"
    surf.save(stl_path)
    _centerline_polydata(paths).save(vtp_path)
    spec = dict(spec)
    spec["stl"] = stl_path.name
    spec["vtp"] = vtp_path.name
    spec["bounds_mm"] = np.asarray(bounds, dtype=float).ravel().tolist()
    with open(out_dir / f"{name}_spec.json", "w") as f:
        json.dump(spec, f, indent=2)
    return spec


# ---------------------------------------------------------------------------
# Geometries
# ---------------------------------------------------------------------------

def make_straight_tube(out_dir, D=3.0, L=30.0, h=0.1):
    """Case A: straight tube along +x, openings on the x- and x+ faces."""
    R = D / 2.0
    p0, p1 = np.array([0.0, 0.0, 0.0]), np.array([L, 0.0, 0.0])
    segments = [(p0, p1, R, R)]
    pts, _ = _resample_polyline([p0, p1])
    paths = [(pts, np.full(len(pts), R))]
    spec = {
        "name": "straight_tube",
        "params": {"D": D, "L": L, "h": h},
        "openings": [
            {"id": "inlet", "center_mm": [0.0, 0.0, 0.0], "radius_mm": R,
             "normal": [1, 0, 0], "face": "x-"},
            {"id": "outlet", "center_mm": [L, 0.0, 0.0], "radius_mm": R,
             "normal": [-1, 0, 0], "face": "x+"},
        ],
    }
    bounds = [0.0, L, -R, R, -R, R]
    return _write_case(out_dir, "straight_tube", segments, bounds,
                       ["x-", "x+"], paths, spec, h)


def make_side_opening_tube(out_dir, D_main=4.0, L_main=30.0, D_side=2.0,
                           L_side=8.0, x_side=15.0, h=0.1):
    """Case B: straight tube along +x with a side branch along +y (T-junction).

    Openings: inlet on x-, main outlet on x+, side outlet on y+.
    """
    Rm, Rs = D_main / 2.0, D_side / 2.0
    a0, a1 = np.array([0.0, 0.0, 0.0]), np.array([L_main, 0.0, 0.0])
    b0, b1 = np.array([x_side, 0.0, 0.0]), np.array([x_side, L_side, 0.0])
    segments = [(a0, a1, Rm, Rm), (b0, b1, Rs, Rs)]
    main_pts, _ = _resample_polyline([a0, a1])
    side_pts, _ = _resample_polyline([a0, b0, b1])
    n_side = len(side_pts)
    # radius: main radius until the junction, then blend to the side radius
    side_r = np.full(n_side, Rs)
    on_main = side_pts[:, 1] <= 1e-9
    side_r[on_main] = Rm
    paths = [
        (main_pts, np.full(len(main_pts), Rm)),
        (side_pts, side_r),
    ]
    spec = {
        "name": "side_opening_tube",
        "params": {"D_main": D_main, "L_main": L_main, "D_side": D_side,
                   "L_side": L_side, "x_side": x_side, "h": h},
        "openings": [
            {"id": "inlet", "center_mm": [0.0, 0.0, 0.0], "radius_mm": Rm,
             "normal": [1, 0, 0], "face": "x-"},
            {"id": "main_outlet", "center_mm": [L_main, 0.0, 0.0], "radius_mm": Rm,
             "normal": [-1, 0, 0], "face": "x+"},
            {"id": "side_outlet", "center_mm": [x_side, L_side, 0.0], "radius_mm": Rs,
             "normal": [0, -1, 0], "face": "y+"},
        ],
    }
    bounds = [0.0, L_main, -Rm, L_side, -Rm, Rm]
    return _write_case(out_dir, "side_opening_tube", segments, bounds,
                       ["x-", "x+", "y+"], paths, spec, h)


def make_y_bifurcation(out_dir, D_parent=4.0, L_parent=12.0, D_d1=3.2, D_d2=2.6,
                       angle_deg=35.0, L_arm=8.0, arc_radius=6.0, x_end=30.0, h=0.1):
    """Case C: planar Y bifurcation.

    Parent runs along +x from the x- face. At the junction each daughter leaves
    at +/-angle in the xy-plane, follows a straight arm, turns back to +x on a
    circular arc, and runs straight to the x+ face, ending perpendicular to it.
    Daughter diameters are deliberately unequal (distinguishable Murray split).
    """
    Rp, R1, R2 = D_parent / 2.0, D_d1 / 2.0, D_d2 / 2.0
    ang = np.deg2rad(angle_deg)
    junction = np.array([L_parent, 0.0, 0.0])

    def daughter_path(sign):
        d = np.array([np.cos(ang), sign * np.sin(ang), 0.0])
        arm_end = junction + L_arm * d
        # arc turning from direction 'd' back to +x: the arc center sits on the
        # inner side of the turn, perpendicular to the travel direction
        center = arm_end + arc_radius * np.array([np.sin(ang),
                                                  -sign * np.cos(ang), 0.0])
        a0 = np.arctan2(arm_end[1] - center[1], arm_end[0] - center[0])
        a1 = a0 - sign * ang  # tangent becomes +x here
        arc = _arc_path(center, a0, a1, arc_radius, n=16)
        straight_end = arc[-1].copy()
        straight_end[0] = x_end
        return np.vstack([[junction], [arm_end], arc[1:], [straight_end]])

    path1 = daughter_path(+1.0)
    path2 = daughter_path(-1.0)
    parent = [np.array([0.0, 0.0, 0.0]), junction]

    segments = _polyline_segments(parent, Rp, Rp)
    segments += _polyline_segments(path1, R1, R1)
    segments += _polyline_segments(path2, R2, R2)

    paths = []
    for dpath, rd in ((path1, R1), (path2, R2)):
        pts, frac = _resample_polyline(np.vstack([parent[0], dpath]))
        # parent radius up to the junction, daughter radius beyond
        along_parent = pts[:, 0] <= L_parent - 1e-9
        radii = np.where(along_parent & (np.abs(pts[:, 1]) < 1e-9), Rp, rd)
        paths.append((pts, radii))

    end1, end2 = path1[-1], path2[-1]
    ymax = max(abs(end1[1]) + R1, abs(end2[1]) + R2)
    zmax = Rp
    spec = {
        "name": "y_bifurcation",
        "params": {"D_parent": D_parent, "L_parent": L_parent, "D_d1": D_d1,
                   "D_d2": D_d2, "angle_deg": angle_deg, "L_arm": L_arm,
                   "arc_radius": arc_radius, "x_end": x_end, "h": h},
        "openings": [
            {"id": "inlet", "center_mm": [0.0, 0.0, 0.0], "radius_mm": Rp,
             "normal": [1, 0, 0], "face": "x-"},
            {"id": "daughter1", "center_mm": end1.tolist(), "radius_mm": R1,
             "normal": [-1, 0, 0], "face": "x+"},
            {"id": "daughter2", "center_mm": end2.tolist(), "radius_mm": R2,
             "normal": [-1, 0, 0], "face": "x+"},
        ],
        "centerline_path_lengths_mm": {
            "daughter1": float(np.sum(np.linalg.norm(np.diff(np.vstack([parent[0], path1]), axis=0), axis=1))),
            "daughter2": float(np.sum(np.linalg.norm(np.diff(np.vstack([parent[0], path2]), axis=0), axis=1))),
        },
    }
    bounds = [0.0, x_end, -ymax, ymax, -zmax, zmax]
    return _write_case(out_dir, "y_bifurcation", segments, bounds,
                       ["x-", "x+"], paths, spec, h)


GENERATORS = {
    "straight_tube": make_straight_tube,
    "side_opening_tube": make_side_opening_tube,
    "y_bifurcation": make_y_bifurcation,
}


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="Generate a synthetic V&V vessel geometry")
    ap.add_argument("geometry", choices=sorted(GENERATORS))
    ap.add_argument("out_dir")
    ap.add_argument("--h", type=float, default=0.1, help="SDF sample spacing [mm]")
    args = ap.parse_args()
    spec = GENERATORS[args.geometry](args.out_dir, h=args.h)
    print(json.dumps(spec, indent=2))
