"""Post-processing of HemoFlow VTI output for the verification checks.

The slicing/integration approach is ported from the historical scripts
tests/verification/0-pipe/pressure_drop.py and 1-flow_split/Murray_check.py.
"""

import glob
import os

import numpy as np
import pyvista as pv


def mag(a):
    """Magnitude of an array of vectors."""
    return np.sqrt((np.asarray(a) ** 2).sum(axis=-1))


def load_npz(path):
    return np.load(path)


def list_states(output_dir):
    """Sorted list of VTI output files."""
    files = glob.glob(os.path.join(str(output_dir), "**", "*.vti"), recursive=True)
    if not files:
        raise FileNotFoundError(f"No .vti output found under {output_dir}")
    return sorted(files)


def load_state(vti_path):
    """Load one VTI frame and return thresholded cell data with velocity magnitude."""
    pointdata = pv.read(vti_path)
    pointdata.point_data["velocity_magnitude"] = mag(pointdata.point_data["velocity [m/s]"])
    voxeldata = pointdata.point_data_to_cell_data()
    threshold = voxeldata.threshold(value=1e-4, scalars="velocity_magnitude")
    return pointdata, threshold


def steadiness(output_dir):
    """Relative change of the velocity field between the last two frames."""
    states = list_states(output_dir)
    if len(states) < 2:
        return np.inf
    a = pv.read(states[-2]).point_data["velocity [m/s]"]
    b = pv.read(states[-1]).point_data["velocity [m/s]"]
    return float(mag(b - a).max() / max(mag(b).max(), 1e-30))


def _local_box(center, size):
    lo = np.asarray(center) - size
    hi = np.asarray(center) + size
    return [lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]]


def opening_flows(threshold, npz, inset_voxels=8):
    """Flow rate and mean pressure at every opening of the geometry.

    Openings are sliced ``inset_voxels`` inside the domain along the opening
    normal (npz normals point into the domain). Returns a list of dicts in npz
    array order with signed flow rate Q = integral(v . n) dA — positive at the
    inlet, negative at outlets.
    """
    dx = float(npz["dx"][0])
    out = []
    for i in range(len(npz["openingRadius"])):
        center = npz["openingCenter"][i] * dx
        normal = np.asarray(npz["openingNormal"][i], dtype=float)
        normal /= np.linalg.norm(normal)
        radius = float(npz["openingRadius"][i])
        origin = center + inset_voxels * dx * normal
        local = threshold.clip_box(_local_box(origin, 3.0 * radius), invert=False)
        sl = local.slice(origin=origin, normal=normal)
        sl["perp_velocity"] = sl["velocity [m/s]"] @ normal
        integ = sl.integrate_data()
        area = float(integ["Area"][0])
        out.append({
            "index": i,
            "label": int(npz["openingIndex"][i]),
            "center_m": center,
            "normal": normal,
            "radius_m": radius,
            "area_m2": area,
            "Q_m3s": float(integ["perp_velocity"][0]),
            "p_mean_Pa": float(integ["density [Pa]"][0] / area) if area else np.nan,
        })
    return out


def find_opening(flows, normal=None, center_pred=None):
    """Select one opening by (approximate) normal direction and/or a predicate
    on its center coordinates [m]."""
    hits = []
    for f in flows:
        if normal is not None and np.dot(f["normal"], normal) < 0.9:
            continue
        if center_pred is not None and not center_pred(f["center_m"]):
            continue
        hits.append(f)
    if len(hits) != 1:
        raise ValueError(f"Opening selection matched {len(hits)} openings, expected 1")
    return hits[0]


def plane_pressure_and_flow(threshold, x, bounds=None):
    """Area-averaged pressure and flow rate on the plane x = const [m]."""
    sl = threshold.slice(origin=[x, 0.0, 0.0], normal=[1, 0, 0])
    sl["perp_velocity"] = sl["velocity [m/s]"] @ np.array([1.0, 0.0, 0.0])
    integ = sl.integrate_data()
    area = float(integ["Area"][0])
    return {
        "p_mean_Pa": float(integ["density [Pa]"][0] / area),
        "Q_m3s": float(integ["perp_velocity"][0]),
        "area_m2": area,
    }


def velocity_profile(threshold, origin, direction, half_length, n=200, recenter=True):
    """Sample the velocity magnitude along a line through ``origin``.

    Returns (offsets [m], velocity magnitude [m/s]) with offsets in
    [-half_length, half_length] relative to the origin.

    With ``recenter`` (default) the offsets are shifted so that r = 0 lies on
    the vertex of a parabola fitted to the profile core. The nominal origin
    (a detected opening center) can be off-axis by ~half a voxel, which would
    otherwise dominate the near-wall comparison error.
    """
    origin = np.asarray(origin, dtype=float)
    direction = np.asarray(direction, dtype=float)
    direction = direction / np.linalg.norm(direction)
    a = origin - half_length * direction
    b = origin + half_length * direction
    line = threshold.sample_over_line(pointa=a, pointb=b, resolution=n)
    offs = np.linspace(-half_length, half_length, line.n_points)
    u = np.asarray(line["velocity_magnitude"])
    valid = np.asarray(line["vtkValidPointMask"], dtype=bool)
    offs, u = offs[valid], u[valid]
    if recenter and len(u) > 5:
        core = u > 0.6 * u.max()
        coef = np.polyfit(offs[core], u[core], 2)
        vertex = -coef[1] / (2.0 * coef[0])
        offs = offs - vertex
    return offs, u


def profile_l2_error(offsets, u_num, u_ana, r_max):
    """Relative L2 error of a velocity profile, restricted to |r| <= r_max."""
    m = np.abs(offsets) <= r_max
    return float(np.linalg.norm(u_num[m] - u_ana[m]) / np.linalg.norm(u_ana[m]))
