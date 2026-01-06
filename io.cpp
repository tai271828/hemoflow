#include "io.h"

// ***********************************
// *** Directory handling routines ***
// ***********************************

// WARNING, not portable! We need portable I/O code in the future.

bool fileExists (const std::string& name) {
    ifstream f(name.c_str());
    return f.good();
}

// Checks for a directory. Hopefuly a portable way. TODO: replace with C++17 method.
int dirExists(const string& pathName)
{
    struct stat info{};

    if( stat( pathName.c_str(), &info ) != 0 )
        return -1; // Cannot acces path
    else if( info.st_mode & S_IFDIR )  // S_ISDIR() doesn't exist on my windows
        return 1;  // Path exists
    else
        return 0;  // Path does not exist
}

/**
** mkpath - ensure all directories in path exist
** Algorithm takes the pessimistic view and works top-down to ensure
** each directory in path exists, rather than optimistically creating
** the last element and working backwards. It uses the custom makedir function below.
*/

// TODO: Unix specific, look for portable solution!
int do_mkdir(const char *path, mode_t mode)
{
    //Stat            st;
    struct stat st = {0};
    int    status = 0;

    if (stat(path, &st) != 0)
    {
        /* Directory does not exist. EEXIST for race condition */
        if (mkdir(path, mode) != 0 && errno != EEXIST)
            status = -1;
    }
    else if (!S_ISDIR(st.st_mode))
    {
        errno = ENOTDIR;
        status = -1;
    }

    return(status);
}

// mkpath(argv[i], 0777);
int mkpath(const char *path, mode_t mode)
{
    char           *pp;
    char           *sp;
    int             status;
    char           *copypath = strdup(path);

    status = 0;
    pp = copypath;
    while (status == 0 && (sp = strchr(pp, '/')) != 0)
    {
        if (sp != pp)
        {
            /* Neither root nor double slash in path */
            *sp = '\0';
            status = do_mkdir(copypath, mode);
            *sp = '/';
        }
        pp = sp + 1;
    }
    if (status == 0)
        status = do_mkdir(path, mode);
    free(copypath);
    return (status);
}


// ****************************
// *** Data saving routines ***
// ****************************

// Write out data in vtk format
void writeVTK(MultiBlockLattice3D<T,DESCRIPTOR>& lattice, const SimPar &sim, plint iter, MultiNTensorField3D<T> *field1)
{
    VtkImageOutput3D<T> vtkOut(createFileName("vtk", iter, 6), sim.C_l);
    vtkOut.writeData<float>(*computeDensity(lattice), "density [Pa]", 1./3. * sim.C_p );
    vtkOut.writeData<3,float>(*computeVelocity(lattice), "velocity [m/s]", sim.C_l/sim.C_t);
    vtkOut.writeData<6,float>(*computeShearStress(lattice), "sigma [1/m2s]", 1./(sim.C_l*sim.C_t*sim.C_t));
    vtkOut.writeData<float>(*computeSymmetricTensorNorm(*computeStrainRateFromStress(lattice)), "S_norm [1/s]", 1./sim.C_t );
    // TODO - output viscosity?

    if (field1 != nullptr)
       vtkOut.writeData<float>(*field1, "field1");
}

