"""Tests for slice.calculateScaleAndShift resolution selection."""

import pytest

from preprocessor import slice as slice_mod

# a 10 x 2 x 2 box as a fake triangle soup (only the vertices matter)
MESH = [
    [(0.0, 0.0, 0.0), (10.0, 2.0, 2.0), (0.0, 2.0, 0.0)],
    [(10.0, 0.0, 2.0), (0.0, 0.0, 2.0), (10.0, 2.0, 0.0)],
]


def test_target_dx_only():
    scale, shift, domain, bbox = slice_mod.calculateScaleAndShift(
        MESH, None, target_dx=0.5)
    assert domain == [20, 4, 4]


def test_target_elements_only():
    scale, shift, domain, bbox = slice_mod.calculateScaleAndShift(
        MESH, 40, target_dx=None)
    # vox_scale = (40 / 40)^(1/3) = 1 voxel per unit length
    assert domain == [10, 2, 2]


def test_target_dx_wins_over_elements():
    _, _, domain, _ = slice_mod.calculateScaleAndShift(MESH, 40, target_dx=0.5)
    assert domain == [20, 4, 4]


def test_neither_raises():
    with pytest.raises(ValueError):
        slice_mod.calculateScaleAndShift(MESH, None, target_dx=None)
