# hemoTai - Minimal MPI Application Demo

## Overview

`hemoTai.cpp` is a minimal MPI application that demonstrates the MPI usage patterns and HDF5 I/O from `hemoFlow.cpp`. It serves as a simplified reference for understanding:

- MPI initialization and communication patterns
- Parallel HDF5 file I/O using the HighFive library
- Domain decomposition strategies
- Checkpointing and restart mechanisms
- Main processor control flow

## Key Features Demonstrated

### 1. MPI Patterns from hemoFlow

```cpp
// Initialize MPI via Palabos (matches hemoFlow)
plbInit(&argc, &argv);

// Main processor control
if (global::mpi().isMainProcessor()) {
    // Directory creation, parameter file writing
}

// Broadcast to all ranks
global::mpi().bCast(&iteration, 1);

// Synchronization barrier
global::mpi().barrier();
```

### 2. Parallel HDF5 I/O

```cpp
// MPI-IO setup
FileAccessProps fapl;
fapl.add(MPIOFileAccess{MPI_COMM_WORLD, MPI_INFO_NULL});
fapl.add(MPIOCollectiveMetadata{});

// Collective I/O
auto xfer_props = DataTransferProps{};
xfer_props.add(UseCollectiveIO{});

// Each rank writes its portion
dataset.select(ElementSet(globalIDs)).write(localData, xfer_props);
```

### 3. Domain Decomposition

- Simple 1D decomposition in Z direction
- Each MPI rank handles a portion of the domain
- Global indices computed for each local point

### 4. Checkpointing Pattern

- Main processor saves parameters
- All ranks save their local data
- Checkpoint files can be used to restart simulation

### 5. Simulation Loop Structure

Matches hemoFlow's main loop:
1. Progress reporting
2. Data generation (replaces lattice->collideAndStream())
3. Periodic output saving
4. Periodic checkpointing

## Building

### Prerequisites

- MPI implementation (OpenMPI, MPICH, etc.)
- HDF5 with parallel support
- Palabos library (for MPI wrapper)
- HighFive header-only library
- C++14 compatible compiler

### Compilation

1. **Adjust paths in `Makefile.hemoTai`**:
   ```makefile
   PALABOS_ROOT = ../palabos
   HDF5_INCLUDE = -I/usr/include/hdf5/openmpi
   HIGHFIVE_INCLUDE = -I../external/HighFive/include
   ```

2. **Build**:
   ```bash
   make -f Makefile.hemoTai
   ```

3. **Alternative: Manual compilation**:
   ```bash
   mpic++ -std=c++14 -O3 \
     -I../palabos/src \
     -I../external/HighFive/include \
     -I/usr/include/hdf5/openmpi \
     hemoTai.cpp \
     ../palabos/src/libpalabos.a \
     -lhdf5 -lhdf5_hl \
     -o hemoTai
   ```

## Running

### Basic run (4 MPI processes):
```bash
mpirun -np 4 ./hemoTai
```

Or using Makefile:
```bash
make -f Makefile.hemoTai run
```

### Restart from checkpoint:
```bash
mpirun -np 4 ./hemoTai -r
```

Or:
```bash
make -f Makefile.hemoTai run-restart
```

## Output

The application creates:

### HDF5 Output Files
- Location: `./output_hemoTai/`
- Format: `output_NNNNNN.h5`
- Contains:
  - `velocity_x`, `velocity_y`, `velocity_z` (m/s)
  - `pressure` (Pa)
  - Metadata: dx, dt, iteration

### Checkpoint Files
- `checkpoint_parameters.dat` - iteration number, grid spacing
- `checkpoint_data_rankN.dat` - per-rank binary data

## Examining Output

### Using h5dump:
```bash
h5dump output_hemoTai/output_000000.h5
```

### Using Python with h5py:
```python
import h5py
import numpy as np

with h5py.File('output_hemoTai/output_000000.h5', 'r') as f:
    print("Datasets:", list(f.keys()))
    vx = f['velocity_x'][:]
    print("Velocity X shape:", vx.shape)
    print("dx =", f.attrs['dx'])
```

