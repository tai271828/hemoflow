# SimpleTube Input File Generator

## What was done

The test case `testcase/input/simple_tube_straight.npz` was **not** created by
`preprocessor/main.py`. It was built directly in voxel space by a separate
script (`create_simple_tube.py` in the `palabos-applications-dev-toolbox` repo).

To allow the preprocessor to work with the same geometry, we
reverse-engineered the NPZ and created a generation script that produces the
two input files the preprocessor expects:

| File | Description |
|------|-------------|
| `input/simpletube.stl` | Watertight surface mesh of the tube lumen |
| `input/simpletube.vtp` | VMTK-style centerline with `MaximumInscribedSphereRadius` |

A matching `preprocessor_dev.simpletube.config.json` is also generated.

### Reference geometry (from the NPZ)

```
Grid:       30 x 30 x 300 voxels
dx:         0.25 mm/voxel

Main tube:  straight cylinder along Z
            radius = 10 voxels = 2.5 mm
            length = 300 voxels = 75 mm
            wall thickness = 2 voxels

Openings:
  10 (inlet)  317 vox  Z=0 face    R=2.511mm  normal=[0,0,1]
  11 (outlet) 317 vox  Z=299 face  R=2.511mm  normal=[0,0,1]
  12 (side)    81 vox  Y=27        R=1.269mm  normal=[0,1,0]

Side opening: circular hole (R=5 vox) punched through the wall
              at Z=150 (midpoint), Y=max side
```

### What the script generates

**STL** (`simpletube.stl`):
A Y-junction mesh: boolean union (via manifold3d) of a main capped cylinder
(R=2.5mm, L=75mm, 128 sections) and a branch stub (R=1.25mm, along +Y at
Z=37.5mm, Y=[1.0, 3.0]mm, 64 sections). The branch creates the side opening
that matches the original NPZ geometry.

**VTP** (`simpletube.vtp`):
Two polylines with per-point `MaximumInscribedSphereRadius`:

- **Line 0** (inlet to Z-max outlet): 151 points along Z from 0 to 75mm, constant R=2.5mm
- **Line 1** (inlet to side opening): 83 points along Z from 0 to 37.5mm, then along +Y to 3.0mm, R transitions from 2.5mm to 1.25mm

### Preprocessor bug fixes

Three bugs were fixed in the preprocessor to support non-cubic bounding boxes
(e.g., the Y-junction where the Y extent differs from X):

1. **`preprocessor/main.py` line 79** — `stentGeomFile` was undefined when no
   stent is configured. Fixed by initializing `stentGeomFile = ""`.

2. **`preprocessor/slice.py` `calculateScaleAndShift`** — Domain dimensions
   for axes 1 and 2 could be too small when the bounding box is non-cubic.
   The uniform scale (derived from axis 0) may cause the scaled mesh to extend
   beyond `int(vox_scale * ds[i])` on other axes. Scanline fill then misses
   exit crossings that fall outside the grid, producing spurious "is the
   geometry watertight?" errors. Fixed by ensuring each domain dimension
   covers `ceil(ds[i] * xyscale) + 1`.

3. **`preprocessor/main.py` `generateCutList`** — Centerline voxel positions
   are in the unpadded coordinate system, but `generateCutList` was comparing
   them against the padded domain boundaries (2 voxels larger per axis).
   Openings at exactly distance=4 from the padded boundary were missed.
   Fixed by passing the unpadded domain size to `generateCutList`.

## How to use

### 1. Set up the virtual environment

From the hemoflow project root:

```bash
uv venv .venv
uv pip install --python .venv/bin/python trimesh manifold3d vtk numpy numpy-stl
```

### 2. Generate the files

```bash
# Generate into the Fontan data directory
.venv/bin/python testcase/scripts/generate_simple_tube_inputs.py \
  --output-dir /path/to/palabos-applications-dev-toolbox/data/data-from-gabor/Fontan

# Or generate into any directory
.venv/bin/python testcase/scripts/generate_simple_tube_inputs.py \
  --output-dir /tmp/simpletube_test
```

This creates:

```
<output-dir>/
  input/
    simpletube.stl
    simpletube.vtp
  output/                              (empty, for preprocessor output)
  preprocessor_dev.simpletube.config.json
```

### 3. Run the preprocessor (optional)

```bash
PYTHONPATH=preprocessor \
  .venv/bin/python preprocessor/main.py \
  /path/to/output-dir/preprocessor_dev.simpletube.config.json
```

The preprocessor will voxelize the STL at the resolution determined by
`target_elements` (5,000,000 in the default config) and produce a compressed
NPZ in `<output-dir>/output/`.

## Dependencies

| Package | Purpose |
|---------|---------|
| `trimesh` | STL mesh creation (boolean union of cylinders) |
| `manifold3d` | Boolean CSG engine used by trimesh |
| `vtk` | VTP centerline file writing |
| `numpy` | Numeric computations |
| `numpy-stl` | Required by the preprocessor to read STL |

## Appendix: Reverse engineering process

Before discovering the original `create_simple_tube.py` script, the geometry
was reverse-engineered purely from the NPZ file and the preprocessor source
code. This appendix documents that investigation.

### Step 1: NPZ inspection

Loaded `testcase/input/simple_tube_straight.npz` and extracted:

