#ifndef IO_H
#define IO_H

#include "globals.h"
#include "helper.h"

// For directory manipulations
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// HDF5 and HighFive includes. HDF5 is defined by CMake only when a parallel
// HDF5 library is found; without it the HDF5 output path is compiled out.
#ifdef HDF5
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>
#include <highfive/H5File.hpp>
#include <highfive/H5PropertyList.hpp>
#include <hdf5.h>
#endif

#include "cnpy.h"
#include "io/xdmfDataOutput.h"

bool fileExists (const std::string& name);
int dirExists(const string& pathName);
int do_mkdir(const char *path, mode_t mode);
int mkpath(const char *path, mode_t mode);

void writeNPZ(MultiBlockLattice3D<T,DESCRIPTOR>& lattice, plint iter);  // Note: this output type does not do unit conversion.
#ifdef HDF5
void writeHDF5(MultiBlockLattice3D<T,DESCRIPTOR>& lattice, const SimPar &sim, plint iter, string outDir, MultiNTensorField3D<T> *field1 = nullptr);
#endif
void writeVTK(MultiBlockLattice3D<T,DESCRIPTOR>& lattice, const SimPar &sim, plint iter, MultiNTensorField3D<T> *field1 = nullptr);


#endif
