"""Tests for centerline VTP reading, including the radius-array fallback.

VMTK writes the local radius as 'MaximumInscribedSphereRadius'; hand-made
centerlines use 'Radius'. getOpeningsFromCenterline must accept both and fail
clearly when neither is present.
"""

import numpy as np
import pytest
import vtk

from preprocessor.centerline import getOpeningsFromCenterline


def _write_centerline(path, radius_array_name):
    """A single 5-point polyline along +x from (0,0,0) to (4,0,0)."""
    points = vtk.vtkPoints()
    for i in range(5):
        points.InsertNextPoint(float(i), 0.0, 0.0)

    line = vtk.vtkPolyLine()
    line.GetPointIds().SetNumberOfIds(5)
    for i in range(5):
        line.GetPointIds().SetId(i, i)
    cells = vtk.vtkCellArray()
    cells.InsertNextCell(line)

    poly = vtk.vtkPolyData()
    poly.SetPoints(points)
    poly.SetLines(cells)

    if radius_array_name:
        radii = vtk.vtkDoubleArray()
        radii.SetName(radius_array_name)
        for r in (2.0, 2.0, 2.0, 1.5, 1.5):
            radii.InsertNextValue(r)
        poly.GetPointData().AddArray(radii)

    writer = vtk.vtkXMLPolyDataWriter()
    writer.SetFileName(str(path))
    writer.SetInputData(poly)
    writer.Write()


@pytest.mark.parametrize("array_name", ["Radius", "MaximumInscribedSphereRadius"])
def test_reads_radius_array(tmp_path, array_name):
    vtp = tmp_path / "ctl.vtp"
    _write_centerline(vtp, array_name)

    openings = getOpeningsFromCenterline(str(vtp))

    # one line -> inlet (line start) + one outlet (line end), sorted by radius
    assert len(openings) == 2
    radii = [o[0] for o in openings]
    assert radii == sorted(radii, reverse=True)
    assert radii[0] == pytest.approx(2.0)   # inlet radius
    assert radii[1] == pytest.approx(1.5)   # outlet radius
    # inlet tangent points inward (+x), outlet tangent inward (-x)
    inlet, outlet = openings
    assert np.dot(inlet[2], [1, 0, 0]) > 0
    assert np.dot(outlet[2], [-1, 0, 0]) > 0


def test_missing_radius_array_raises(tmp_path):
    vtp = tmp_path / "ctl.vtp"
    _write_centerline(vtp, None)

    with pytest.raises(ValueError, match="Radius"):
        getOpeningsFromCenterline(str(vtp))
