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

// TODO - too slow, optimize the arrays (MPI rank is now saved in every lattice?).
// TODO - Optimize chunk size.
// DONE - velocity saved as 4D vector [Nz,Ny,Nx,3], shear stress as 4D tensor [Nz,Ny,Nx,6] with XDMF Vector/Tensor6 attributes
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

    // Start to count the writing time
    T FindAttributesTime = T();
    global::timer("FindAttributes").restart();

    // GlobalID for 3D scalar arrays (density, S_norm, Field1, MPI_rank)
    vector<vector<long unsigned int>> GlobalID;
    // GlobalID for 4D velocity array [Nz, Ny, Nx, 3] - includes component index
    vector<vector<long unsigned int>> GlobalID_Velocity;
    // GlobalID for 4D shear stress array [Nz, Ny, Nx, 6] - includes component index
    vector<vector<long unsigned int>> GlobalID_ShearStress;

    // Velocity stored as interleaved vector [vx0, vy0, vz0, vx1, vy1, vz1, ...]
    vector<float> Velocity;
    // Shear stress stored as interleaved tensor [s1_0, s2_0, ..., s6_0, s1_1, s2_1, ...]
    vector<float> ShearStress;
    // Scalars remain as before
    vector<float> Density;
    vector<float> SNorm;
    vector<float> Field1;
    vector<int> this_rank;

    int RankID = global::mpi().getRank();

    // Now we loop through all local blocks on current MPI thread
    for(long blockId : LocalBlockIDs) {
        // The "SmartBulk3D" object represents local atomic block in a global view, i.e. its bounding box coordinates are in global scale.
        // If you do not understand, go check the source codes of "MultiBlockManagement3D::findAllLocalRepresentations()"
        // Why we use it? Because we need to know which atomic blocks are stored on current MPI thread!
        SmartBulk3D LocalBulk(VelocityBlockManagement.getSparseBlockStructure(), VelocityBlockManagement.getEnvelopeWidth(), blockId);

        for(unsigned int i = LocalBulk.getBulk().x0; i <= LocalBulk.getBulk().x1; i++)
            for(unsigned int j = LocalBulk.getBulk().y0; j <= LocalBulk.getBulk().y1; j++)
                for(unsigned int k = LocalBulk.getBulk().z0; k <= LocalBulk.getBulk().z1; k++){

                    // Now we convert the global scale coordinates to block local coordinates
                    unsigned int LocalX = LocalBulk.toLocalX(i);
                    unsigned int LocalY = LocalBulk.toLocalY(j);
                    unsigned int LocalZ = LocalBulk.toLocalZ(k);

                    // Store 3D coordinates for scalar fields
                    GlobalID.push_back({k,j,i});

                    // Velocity - stored as 4D array [Nz, Ny, Nx, 3]
                    Array<double,3> const& foundVelocity = DistributedVelocity.getComponent(blockId).get(LocalX, LocalY, LocalZ);
                    // Note: Scale to physical unit before saving
                    float vel_scale = float(sim.C_l/sim.C_t);
                    // Push 4D coordinates (k, j, i, component) for each velocity component
                    GlobalID_Velocity.push_back({k, j, i, 0});
                    GlobalID_Velocity.push_back({k, j, i, 1});
                    GlobalID_Velocity.push_back({k, j, i, 2});
                    // Push interleaved velocity values
                    Velocity.push_back(float(foundVelocity[0])*vel_scale);
                    Velocity.push_back(float(foundVelocity[1])*vel_scale);
                    Velocity.push_back(float(foundVelocity[2])*vel_scale);

                    // Density (scalar - uses 3D GlobalID)
                    double foundDensity = DistributedDensity.getComponent(blockId).get(LocalX, LocalY, LocalZ);
                    Density.push_back(float(foundDensity));

                    // Shear Stress - stored as 4D array [Nz, Ny, Nx, 6]
                    Array<double,6> const& foundSS = DistributedShearStress.getComponent(blockId).get(LocalX, LocalY, LocalZ);
                    float SS_scale = sim.C_m / (sim.C_l*sim.C_t*sim.C_t);
                    // Push 4D coordinates (k, j, i, component) for each stress component
                    for (int c = 0; c < 6; c++) {
                        GlobalID_ShearStress.push_back({k, j, i, (long unsigned int)c});
                        ShearStress.push_back(float(foundSS[c])*SS_scale);
                    }
                    // S_Norm
                    double foundS_Norm = DistributedS_Norm.getComponent(blockId).get(LocalX, LocalY, LocalZ);
                    SNorm.push_back(foundS_Norm*float(1./sim.C_t));
                    // Additional field - No unit conversion!
                    if (field1 != nullptr) {
                        double foundField1 = *field1->getComponent(blockId).get(LocalX, LocalY, LocalZ);
                        Field1.push_back(foundField1);
                    }
                    else {
                        Field1.push_back(0.0);
                    }
                    // Rank of current mpi thread
                    this_rank.push_back(RankID);

                }
    }

    FindAttributesTime = global::timer("FindAttributes").stop();
    pcout << "Finding attributes time: " << FindAttributesTime << " sec" << endl;

    assert(!GlobalID.empty());

    ///////////////////////////// Saving HDF5 /////////////////////////////

    // Now save the partial local data to hdf5, if you dont understand,
    // check (https://github.com/BlueBrain/HighFive/blob/master/src/examples/parallel_hdf5_collective_io.cpp)
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

    // For compression - base chunk sizes
    hsize_t chunk_x = std::min<hsize_t>(Nx, 100);
    hsize_t chunk_y = std::min<hsize_t>(Ny, 100);
    hsize_t chunk_z = std::min<hsize_t>(Nz, 100);

    // Properties for 3D scalar datasets (density, S_norm, Field1, MPI_rank)
    DataSetCreateProps props3D;
    props3D.add(Chunking(std::vector<hsize_t>{chunk_z, chunk_y, chunk_x}));
    props3D.add(Shuffle());
    props3D.add(Deflate(7));

    // Properties for 4D velocity dataset [Nz, Ny, Nx, 3]
    DataSetCreateProps propsVelocity;
    propsVelocity.add(Chunking(std::vector<hsize_t>{chunk_z, chunk_y, chunk_x, 3}));
    propsVelocity.add(Shuffle());
    propsVelocity.add(Deflate(7));

    // Properties for 4D shear stress dataset [Nz, Ny, Nx, 6]
    DataSetCreateProps propsShearStress;
    propsShearStress.add(Chunking(std::vector<hsize_t>{chunk_z, chunk_y, chunk_x, 6}));
    propsShearStress.add(Shuffle());
    propsShearStress.add(Deflate(7));

    // Dimensions for scalar fields (3D)
    std::vector<size_t> Dims{(long unsigned int)Nz, (long unsigned int)Ny, (long unsigned int)Nx};
    // Dimensions for velocity (4D vector)
    std::vector<size_t> VelocityDims{(long unsigned int)Nz, (long unsigned int)Ny, (long unsigned int)Nx, 3};
    // Dimensions for shear stress (4D tensor - 6 components for symmetric tensor)
    std::vector<size_t> ShearStressDims{(long unsigned int)Nz, (long unsigned int)Ny, (long unsigned int)Nx, 6};

    // Create velocity dataset as a single 4D vector field
    DataSet velocity = file.createDataSet<float>("velocity", DataSpace(VelocityDims), propsVelocity);
    // Create shear stress dataset as a single 4D tensor field (symmetric, 6 components)
    DataSet sigma = file.createDataSet<float>("sigma", DataSpace(ShearStressDims), propsShearStress);
    // Density (scalar)
    DataSet density = file.createDataSet<float>("density", DataSpace(Dims), props3D);
    // S_Norm (scalar)
    DataSet S_Norm = file.createDataSet<float>("S_norm", DataSpace(Dims), props3D);
    // Field1 (scalar)
    DataSet Field1_data = file.createDataSet<float>("Field1", DataSpace(Dims), props3D);
    // MPI rank (scalar)
    DataSet Rank = file.createDataSet<int>("MPI_rank", DataSpace(Dims), props3D);

    auto xfer_props = DataTransferProps{};
    xfer_props.add(UseCollectiveIO{});

    // Each process writes the local attributes to the file
    // Velocity - write as 4D vector using expanded element selection
    velocity.select(ElementSet(GlobalID_Velocity)).write(Velocity, xfer_props);
    // Shear Stress - write as 4D tensor using expanded element selection
    sigma.select(ElementSet(GlobalID_ShearStress)).write(ShearStress, xfer_props);
    // Scalar fields use 3D element selection
    density.select(ElementSet(GlobalID)).write(Density, xfer_props);
    S_Norm.select(ElementSet(GlobalID)).write(SNorm, xfer_props);
    Field1_data.select(ElementSet(GlobalID)).write(Field1, xfer_props);
    Rank.select(ElementSet(GlobalID)).write(this_rank, xfer_props);

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
        // Velocity - stored as a 4D vector field [Nz, Ny, Nx, 3]
        fprintf(xmf, "     <Attribute Name=\"Velocity [m/s]\" AttributeType=\"Vector\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d 3\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/velocity\n", h5_name.c_str());
        fprintf(xmf, "       </DataItem>\n");
        fprintf(xmf, "     </Attribute>\n");
        fprintf(xmf, "     \n");
        // Shear Stress - stored as a 4D symmetric tensor field [Nz, Ny, Nx, 6]
        // Tensor6 format: xx, yy, zz, xy, xz, yz (symmetric 3x3 tensor with 6 independent components)
        fprintf(xmf, "     <Attribute Name=\"Shear Stress [Pa]\" AttributeType=\"Tensor6\" Center=\"Cell\">\n");
        fprintf(xmf, "       <DataItem Dimensions=\"%d %d %d 6\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">\n", Nz, Ny, Nx);
        fprintf(xmf, "          %s.h5:/sigma\n", h5_name.c_str());
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