### Using ParaView (via XDMF):
Create an XDMF wrapper file to load HDF5 in ParaView (similar to hemoFlow's approach).

## Code Structure

### Main Components

1. **SimParams struct** - Simulation parameters (domain size, time steps, I/O settings)
2. **generateFakeData()** - Creates dummy simulation data (replaces physics computation)
3. **writeParallelHDF5()** - Parallel HDF5 output using HighFive
4. **saveCheckpoint() / loadCheckpoint()** - Checkpoint I/O
5. **main()** - Main simulation loop

### Differences from hemoFlow

| Feature | hemoFlow | hemoTai |
|---------|----------|---------|
| Physics | Full LBM solver | Fake sinusoidal data |
| Domain | MultiBlockLattice3D | Simple array |
| Decomposition | Palabos automatic | Manual 1D in Z |
| Output | 10+ variables | 4 variables (velocity + pressure) |
| Size | ~600 lines | ~300 lines |

## Parameters

Default simulation settings (in code):

```cpp
sim.Nx = 32;             // X grid points
sim.Ny = 32;             // Y grid points
sim.Nz = 64;             // Z grid points
sim.dx = 0.001;          // 1 mm voxel size
sim.dt = 0.0001;         // 0.1 ms time step
sim.numSteps = 100;      // Total iterations
sim.saveFrequency = 20;  // Save every 20 steps
sim.checkpointFrequency = 30;  // Checkpoint every 30 steps
```

Modify these in `main()` to test different scenarios.

## Testing Scenarios

### 1. Quick test (small domain, few steps):
```cpp
sim.Nx = sim.Ny = 16;
sim.Nz = 32;
sim.numSteps = 20;
```

### 2. Checkpoint/restart test:
```bash
# Run and let it checkpoint
mpirun -np 4 ./hemoTai

# Restart from checkpoint
mpirun -np 4 ./hemoTai -r
```

### 3. Scaling test:
```bash
# Test with different numbers of processes
for np in 1 2 4 8; do
  echo "Testing with $np processes"
  mpirun -np $np ./hemoTai
done
```

## Common Issues

### HDF5 Library Not Found
```
error while loading shared libraries: libhdf5.so
```
**Solution**: Add HDF5 lib path to `LD_LIBRARY_PATH`:
```bash
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/hdf5/openmpi:$LD_LIBRARY_PATH
```

### Palabos Headers Not Found
```
fatal error: palabos3D.h: No such file or directory
```
**Solution**: Build Palabos first and adjust `PALABOS_ROOT` in Makefile.

### MPI Initialization Error
```
MPI_Init has already been called
```
**Solution**: Ensure you're using `plbInit()` not `MPI_Init()` directly.

## Extending hemoTai

### Add More Variables
```cpp
// In writeParallelHDF5()
DataSet ds_new = file.createDataSet<float>("new_var", DataSpace(dims), props);
ds_new.select(ElementSet(globalIDs)).write(local_new, xfer_props);
```

### Change Domain Decomposition
```cpp
// Current: 1D in Z
// Extend to 2D (Y-Z) or 3D decomposition
```

### Add Physics
```cpp
// Replace generateFakeData() with actual computation
// E.g., simple diffusion equation solver
```

## Comparison with hemoFlow

This minimal application preserves the essential MPI and I/O patterns while removing:
- Palabos lattice structures
- Physical modeling (LBM, Carreau viscosity)
- Opening/boundary condition handling
- XDMF metadata generation
- VTK and NPZ output formats

It's ideal for:
- Learning MPI-HDF5 I/O patterns
- Testing build environments
- Debugging MPI issues
- Prototyping new I/O strategies

## References

- hemoFlow main code: `hemoFlow.cpp`
- HighFive documentation: https://github.com/BlueBrain/HighFive
- HDF5 parallel I/O guide: https://portal.hdfgroup.org/display/HDF5/Parallel+HDF5
- Palabos documentation: https://palabos.unige.ch/

## License

Same as hemoFlow project (check main project LICENSE).