// Optimized: Uses hyperslab selection per block for contiguous I/O instead of ElementSet
// Optimized: Pre-allocates buffers and pre-computes scaling factors
// Optimized: Adaptive chunk size based on dataset dimensions
// Note: MPI rank is still saved per lattice point for debugging/visualization purposes
// TODO - save as vectors and matrices instead of 3D scalar arrays (also modify xdmf) - https://github.com/BlueBrain/HighFive/blob/master/src/examples/create_dataset_double.cpp
void writeHDF5(MultiBlockLattice3D<T,DESCRIPTOR>& lattice, const SimPar &sim, plint iter, string outDir, MultiNTensorField3D<T> *field1)
{

    T SaveTime = T();
    global::timer("SaveTime").restart();

    // Compute velocity in 3 dims, shear stress in 6 dims
    // Note the velovities are distributed on every processor
    MultiTensorField3D<double,3> DistributedVelocity = *computeVelocity(lattice);
    MultiScalarField3D<double> DistributedDensity = *computeDensity(lattice);
    MultiTensorField3D<double,6> DistributedShearStress = *computeShearStress(lattice);
    MultiScalarField3D<double> DistributedS_Norm = *computeSymmetricTensorNorm(*computeStrainRateFromStress(lattice));
    // MultiScalarField3D<double> DistributedField1 = *field1; // Used for additional fields, e.g. porosity, do any necessary calculations here.

    // Density/Velocity/... shared the same atomic block distribution!
    MultiBlockManagement3D VelocityBlockManagement = DistributedVelocity.getMultiBlockManagement();

    vector<plint> LocalBlockIDs = VelocityBlockManagement.getLocalInfo().getBlocks();

    ///////////////////////////// Saving HDF5 /////////////////////////////

    // Now save the partial local data to hdf5 using hyperslab selection per block
    // This is much faster than ElementSet because it uses contiguous memory access
    // See: https://github.com/BlueBrain/HighFive/blob/master/src/examples/parallel_hdf5_collective_io.cpp
    using namespace HighFive;

    FileAccessProps fapl;
    // Tell HDF5 to use MPI-IO
    fapl.add(MPIOFileAccess{MPI_COMM_WORLD, MPI_INFO_NULL});
    // Specify that we want all meta-data related operations to use MPI collective operations,
    // that is, all MPI ranks must participate in any HDF5 operations.
    fapl.add(MPIOCollectiveMetadata{});

    // Create the file as usual.
    std::string file_name = createFileName(outDir + "/output_", iter, 6);
    File file(file_name + ".h5", File::Truncate, fapl);

    // For compression
    DataSetCreateProps props;
    // Use adaptive chunking based on dataset dimensions
    // Chunk size should be <= dimension size
    hsize_t chunk_x = std::min<hsize_t>(Nx, 100);
    hsize_t chunk_y = std::min<hsize_t>(Ny, 100);
    hsize_t chunk_z = std::min<hsize_t>(Nz, 100);
    // The order matters if chunking is not cubic
    props.add(Chunking(std::vector<hsize_t>{chunk_z, chunk_y, chunk_x}));
    // Enable shuffle
    props.add(Shuffle());
    // Enable deflate
    props.add(Deflate(7));

    // Create the datasets
    std::vector<size_t> Dims{(long unsigned int)Nz, (long unsigned int)Ny, (long unsigned int)Nx};
    DataSet velocity_x = file.createDataSet<float>("velocity_x", DataSpace(Dims), props);
    DataSet velocity_y = file.createDataSet<float>("velocity_y", DataSpace(Dims), props);
    DataSet velocity_z = file.createDataSet<float>("velocity_z", DataSpace(Dims), props);
    // Shear Stress
    DataSet SS_1 = file.createDataSet<float>("sigma_1", DataSpace(Dims), props);
    DataSet SS_2 = file.createDataSet<float>("sigma_2", DataSpace(Dims), props);
    DataSet SS_3 = file.createDataSet<float>("sigma_3", DataSpace(Dims), props);
    DataSet SS_4 = file.createDataSet<float>("sigma_4", DataSpace(Dims), props);
    DataSet SS_5 = file.createDataSet<float>("sigma_5", DataSpace(Dims), props);
    DataSet SS_6 = file.createDataSet<float>("sigma_6", DataSpace(Dims), props);
    // Density
    DataSet density = file.createDataSet<float>("density", DataSpace(Dims), props);
    // S_Norm
    DataSet S_Norm = file.createDataSet<float>("S_norm", DataSpace(Dims), props);
    // Field1
    DataSet Field1_data = file.createDataSet<float>("Field1", DataSpace(Dims), props);
    // MPI rank
    DataSet Rank = file.createDataSet<int>("MPI_rank", DataSpace(Dims), props);

    auto xfer_props = DataTransferProps{};
    xfer_props.add(UseCollectiveIO{});

    // Start to count the data extraction and writing time
    T FindAttributesTime = T();
    global::timer("FindAttributes").restart();

    int RankID = global::mpi().getRank();

    // Pre-compute scaling factors once (moved outside inner loop)
    const float vel_scale = float(sim.C_l/sim.C_t);
    const float SS_scale = float(sim.C_m / (sim.C_l*sim.C_t*sim.C_t));
    const float S_norm_scale = float(1./sim.C_t);

    // Process each block and write using hyperslab selection (contiguous memory layout)
    // This is much faster than ElementSet because:
    // 1. Each block is a contiguous region in the global domain
    // 2. Hyperslab selection uses contiguous I/O patterns
    // 3. Lower memory peak (one block at a time vs all blocks)
    for(long blockId : LocalBlockIDs) {
        SmartBulk3D LocalBulk(VelocityBlockManagement.getSparseBlockStructure(), VelocityBlockManagement.getEnvelopeWidth(), blockId);

        // Block dimensions in global coordinates
        unsigned int x0 = LocalBulk.getBulk().x0;
        unsigned int y0 = LocalBulk.getBulk().y0;
        unsigned int z0 = LocalBulk.getBulk().z0;
        unsigned int x1 = LocalBulk.getBulk().x1;
        unsigned int y1 = LocalBulk.getBulk().y1;
        unsigned int z1 = LocalBulk.getBulk().z1;

        size_t block_nx = x1 - x0 + 1;
        size_t block_ny = y1 - y0 + 1;
        size_t block_nz = z1 - z0 + 1;
        size_t blockSize = block_nx * block_ny * block_nz;

        // Pre-allocate contiguous buffers for this block
        vector<float> VelocityX(blockSize), VelocityY(blockSize), VelocityZ(blockSize);
        vector<float> SS1(blockSize), SS2(blockSize), SS3(blockSize);
        vector<float> SS4(blockSize), SS5(blockSize), SS6(blockSize);
        vector<float> DensityBuf(blockSize), SNormBuf(blockSize), Field1Buf(blockSize);
        vector<int> RankBuf(blockSize, RankID);

        // Fill buffers with data in HDF5 row-major order
        // Dataset dims are (Nz, Ny, Nx), so x changes fastest (innermost loop)
        // Linear index: idx = z_local * (ny * nx) + y_local * nx + x_local
        for(unsigned int k = z0; k <= z1; k++) {
            for(unsigned int j = y0; j <= y1; j++) {
                for(unsigned int i = x0; i <= x1; i++) {
                    unsigned int LocalX = LocalBulk.toLocalX(i);
                    unsigned int LocalY = LocalBulk.toLocalY(j);
                    unsigned int LocalZ = LocalBulk.toLocalZ(k);

                    // Compute linear index in row-major order (z slowest, x fastest)
                    size_t idx = (size_t)(k - z0) * block_ny * block_nx +
                                 (size_t)(j - y0) * block_nx +
                                 (size_t)(i - x0);

                    // Velocity
                    Array<double,3> const& foundVelocity = DistributedVelocity.getComponent(blockId).get(LocalX, LocalY, LocalZ);
                    VelocityX[idx] = float(foundVelocity[0]) * vel_scale;
                    VelocityY[idx] = float(foundVelocity[1]) * vel_scale;
                    VelocityZ[idx] = float(foundVelocity[2]) * vel_scale;

                    // Density
                    DensityBuf[idx] = float(DistributedDensity.getComponent(blockId).get(LocalX, LocalY, LocalZ));

                    // Shear Stress
                    Array<double,6> const& foundSS = DistributedShearStress.getComponent(blockId).get(LocalX, LocalY, LocalZ);
                    SS1[idx] = float(foundSS[0]) * SS_scale;
                    SS2[idx] = float(foundSS[1]) * SS_scale;
                    SS3[idx] = float(foundSS[2]) * SS_scale;
                    SS4[idx] = float(foundSS[3]) * SS_scale;
                    SS5[idx] = float(foundSS[4]) * SS_scale;
                    SS6[idx] = float(foundSS[5]) * SS_scale;

                    // S_Norm
                    SNormBuf[idx] = float(DistributedS_Norm.getComponent(blockId).get(LocalX, LocalY, LocalZ)) * S_norm_scale;

                    // Additional field
                    if (field1 != nullptr) {
                        Field1Buf[idx] = float(*field1->getComponent(blockId).get(LocalX, LocalY, LocalZ));
                    } else {
                        Field1Buf[idx] = 0.0f;
                    }
                }
            }
        }

        // Define hyperslab selection for this block
        // HDF5 uses row-major order: offset and count are in (z, y, x) order
        std::vector<size_t> offset{z0, y0, x0};
        std::vector<size_t> count{block_nz, block_ny, block_nx};

        // Reshape buffers to 3D for hyperslab write
        // HighFive expects data in the same shape as the selection
        // Since we filled in x-y-z order with z fastest, reshape to (nz, ny, nx)

        // Write each dataset using hyperslab selection (contiguous I/O)
        velocity_x.select(offset, count).write_raw(VelocityX.data(), xfer_props);
        velocity_y.select(offset, count).write_raw(VelocityY.data(), xfer_props);
        velocity_z.select(offset, count).write_raw(VelocityZ.data(), xfer_props);
        SS_1.select(offset, count).write_raw(SS1.data(), xfer_props);
        SS_2.select(offset, count).write_raw(SS2.data(), xfer_props);
        SS_3.select(offset, count).write_raw(SS3.data(), xfer_props);
        SS_4.select(offset, count).write_raw(SS4.data(), xfer_props);
        SS_5.select(offset, count).write_raw(SS5.data(), xfer_props);
        SS_6.select(offset, count).write_raw(SS6.data(), xfer_props);
        density.select(offset, count).write_raw(DensityBuf.data(), xfer_props);
        S_Norm.select(offset, count).write_raw(SNormBuf.data(), xfer_props);
        Field1_data.select(offset, count).write_raw(Field1Buf.data(), xfer_props);
        Rank.select(offset, count).write_raw(RankBuf.data(), xfer_props);
    }

    FindAttributesTime = global::timer("FindAttributes").stop();
    pcout << "Data extraction and HDF5 write time: " << FindAttributesTime << " sec" << endl;

    global::mpi().barrier();

    SaveTime = global::timer("SaveTime").stop();
    pcout << "Saving HDF5 time: " << SaveTime << " sec" << endl;

    T XDMFtime = T();
    global::timer("XDMFTime").restart();

    ///////////////////////////// Writing Xdmf /////////////////////////////
    if (global::mpi().isMainProcessor())
    {
        FILE *xmf = nullptr;

        /*
        * Open the file and write the header.
        */
        std::string xmf_name = createFileName(outDir + "/output_", iter, 6) + ".xmf";
        xmf = fopen(xmf_name.c_str(), "w");

        // HDF5 name
        // Find the last occurrence of the directory separator '/'
        size_t lastSlash = file_name.find_last_of('/');
        // Return the substring after the last '/'
        std::string h5_name = file_name.substr(lastSlash + 1);

        fprintf(xmf, "<?xml version=\"1.0\" ?>\n");
        fprintf(xmf, "<!DOCTYPE Xdmf SYSTEM \"Xdmf.dtd\" []>\n");
        fprintf(xmf, "<Xdmf Version=\"2.0\">\n");

        /*
        * Write the mesh description and the variables defined on the mesh.
        */
        fprintf(xmf, " <Domain>\n");

        fprintf(xmf, "   <Grid Name=\"mesh\" GridType=\"Uniform\">\n");
        // Regular mesh
        fprintf(xmf, "     <Topology TopologyType=\"3DCoRectMesh\" NumberOfElements=\"%d %d %d\"/>\n", Nz, Ny, Nx);
        fprintf(xmf, "     <Geometry GeometryType=\"Origin_DxDyDz\">\n");
        fprintf(xmf, "       <DataItem Name=\"Origin\" Dimensions=\"%d\" NumberType=\"Float\" Precision=\"4\" Format=\"XML\">\n", 3);
        fprintf(xmf, "          0 0 0\n");
        fprintf(xmf, "       </DataItem>\n");
        // Discretization step size
        fprintf(xmf, "       <DataItem Name=\"Spacing\" Dimensions=\"%d\" NumberType=\"Float\" Precision=\"4\" Format=\"XML\">\n", 3);
        fprintf(xmf, "          %f %f %f\n", sim.C_l, sim.C_l, sim.C_l);
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Geometry>\n");
        fprintf(xmf, "     \n");
        // Density
        fprintf(xmf, "     <Attribute Name=\"Density [Pa]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/density\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        // Velocities
        fprintf(xmf, "     <Attribute Name=\"Velocity-X [m/s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/velocity_x\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     <Attribute Name=\"Velocity-Y [m/s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/velocity_y\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     <Attribute Name=\"Velocity-Z [m/s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/velocity_z\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        // Shear Stress
        fprintf(xmf, "     <Attribute Name=\"Shear Stress 1 [1/m2s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma_1\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     <Attribute Name=\"Shear Stress 2 [1/m2s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma_2\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     <Attribute Name=\"Shear Stress 3 [1/m2s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma_3\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        fprintf(xmf, "     <Attribute Name=\"Shear Stress 4 [1/m2s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma_4\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     <Attribute Name=\"Shear Stress 5 [1/m2s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma_5\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     <Attribute Name=\"Shear Stress 6 [1/m2s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma_6\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        // S_Norm
        fprintf(xmf, "     <Attribute Name=\"S_Norm [1/s]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/S_norm\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        // Field1
        fprintf(xmf, "     <Attribute Name=\"Additional Field [-]\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/Field1\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        // MPI Rank
        fprintf(xmf, "     <Attribute Name=\"MPI Rank\" AttributeType=\"Scalar\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d\" NumberType=\"Int\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/MPI_rank\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");

        fprintf(xmf, "   </Grid>\n");
        fprintf(xmf, " </Domain>\n");

        /*
        * Write the footer and close the file.
        */
        fprintf(xmf, "</Xdmf>\n");
        fclose(xmf);
    }

    global::mpi().barrier();

    XDMFtime = global::timer("XDMFTime").stop();
    pcout << "Saving XDMF time: " << XDMFtime << " sec" << endl;

}

void writeNPZ(MultiBlockLattice3D<T,DESCRIPTOR>& lattice, plint iter)
{
    Box3D bb = lattice.getBoundingBox();
    long unsigned int nx = bb.getNx();
    long unsigned int ny = bb.getNy();
    long unsigned int nz = bb.getNz();

    TensorField3D<T,3> localVelocity(nx, ny, nz);
    copySerializedBlock(*computeVelocity(lattice), localVelocity);

    if(global::mpi().isMainProcessor()) {
        double *data = new double[3*nx*ny*nz];

        for(unsigned int i = 0; i < nx; i++)
            for(unsigned int j = 0; j < ny; j++)
                for(unsigned int k = 0; k < nz; k++) {
                int idx = (i*nx*nz+j*nz+k)*3;

                data[idx]   = localVelocity.get(i, j, k)[0];
                data[idx+1] = localVelocity.get(i, j, k)[1];
                data[idx+2] = localVelocity.get(i, j, k)[2];
            }

        cnpy::npz_save(createFileName("output_", iter, 6) + ".npz", "velocity",&data[0],{3,nz,ny,nx},"w");
    }

    global::mpi().barrier();
}
