# Watertight false-alarm: root cause study

## Symptom

Running the preprocessor on `simpletube.sharp.stl` prints a flood of warnings:

```
$ python preprocessor/main.py .../simpletube/preprocessor_dev.simpletube.sharp.config.json
### Voxelizing vessel geometry ###
An error has occured at x26z238.0 - is the geometry watertight?
An error has occured at x27z238.0 - is the geometry watertight?
...
An error has occured at x44z241.0 - is the geometry watertight?
...
```

The warning originates at `preprocessor/perimeter.py:34`, inside `linesToVoxels`:

```python
if isBlack:
    print("An error has occured at x%sz%s - is the geometry watertight?" % (x, lineList[0][0][2]))
```

The mesh is in fact watertight — this is a **false alarm**.

## Root cause

There is an off-by-one in `preprocessor/slice.py:calculateScaleAndShift` that
allocates the per-slice pixel array slightly smaller than the scaled mesh
extent. Wall crossings that land on the missing last row are silently dropped,
which leaves the parity flag `isBlack` stuck at `True` at end-of-row.

### The numbers

For `simpletube.sharp.stl`:

| quantity                       | value           |
|--------------------------------|-----------------|
| bounding-box extents `ds`      | `[5.0, 5.5, 75.0]` |
| `vox_scale = (5e6/ds3)^(1/3)`  | `~13.434`       |
| `domain = [int(vox_scale*d) for d in ds]` | `[67, 73, 1007]` |
| `xyscale = domain[0]/ds[0]`    | `67 / 5 = 13.4` |
| **scaled y extent applied to mesh** `xyscale*ds[1]` | **`13.4 * 5.5 = 73.7`** |
| **allocated pixel array y size** `domain[1]`        | **`73`**        |

The mesh is scaled by `xyscale = 13.4`, so a vertex sitting on world
`y = ymax = 3.0` lands at voxel `y = 73.7`. The pixel array however has
shape `(domain[0], domain[1]) = (67, 73)` — valid y indices are `0..72`.
Anything on the top wall is **outside the array**.

### Why the warning fires (mechanism)

`linesToVoxels` (`preprocessor/perimeter.py:5-34`) is a scan-line filler. For
each `x`, it walks `y` and toggles `isBlack` every time it hits a wall
crossing. After visiting the last `y`, `isBlack` should be `False` (an even
number of crossings) for any closed contour. If it isn't, the warning fires.

For the "sharp" tube at `z=238` (≈ `world z = 17.76`), the cross-section has a
flat top face at world `y = 3.0`. The slice generates line segments whose y
endpoints are `~73.7`. For a fixed `x` on that flat top, e.g. `x=26`:

```
relevant lines at x=26: 2
  line A: ((26.8, 0.678), (23.6, 1.508))     -> generateY(x=26) =  0.886 -> int = 0
  line B: ((23.45, 70.50), (26.65, 73.70))   -> generateY(x=26) = 73.05  -> int = 73

targetYs = [0, 73]
```

The scan loop runs `for y in range(len(pixels[x]))` = `range(73)`, i.e.
`y ∈ 0..72`. So:

* `y=0` → toggles `isBlack` to `True`  ✓
* `y=73` → **never visited** (out of range)
* end of row → `isBlack == True` → warning fires.

### Why "sharp" trips this and "smooth" doesn't

The smooth `simpletube.stl` is a curved tube — its boundary almost never sits
*exactly* on `y = ymax`. After scaling, the upper crossings round to `y=72`
(inside the array) instead of `y=73`. The crossings happen, parity flips back
to `False`, no warning.

The "sharp" variant has a flat top face at `y = 3.0` exactly (entire edges sit
on `y_max`). Every x along that flat segment hits the bug, which produces the
characteristic block of contiguous `(x, z)` warnings:

```
x ∈ [22..44] × z ∈ [238..241+]
```

— exactly the x-range and z-range over which the flat top edge appears in the
sliced cross-section.

## Where the bug lives

`preprocessor/slice.py:98-119`:

```python
def calculateScaleAndShift(mesh, targetElements):
    ...
    domain = [int(x) for x in [vox_scale * ds[0], vox_scale * ds[1], vox_scale * ds[2]]]
    shift  = [-minimum for minimum in mins]

    xyscale = domain[0] / ds[0]
    scale   = [xyscale, xyscale, xyscale]   # TODO Something is fishy here, what is this xyscale???
    ...
```

`domain[i]` is computed from `vox_scale`, but the mesh is then scaled by
`xyscale = domain[0]/ds[0]`, which is *slightly larger* than `vox_scale`
(because `domain[0] = int(vox_scale*ds[0]) ≤ vox_scale*ds[0]`, but the
denominator `ds[0]` is exact, so the ratio overshoots). The scaled mesh
therefore extends past `domain[i]` on dimensions `i ≠ 0`.

In numbers:

```
vox_scale * ds[1] = 13.434 * 5.5 = 73.886   -> int -> 73   (= domain[1])
xyscale  * ds[1]  = 13.4   * 5.5 = 73.7     -> NOT bounded by domain[1]
```

So the array is sized using `vox_scale` but the mesh is scaled by `xyscale`,
and the two never agree.

`padVoxelArray` (called later in `voxelizeStl.voxelize`) does add a padding
border, but it runs *after* slicing — by then the missing crossings have
already been silently dropped.

## Fix

Make the domain match the scaled mesh exactly. After computing `xyscale`,
derive `domain` from it (using `ceil` to be safe, +1 to keep an inclusive top
row):

```python
import math
xyscale = ...                          # compute first
domain  = [int(math.ceil(xyscale * d)) + 1 for d in ds]
scale   = [xyscale, xyscale, xyscale]
```

Trace at `x=26, z=238` after the fix:

* `domain[1]` becomes `74` (or larger), pixel array y-range is `0..73`.
* `y=73` is now visited, `line B` fires, `isBlack` toggles back to `False`.
* No warning.

## Reproduction commands

```sh
# reproduce
.venv/bin/python preprocessor/main.py \
  ../palabos-applications-dev-toolbox/data/simpletube/preprocessor_dev.simpletube.sharp.config.json \
  | head -20
```

Trace one bad row directly:

```python
import sys; sys.path.insert(0, 'preprocessor')
from voxelizeStl import import_stl_file
import slice as sl, perimeter

mesh = list(import_stl_file('.../simpletube.sharp.stl'))
scale, shift, domain, bbox = sl.calculateScaleAndShift(mesh, 5_000_000)
mesh = list(sl.scaleAndShiftMesh(mesh, scale, shift))

lines    = sl.toIntersectingLines(mesh, 238)
relevant = list(perimeter.findRelevantLines(lines, 26))
print([int(perimeter.generateY(L, 26)) for L in relevant])  # -> [0, 73]
print('domain[1] =', domain[1])                              # -> 73
```
