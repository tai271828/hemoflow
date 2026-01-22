# writeHDF5 Function Analysis and Vector/Matrix Implementation

## Table of Contents
1. [What writeHDF5 Does](#what-writehdf5-does)
2. [Background Knowledge](#background-knowledge)
3. [Current Implementation Analysis](#current-implementation-analysis)
4. [The TODO: Save as Vectors and Matrices](#the-todo-save-as-vectors-and-matrices)
5. [Implementation Strategy](#implementation-strategy)
6. [Code Changes](#code-changes)

---

## What writeHDF5 Does

The `writeHDF5` function in `io.cpp:105` saves simulation data from a Lattice Boltzmann Method (LBM) blood flow simulation to HDF5 files with accompanying XDMF metadata files for visualization.

### High-Level Overview

```
writeHDF5(lattice, sim, iter, outDir, field1)
    │
    ├── 1. Compute physical quantities from lattice
    │       ├── Velocity (3 components: vx, vy, vz)
    │       ├── Density (scalar)
    │       ├── Shear Stress (6 components: symmetric tensor)
    │       └── Strain Rate Norm (scalar)
    │
    ├── 2. Gather distributed data from MPI processes
    │       └── Each MPI rank processes its local atomic blocks
    │
    ├── 3. Write to HDF5 using parallel MPI-IO
    │       └── Each rank writes its portion using ElementSet selection
    │
    └── 4. Write XDMF metadata (main processor only)
            └── Describes HDF5 structure for visualization tools
```

### Detailed Flow

1. **Computing Physical Quantities** (lines 111-117):
   - `computeVelocity(lattice)` → 3D tensor field with 3 components per cell
   - `computeDensity(lattice)` → 3D scalar field
   - `computeShearStress(lattice)` → 3D tensor field with 6 components (symmetric tensor)
   - `computeSymmetricTensorNorm(...)` → Scalar norm of strain rate

2. **Data Gathering Loop** (lines 139-185):
   - Iterates through local atomic blocks on each MPI rank
   - Converts global coordinates to local coordinates
   - Extracts values and applies unit conversion (lattice units → physical units)
   - Stores coordinates in `GlobalID` and values in separate vectors

3. **HDF5 Writing** (lines 196-267):
   - Uses HighFive library with MPI collective I/O
   - Creates compressed datasets with chunking, shuffle, and deflate
   - Each MPI rank writes its elements using `ElementSet` selection

4. **XDMF Writing** (lines 277-402):
   - Main processor writes XML metadata
   - Describes mesh topology (3D co-rectlinear mesh)
   - Maps HDF5 datasets to visualization attributes

---

## Background Knowledge

### 1. Lattice Boltzmann Method (LBM)

LBM simulates fluid dynamics by tracking particle distribution functions on a discrete lattice. Key concepts:

- **Lattice**: 3D grid where each cell contains distribution functions
- **D3Q19**: 3D lattice with 19 velocity directions per cell
- **Macroscopic quantities**: Derived from distribution functions:
  - Density: ρ = Σf_i (sum of distributions)
  - Velocity: u = (1/ρ) Σf_i * c_i (momentum/density)
  - Stress tensor: Computed from non-equilibrium distributions

### 2. Palabos Library

Palabos is the underlying LBM framework. Key types used:

```cpp
MultiBlockLattice3D<T,DESCRIPTOR>  // Distributed lattice across MPI ranks
MultiTensorField3D<T,N>           // Distributed tensor field (N components)
MultiScalarField3D<T>             // Distributed scalar field
SmartBulk3D                       // Represents atomic block in global coordinates
```

### 3. MPI Parallelization

The simulation domain is decomposed into atomic blocks distributed across MPI ranks:

```
┌─────────┬─────────┬─────────┐
│ Rank 0  │ Rank 1  │ Rank 2  │
│ Block 0 │ Block 3 │ Block 6 │
├─────────┼─────────┼─────────┤
│ Rank 0  │ Rank 1  │ Rank 2  │
│ Block 1 │ Block 4 │ Block 7 │
├─────────┼─────────┼─────────┤
│ Rank 0  │ Rank 1  │ Rank 2  │
│ Block 2 │ Block 5 │ Block 8 │
└─────────┴─────────┴─────────┘
```

Each rank only has access to its local blocks.

### 4. HDF5 and Parallel I/O

HDF5 (Hierarchical Data Format 5) is a file format for large scientific datasets.

**Key concepts:**
- **Dataset**: N-dimensional array stored in file
- **DataSpace**: Describes shape of data (e.g., [100, 200, 300])
- **Selection**: Specifies which elements to read/write
  - **Hyperslab**: Contiguous rectangular region
  - **ElementSet**: List of individual element coordinates

**Parallel HDF5 with MPI:**
```cpp
FileAccessProps fapl;
fapl.add(MPIOFileAccess{MPI_COMM_WORLD, MPI_INFO_NULL});  // Enable MPI-IO
fapl.add(MPIOCollectiveMetadata{});  // Collective metadata operations

// Each rank selects its elements and writes collectively
dataset.select(ElementSet(myCoordinates)).write(myData, xfer_props);
```

### 5. XDMF (eXtensible Data Model and Format)

XDMF is an XML-based format that describes HDF5 data for visualization tools (ParaView, VisIt).

**Structure:**
```xml
<Xdmf>
  <Domain>
    <Grid>
      <Topology>  <!-- Mesh structure -->
      <Geometry>  <!-- Coordinate system -->
      <Attribute> <!-- Data fields -->
        <DataItem> <!-- Link to HDF5 -->
    </Grid>
  </Domain>
</Xdmf>
```

**Attribute Types:**
- `Scalar`: Single value per cell (e.g., density)
- `Vector`: 3 components per cell (e.g., velocity)
- `Tensor6`: 6 components for symmetric 3x3 tensor (e.g., stress)

### 6. Stress Tensor Representation

The symmetric stress tensor has 6 independent components:

```
       ┌ σ_xx  σ_xy  σ_xz ┐     ┌ σ_1  σ_4  σ_5 ┐
σ_ij = │ σ_yx  σ_yy  σ_yz │  =  │ σ_4  σ_2  σ_6 │
       └ σ_zx  σ_zy  σ_zz ┘     └ σ_5  σ_6  σ_3 ┘
```

**Voigt notation** stores these as a 6-element vector:
- [σ_1, σ_2, σ_3, σ_4, σ_5, σ_6] = [σ_xx, σ_yy, σ_zz, σ_xy, σ_xz, σ_yz]

---

## Current Implementation Analysis

### Data Storage (Before)

Currently, each component is stored as a separate 3D scalar dataset:

```
HDF5 File Structure:
├── velocity_x     [Nz × Ny × Nx]  float32
├── velocity_y     [Nz × Ny × Nx]  float32
├── velocity_z     [Nz × Ny × Nx]  float32
├── sigma_1        [Nz × Ny × Nx]  float32
├── sigma_2        [Nz × Ny × Nx]  float32
├── sigma_3        [Nz × Ny × Nx]  float32
├── sigma_4        [Nz × Ny × Nx]  float32
├── sigma_5        [Nz × Ny × Nx]  float32
├── sigma_6        [Nz × Ny × Nx]  float32
├── density        [Nz × Ny × Nx]  float32
├── S_norm         [Nz × Ny × Nx]  float32
├── Field1         [Nz × Ny × Nx]  float32
└── MPI_rank       [Nz × Ny × Nx]  int32
```

**Problems with this approach:**
1. **Redundancy**: 13 separate datasets with repeated structure
2. **Scattered data**: Vector/tensor components stored non-contiguously
3. **Inefficient access**: Reading velocity requires 3 separate reads
4. **Visualization complexity**: Tools must combine components manually

### Memory Layout (Before)

```cpp
vector<float> VelocityX;  // [v0_x, v1_x, v2_x, ...]
vector<float> VelocityY;  // [v0_y, v1_y, v2_y, ...]
vector<float> VelocityZ;  // [v0_z, v1_z, v2_z, ...]
vector<float> SS1, SS2, SS3, SS4, SS5, SS6;  // Same pattern
```

---

## The TODO: Save as Vectors and Matrices

From `io.cpp:104`:
```cpp
// TODO - save as vectors and matrices instead of 3D scalar arrays (also modify xdmf)
```

### Goal

Transform the storage from separate scalar fields to:
- **Velocity**: Single 4D dataset [Nz × Ny × Nx × 3]
- **Shear Stress**: Single 4D dataset [Nz × Ny × Nx × 6]

### Target HDF5 Structure

```
HDF5 File Structure (After):
├── velocity       [Nz × Ny × Nx × 3]  float32   ← Combined!
├── sigma          [Nz × Ny × Nx × 6]  float32   ← Combined!
├── density        [Nz × Ny × Nx]      float32
├── S_norm         [Nz × Ny × Nx]      float32
├── Field1         [Nz × Ny × Nx]      float32
└── MPI_rank       [Nz × Ny × Nx]      int32
```

### Benefits

1. **Semantic clarity**: Velocity is stored as a vector, stress as a tensor
2. **Better memory locality**: Components are adjacent in memory
3. **Fewer I/O operations**: One read for all velocity components
4. **Native visualization**: XDMF `Vector` and `Tensor6` types work directly
5. **Smaller file size**: Less metadata overhead from fewer datasets

---

## Implementation Strategy

### Approach Selection

There are several possible approaches:

| Approach | Description | Pros | Cons |
|----------|-------------|------|------|
| **A. Expanded ElementSet** | Expand coordinate list to include component index | Simple, maintains pattern | 3x/6x more selections |
| **B. Compound Datatype** | HDF5 struct with named fields | Efficient, single selection | More complex, XDMF compatibility issues |
| **C. Hyperslab per point** | Use hyperslab for [k,j,i,:] slice | Efficient for contiguous | Requires sorted coordinates |
| **D. Store as std::array** | HighFive handles vector<array<T,N>> | Natural C++ mapping | May not work with ElementSet |

### Chosen Approach: A - Expanded ElementSet

**Rationale:**
1. **Minimal code changes**: Maintains the existing parallel writing pattern
2. **Proven pattern**: Uses the same ElementSet mechanism that already works
3. **XDMF compatibility**: Standard 4D arrays work with XDMF Vector/Tensor6
4. **Correctness**: Each component is explicitly addressed

### Implementation Details

#### 1. Memory Layout Change

**Before:**
```cpp
vector<float> VelocityX, VelocityY, VelocityZ;
// Separate vectors, one per component
```

**After:**
```cpp
vector<float> Velocity;           // Interleaved: [vx0, vy0, vz0, vx1, vy1, vz1, ...]
vector<vector<unsigned long>> GlobalID_Velocity;  // [[k,j,i,0], [k,j,i,1], [k,j,i,2], ...]
```

#### 2. Data Collection Loop

**Before:**
```cpp
VelocityX.push_back(vx);
VelocityY.push_back(vy);
VelocityZ.push_back(vz);
```

**After:**
```cpp
// Store coordinates with component index
GlobalID_Velocity.push_back({k, j, i, 0});
GlobalID_Velocity.push_back({k, j, i, 1});
GlobalID_Velocity.push_back({k, j, i, 2});
// Store values interleaved
Velocity.push_back(vx);
Velocity.push_back(vy);
Velocity.push_back(vz);
```

#### 3. HDF5 Dataset Creation

**Before:**
```cpp
vector<size_t> Dims{Nz, Ny, Nx};
DataSet velocity_x = file.createDataSet<float>("velocity_x", DataSpace(Dims), props);
DataSet velocity_y = file.createDataSet<float>("velocity_y", DataSpace(Dims), props);
DataSet velocity_z = file.createDataSet<float>("velocity_z", DataSpace(Dims), props);
```

**After:**
```cpp
vector<size_t> VelocityDims{Nz, Ny, Nx, 3};
DataSet velocity = file.createDataSet<float>("velocity", DataSpace(VelocityDims), propsVel);
```

#### 4. HDF5 Writing

**Before:**
```cpp
velocity_x.select(ElementSet(GlobalID)).write(VelocityX, xfer_props);
velocity_y.select(ElementSet(GlobalID)).write(VelocityY, xfer_props);
velocity_z.select(ElementSet(GlobalID)).write(VelocityZ, xfer_props);
```

**After:**
```cpp
velocity.select(ElementSet(GlobalID_Velocity)).write(Velocity, xfer_props);
```

#### 5. Chunking Adjustment

For 4D data, chunking needs the component dimension:
```cpp
// For velocity [Nz, Ny, Nx, 3]
props.add(Chunking({chunk_z, chunk_y, chunk_x, 3}));

// For shear stress [Nz, Ny, Nx, 6]
props.add(Chunking({chunk_z, chunk_y, chunk_x, 6}));
```

#### 6. XDMF Changes

**Before (separate scalars):**
```xml
<Attribute Name="Velocity-X [m/s]" AttributeType="Scalar" Center="Cell">
  <DataItem Dimensions="Nz Ny Nx" NumberType="Float" Precision="4" Format="HDF">
    output.h5:/velocity_x
  </DataItem>
</Attribute>
```

**After (vector):**
```xml
<Attribute Name="Velocity [m/s]" AttributeType="Vector" Center="Cell">
  <DataItem Dimensions="Nz Ny Nx 3" NumberType="Float" Precision="4" Format="HDF">
    output.h5:/velocity
  </DataItem>
</Attribute>
```

**For stress tensor (Tensor6):**
```xml
<Attribute Name="Shear Stress [Pa]" AttributeType="Tensor6" Center="Cell">
  <DataItem Dimensions="Nz Ny Nx 6" NumberType="Float" Precision="4" Format="HDF">
    output.h5:/sigma
  </DataItem>
</Attribute>
```

### Performance Considerations

1. **Memory**: Slightly more memory for expanded GlobalID vectors (4D vs 3D coordinates)
2. **I/O**: Fewer dataset operations, but same total data volume
3. **Compression**: May be slightly better due to component locality

---

## Code Changes

See the actual implementation in `io.cpp`. The key changes are:

1. **Lines ~130-134**: Changed from separate component vectors to:
   - `vector<float> Velocity` (interleaved)
   - `vector<float> ShearStress` (interleaved)
   - `vector<vector<unsigned long>> GlobalID_Velocity` (4D coords)
   - `vector<vector<unsigned long>> GlobalID_ShearStress` (4D coords)

2. **Lines ~154-170**: Data collection loop pushes coordinates with component indices and interleaved values

3. **Lines ~213-218**: New chunk sizes for 4D datasets

4. **Lines ~224-228**: Create 4D datasets instead of multiple 3D datasets

5. **Lines ~249-253**: Single write calls for vector/tensor data

6. **Lines ~322-340**: XDMF uses `AttributeType="Vector"` and `AttributeType="Tensor6"`

---

## Testing Recommendations

1. **Verify HDF5 structure**: Use `h5dump -H output.h5` to check dataset shapes
2. **Check data integrity**: Compare values with old format using h5diff or Python
3. **Visualization test**: Open in ParaView with XDMF, verify vector/tensor display
4. **MPI test**: Run with multiple ranks, verify all data is written correctly
5. **Performance benchmark**: Compare I/O times with old implementation

---

## References

- [HighFive Library](https://github.com/BlueBrain/HighFive)
- [XDMF Specification](https://www.xdmf.org/index.php/XDMF_Model_and_Format)
- [HDF5 Tutorial - Parallel I/O](https://portal.hdfgroup.org/display/HDF5/Parallel+HDF5)
- [Palabos Documentation](https://palabos.unige.ch/)
