# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HemoFlow is a Lattice Boltzmann Method (LBM) fluid dynamics simulator for hemodynamic simulations in vascular networks. It uses the Palabos library for LBM computations with MPI parallelization and sparse domain decomposition.

## Build Commands

```bash
# Standard build
mkdir build && cd build
cmake ..
make -j 4

# With custom HDF5 (required on Ubuntu - needs HDF5 1.13+ parallel)
cmake -DHDF5_ROOT=/opt/hdf5 ..
make -j 4

# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j 4
```

**CMake Options:**
- `BUILD_HDF5=ON` (default): Compile with HDF5 parallel support
- `ENABLE_MPI=ON` (default): Enable MPI parallelization
- `CMAKE_BUILD_TYPE`: Release (default) or Debug

## Running Simulations

```bash
# Single process
./build/hemoFlow testcase/test_SimpleTube_Straight_Freeflow.xml

# With MPI
mpirun -np 4 ./build/hemoFlow testcase/test_SimpleTube_Straight_Freeflow.xml

# Debug executable
./build/hemoFlowd testcase/test_Freeflow.xml
```

Test cases are XML configuration files in `testcase/` directory.

## Architecture

### Core Components

- **hemoFlow.cpp**: Main simulation loop - parses XML config, initializes Palabos lattice, runs time-stepping, writes output
- **opening.cpp/h**: Boundary condition handling for inlets/outlets (velocity, pressure, Murray law, freeflow profiles)
- **io.cpp/h**: Output writers (HDF5 parallel, VTK, NPZ)
- **helper.h**: `SimPar` struct with unit conversion factors (SI ↔ LB units)
- **globals.h**: LBM descriptor configuration (D3Q19 forced dynamics)
- **porous.h**: Porous media (stent) force functionals

### Palabos Integration

Uses Palabos MultiBlock lattice with D3Q19 forced BGK/MRT dynamics. Key concepts:
- Domain decomposition via atomic blocks (configurable `blockSize`)
- Sparse grid support for memory efficiency
- Collision-streaming time-stepping

### Preprocessor (Python)

`preprocessor/main.py` - Voxelizes STL geometry for simulation:
```bash
python preprocessor/main.py config.json
```

Outputs NPZ file with geometry flags:
- 1: wall, 2: fluid, 3: porous, 10: inlet, 11+: outlets

### Geometry Constraints

- Openings must be on axis-aligned bounding box borders
- Openings cannot fall on edges/corners of the bounding box
- Centerline must contain one line per outlet (inlet→outlet paths)

## Configuration

XML config structure (see `template_config.xml`):
- `<simulation>`: dt, blockSize, outputDir, simLength, saveFrequency
- `<geometry>`: NPZ file path, opening definitions (type 1-4: velocity/Murray/pressure/freeflow)
- `<flowdiverter>`: Porous media coefficients for stent modeling

Opening types:
1. Velocity inlet/outlet
2. Murray law (auto-calculated velocity based on vessel radii)
3. Pressure outlet
4. Freeflow (stress-free)

## Dependencies

- **Palabos**: LBM library (clone to `palabos/` or run `setup.sh`)
- **HDF5 1.13+**: Parallel version required (not available via apt, compile manually)
- **HighFive**: HDF5 C++ wrapper (in `external/`)
- **cnpy**: NumPy file I/O (in `external/`)
- **Python 3.7+**: For preprocessor (pynrrd, numpy-stl)
