/*
 * hemoTai.cpp - Minimal MPI application demonstrating MPI patterns from hemoFlow
 *
 * This application demonstrates:
 * - MPI initialization and finalization (via Palabos plbInit)
 * - Parallel HDF5 file I/O using HighFive library
 * - MPI collective operations (barrier, broadcast)
 * - Main processor control flow
 * - Domain decomposition and parallel data writing
 * - Checkpointing pattern
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>
#include <mpi.h>

// HDF5 and HighFive includes
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>
#include <highfive/H5File.hpp>
#include <highfive/H5PropertyList.hpp>
#include <hdf5.h>

// Palabos headers for MPI initialization (matches hemoFlow pattern)
#include "palabos3D.h"
#include "palabos3D.hh"

using namespace plb;
using namespace std;

// Simulation parameters (simplified from hemoFlow)
struct SimParams {
    int Nx, Ny, Nz;          // Domain size
    double dx, dt;            // Physical scales
    int numSteps;             // Total simulation steps
    int saveFrequency;        // Save every N steps
    int checkpointFrequency;  // Checkpoint every N steps
    string outputDir;         // Output directory
};

// Create output filename (matches hemoFlow pattern)
string createFileName(const string& prefix, int iter, int width = 6) {
    ostringstream oss;
    oss << prefix << setfill('0') << setw(width) << iter;
    return oss.str();
}

// Check if file exists
bool fileExists(const string& name) {
    ifstream f(name.c_str());
    return f.good();
}

// Generate fake simulation data for a local domain portion
void generateFakeData(int localSize, vector<float>& velocity_x,
                      vector<float>& velocity_y, vector<float>& velocity_z,
                      vector<float>& pressure, int rank, int step) {
    velocity_x.resize(localSize);
    velocity_y.resize(localSize);
    velocity_z.resize(localSize);
    pressure.resize(localSize);

    for (int i = 0; i < localSize; i++) {
        // Generate simple sinusoidal pattern dependent on rank and step
        double phase = 2.0 * M_PI * i / localSize + 0.1 * step;
        velocity_x[i] = sin(phase) * (rank + 1) * 0.1;
        velocity_y[i] = cos(phase) * (rank + 1) * 0.15;
        velocity_z[i] = sin(phase * 0.5) * (rank + 1) * 0.05;
        pressure[i] = 101325.0 + sin(phase) * 100.0; // Pa
    }
}

// Write HDF5 output using parallel I/O (matches hemoFlow writeHDF5 pattern)
void writeParallelHDF5(const SimParams& sim, int iter,
                       const vector<float>& local_vx,
                       const vector<float>& local_vy,
                       const vector<float>& local_vz,
                       const vector<float>& local_p,
                       const vector<vector<size_t>>& globalIDs,
                       int rank) {

    using namespace HighFive;

    pcout << "Writing HDF5 output at iteration " << iter << endl;

    // FileAccessProps with MPI-IO (matches hemoFlow pattern)
    FileAccessProps fapl;
    fapl.add(MPIOFileAccess{MPI_COMM_WORLD, MPI_INFO_NULL});
    fapl.add(MPIOCollectiveMetadata{});

    // Create output file
    string filename = createFileName(sim.outputDir + "/output_", iter, 6) + ".h5";
    File file(filename, File::Truncate, fapl);

    // Compression properties (matches hemoFlow)
    DataSetCreateProps props;
    props.add(Chunking(vector<hsize_t>{10, 10, 10}));
    props.add(Shuffle());
    props.add(Deflate(7));

    // Create datasets with global domain dimensions
    vector<size_t> dims{(size_t)sim.Nz, (size_t)sim.Ny, (size_t)sim.Nx};
    DataSet ds_vx = file.createDataSet<float>("velocity_x", DataSpace(dims), props);
    DataSet ds_vy = file.createDataSet<float>("velocity_y", DataSpace(dims), props);
    DataSet ds_vz = file.createDataSet<float>("velocity_z", DataSpace(dims), props);
    DataSet ds_p = file.createDataSet<float>("pressure", DataSpace(dims), props);

    // Use collective I/O
    auto xfer_props = DataTransferProps{};
    xfer_props.add(UseCollectiveIO{});

    // Each rank writes its local portion using element selection
    ds_vx.select(ElementSet(globalIDs)).write(local_vx, xfer_props);
    ds_vy.select(ElementSet(globalIDs)).write(local_vy, xfer_props);
    ds_vz.select(ElementSet(globalIDs)).write(local_vz, xfer_props);
    ds_p.select(ElementSet(globalIDs)).write(local_p, xfer_props);

    // Add attributes (metadata)
    ds_vx.createAttribute("units", string("m/s"));
    ds_p.createAttribute("units", string("Pa"));
    file.createAttribute("dx", sim.dx);
    file.createAttribute("dt", sim.dt);
    file.createAttribute("iteration", iter);

    pcout << "  Saved: " << filename << endl;
}

// Save checkpoint (matches hemoFlow checkpointing pattern)
void saveCheckpoint(const SimParams& sim, int iteration, int rank, int size) {
    string chkParamFile = sim.outputDir + "/checkpoint_parameters.dat";
    string chkDataFile = sim.outputDir + "/checkpoint_data.dat";

    // Main processor saves parameters (matches hemoFlow pattern)
    if (global::mpi().isMainProcessor()) {
        ofstream ofile(chkParamFile.c_str());
        ofile << iteration << endl;
        ofile << sim.dx << " " << sim.dt << endl;
        pcout << "Checkpoint saved at iteration " << iteration << endl;
    }

    // All processors participate in data save (simplified)
    // In real hemoFlow, this uses saveBinaryBlock for lattice data
    ofstream dataFile(chkDataFile + "_rank" + to_string(rank) + ".dat", ios::binary);
    dataFile.write(reinterpret_cast<const char*>(&iteration), sizeof(int));
    dataFile.close();

    global::mpi().barrier(); // Synchronize after checkpoint (matches hemoFlow)
}

// Load checkpoint (matches hemoFlow restore pattern)
int loadCheckpoint(const SimParams& sim, int rank) {
    string chkParamFile = sim.outputDir + "/checkpoint_parameters.dat";
    int iteration = 0;

    // Main processor loads and broadcasts (matches hemoFlow pattern)
    if (global::mpi().isMainProcessor()) {
        ifstream ifile(chkParamFile.c_str());
        if (ifile.is_open()) {
            ifile >> iteration;
            pcout << "Loading checkpoint from iteration " << iteration << endl;
        } else {
            pcout << "ERROR: Cannot read checkpoint file!" << endl;
            return -1;
        }
    }

    // Broadcast iteration counter to all ranks (matches hemoFlow pattern)
    global::mpi().bCast(&iteration, 1);

    return iteration;
}

// Main simulation loop
int main(int argc, char* argv[]) {

    // Initialize MPI via Palabos (matches hemoFlow pattern)
    plbInit(&argc, &argv);

    int rank = global::mpi().getRank();
    int size = global::mpi().getSize();

    pcout << "********************************" << endl
          << "*      hemoTai v0.1           *" << endl
          << "*   Minimal MPI+HDF5 Demo     *" << endl
          << "********************************" << endl;

    // Check command line arguments
    bool restartFromCheckpoint = false;
    if (argc > 1) {
        string flag(argv[1]);
        if (flag == "-r") {
            restartFromCheckpoint = true;
            pcout << "Restart from checkpoint requested" << endl;
        }
    }

    // Simulation parameters
    SimParams sim;
    sim.Nx = 32;
    sim.Ny = 32;
    sim.Nz = 64;
    sim.dx = 0.001;  // 1 mm
    sim.dt = 0.0001; // 0.1 ms
    sim.numSteps = 100;
    sim.saveFrequency = 20;
    sim.checkpointFrequency = 30;
    sim.outputDir = "./output_hemoTai";

    pcout << "Domain size: " << sim.Nx << " x " << sim.Ny << " x " << sim.Nz << endl;
    pcout << "MPI processes: " << size << endl;
    pcout << "dx = " << sim.dx << " m, dt = " << sim.dt << " s" << endl;

    // Create output directory (main processor only, matches hemoFlow)
    if (global::mpi().isMainProcessor()) {
        // Create directory (simplified, in real hemoFlow uses mkpath)
        system(("mkdir -p " + sim.outputDir).c_str());
    }

    // Barrier to ensure directory exists before others try to write
    global::mpi().barrier();

    // Domain decomposition: simple 1D decomposition in Z direction
    int totalPoints = sim.Nx * sim.Ny * sim.Nz;
    int localZsize = sim.Nz / size;
    int remainder = sim.Nz % size;

    // Adjust for remainder
    int zStart = rank * localZsize + min(rank, remainder);
    int zEnd = zStart + localZsize + (rank < remainder ? 1 : 0);
    int actualLocalZ = zEnd - zStart;

    int localSize = sim.Nx * sim.Ny * actualLocalZ;

    pcout << "Rank " << rank << ": Z range [" << zStart << ", " << zEnd
          << "), local size = " << localSize << endl;

    // Generate global indices for this rank's portion
    vector<vector<size_t>> globalIDs;
    globalIDs.reserve(localSize);

    for (int z = zStart; z < zEnd; z++) {
        for (int y = 0; y < sim.Ny; y++) {
            for (int x = 0; x < sim.Nx; x++) {
                globalIDs.push_back({(size_t)z, (size_t)y, (size_t)x});
            }
        }
    }

    // Starting iteration
    int startIter = 0;

    // Restore from checkpoint if requested (matches hemoFlow pattern)
    if (restartFromCheckpoint) {
        pcout << endl << "*** Restoring checkpoint ***" << endl;
        startIter = loadCheckpoint(sim, rank);
        if (startIter < 0) {
            pcout << "Checkpoint load failed, exiting" << endl;
            return -1;
        }
        pcout << "Checkpoint restored successfully" << endl << endl;
    }

    // Main simulation loop (matches hemoFlow structure)
    pcout << endl << "*** Starting simulation ***" << endl;

    for (int iter = startIter; iter <= sim.numSteps; iter++) {

        // Progress output every few steps
        if (iter % 10 == 0) {
            pcout << "Iteration: " << iter << " / " << sim.numSteps
                  << " (time = " << iter * sim.dt << " s)" << endl;
        }

        // Generate fake simulation data
        vector<float> vx, vy, vz, p;
        generateFakeData(localSize, vx, vy, vz, p, rank, iter);

        // Save output at specified frequency (matches hemoFlow)
        if (iter % sim.saveFrequency == 0) {
            writeParallelHDF5(sim, iter, vx, vy, vz, p, globalIDs, rank);
        }

        // Save checkpoint at specified frequency (matches hemoFlow)
        if (iter % sim.checkpointFrequency == 0 && iter > 0) {
            saveCheckpoint(sim, iter, rank, size);
        }

        // Simulate some computation time
        // (In real hemoFlow: lattice->collideAndStream())
    }

    pcout << endl << "*** Simulation completed successfully ***" << endl;

    return 0;
}