```
Keys: geometryFlag, dx, openingIndex, openingRadius, openingNormal, stent

geometryFlag: shape (30, 30, 300), dtype int16
  0 (unused):  137,628
  1 (wall):     37,110
  2 (fluid):    94,547
  10 (inlet):      317
  11 (outlet):     317
  12 (side):        81

dx: [0.00025] m  (0.25 mm/voxel)

openingIndex:  [10, 11, 12]
openingRadius: [0.00251128, 0.00251128, 0.00126943] m
openingNormal: [[0,0,1], [0,0,1], [0,1,0]]
stent: all zeros (no stent)
```

Key observation: the NPZ lacks `openingCenter` and `openingNormalizedQRatio`
fields that `preprocessor/main.py` saves, indicating it was **not** created by
the preprocessor.

### Step 2: Spatial analysis of the voxel data

Located each opening in the voxel grid:

```
Opening 10: center=(15,15,  0), Z=0   face, 317 voxels
Opening 11: center=(15,15,299), Z=299 face, 317 voxels
Opening 12: center=(15,27,150), Y=27,        81 voxels
```

Cross-section analysis:

- Fluid spans X=[5,25], Y=[5,26] in the main tube (diameter ~20 voxels)
- At Y=27: only the 81-voxel side opening exists
- At Z=148-150: cross-section widens slightly where the side opening attaches
- pi*10^2 = 314 ~ 317 (inlet/outlet), pi*5^2 = 78.5 ~ 81 (side) confirmed
  the radii

### Step 3: Preprocessor pipeline analysis

Read all modules in `preprocessor/` to understand the full pipeline:

1. **`voxelizeStl.py`**: Imports STL via `numpy-stl`, computes scale/shift
   from bounding box and `target_elements`, voxelizes via Z-slice scanline
   fill. Creates `vol[z][x][y]`, pads +1 each side, swaps axes to `[x][y][z]`.

2. **`readCL.py`**: Reads VTP centerline. Each cell (polyline) runs from
   inlet to an outlet. Extracts `(radius, position, tangent)` at endpoints.
   Tangent for inlet = `pArray[1] - pArray[0]` (inward); for outlets =
   `pArray[-3] - pArray[-1]` (note: uses `[-3]` because last 2 points are
   sometimes identical in VMTK output).

3. **`createFluidSolid.py`**: Expands fluid by 1 voxel in all 26 directions
   to create walls. Removes unused outer layers. Cuts specified boundary
   faces to create openings.

4. **`detectOpenings.py`**: Finds fluid voxels adjacent to unused voxels.
   Groups them into connected components. Classifies: largest = inlet (10),
   smallest remaining = pressure outlet (11), rest = velocity outlets (12+).

5. **`main.py`**: Orchestrates the pipeline. Matches detected voxel openings
   to centerline openings by proximity (`inRange3D` with distance=4 voxels).

### Step 4: Coordinate transform analysis

Traced the voxel-to-physical mapping:

```
physical_pos_mm = voxel_pos / scale - shift
where shift = [-min_x, -min_y, -min_z] (moves STL minimum to origin)
      scale = domain[0] / ds[0]  (uniform, from bounding box)
      dx_m  = (1/scale) * 0.001
```

From `dx = 0.00025 m`: scale = 4.0 voxels/mm.

### Step 5: Initial T-junction mesh attempts (failed)

Initially assumed the side opening was a physical branch tube (T-junction).
Attempted several mesh generation approaches:

**Attempt 1 — trimesh CSG (boolean union of two cylinders):**
Main cylinder (R=2.5mm along Z) + branch cylinder (R=1.27mm along Y).
The `manifold3d` engine produced a watertight mesh (verified by trimesh), but
the preprocessor's scanline voxelizer failed at the junction with hundreds of
"is the geometry watertight?" errors. Root cause: at Z-slices through the
junction, the concave "keyhole" cross-section caused odd scanline crossing
counts.

**Attempt 2 — VTK implicit SDF + marching cubes:**
Defined the T-junction as `union(capped_cylinder_Z, capped_cylinder_Y)` using
`vtkImplicitBoolean`, sampled on a 0.08mm grid, extracted isosurface via
`vtkContourFilter`. Same scanline voxelizer failures at the junction.

**Attempt 3 — High-resolution trimesh CSG + subdivision:**
Increased cylinder sections to 256/128, subdivided result to 2.4M faces.
Same scanline failures.

**Debugging the scanline voxelizer:**
Tested the trimesh CSG mesh at multiple `target_elements` values:

```
target=  100,000  scale= 2.80  errors=  0
target=  500,000  scale= 5.00  errors=120
target=1,000,000  scale= 6.20  errors=  0
target=2,000,000  scale= 7.80  errors=  0
target=5,000,000  scale=10.80  errors=469
```

Errors were resolution-dependent (not consistently present), caused by
Z-slice heights aligning with mesh triangle edges at the junction.
Further investigation found the branch cylinder extending below the main
tube's bounding box created partial shapes at domain boundaries (y=0), giving
single-crossing scanlines.

Fixing the branch extent reduced but did not eliminate the errors. The
scanline fill algorithm in `perimeter.py` is fundamentally fragile at
concave junctions.

### Step 6: Discovery of the original creation script

The user pointed to `02-create-simple.tube.sh` which calls
`create_simple_tube.py`. This revealed the NPZ was constructed directly in
voxel space — no STL or preprocessor involved. The side opening is a
circular hole punched through the 2-voxel wall, not a physical branch tube.

This led to the final (successful) approach: a simple capped cylinder STL
(no junction) that voxelizes cleanly at all resolutions.
