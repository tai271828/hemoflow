#ifndef PREPROCESSOR_CORE_H
#define PREPROCESSOR_CORE_H

#include <string>
#include <vector>
#include <array>
#include <set>
#include <cstdint>
#include <cmath>
#include <tuple>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <cassert>
#include <stack>
#include <map>
#include <functional>
#include <cstring>
#include <sys/stat.h>

// ============================================================================
// Voxel labels (matching globals.h GeometryLabel enum)
// ============================================================================
enum VoxelLabel : int16_t {
    VOXEL_UNUSED = 0,
    VOXEL_WALL = 1,
    VOXEL_FLUID = 2,
    VOXEL_INLET = 10,
    VOXEL_OUTLET = 11,
    VOXEL_OUTLET_REST = 12
};

// ============================================================================
// 3D array helper — row-major layout: idx(x,y,z) = x*ny*nz + y*nz + z
// ============================================================================
struct Vol3D {
    std::vector<int16_t> data;
    int nx, ny, nz;

    Vol3D() : nx(0), ny(0), nz(0) {}
    Vol3D(int nx_, int ny_, int nz_, int16_t val = 0)
        : data(static_cast<size_t>(nx_) * ny_ * nz_, val), nx(nx_), ny(ny_), nz(nz_) {}

    inline int16_t& operator()(int x, int y, int z) {
        return data[static_cast<size_t>(x) * ny * nz + y * nz + z];
    }
    inline int16_t operator()(int x, int y, int z) const {
        return data[static_cast<size_t>(x) * ny * nz + y * nz + z];
    }
    inline size_t size() const { return data.size(); }
};

// Boolean 3D array for voxelization
struct BoolVol3D {
    std::vector<uint8_t> data;
    int nx, ny, nz;

    BoolVol3D() : nx(0), ny(0), nz(0) {}
    BoolVol3D(int nx_, int ny_, int nz_)
        : data(static_cast<size_t>(nx_) * ny_ * nz_, 0), nx(nx_), ny(ny_), nz(nz_) {}

    inline uint8_t& operator()(int x, int y, int z) {
        return data[static_cast<size_t>(x) * ny * nz + y * nz + z];
    }
    inline uint8_t operator()(int x, int y, int z) const {
        return data[static_cast<size_t>(x) * ny * nz + y * nz + z];
    }
};

// Boolean 2D array for slicing
struct BoolGrid2D {
    std::vector<uint8_t> data;
    int nx, ny;

    BoolGrid2D() : nx(0), ny(0) {}
    BoolGrid2D(int nx_, int ny_)
        : data(static_cast<size_t>(nx_) * ny_, 0), nx(nx_), ny(ny_) {}

    inline uint8_t& operator()(int x, int y) {
        return data[static_cast<size_t>(x) * ny + y];
    }
    inline uint8_t operator()(int x, int y) const {
        return data[static_cast<size_t>(x) * ny + y];
    }
};

// ============================================================================
// Configuration structs
// ============================================================================
struct DebugConfig {
    bool enabled = false;
    std::set<std::string> outputs;
    std::string output_dir = ".";

    bool shouldSave(const std::string& name) const {
        return enabled && (outputs.empty() || outputs.count(name));
    }
};

struct OutputMeshesConfig {
    bool enabled = false;
    bool vasculature = true;
    bool coil = true;
};

struct RotationConfig {
    bool enabled = true;
    std::string inlet_target_axis = "-x";
    int inlet_centerline_index = 0;
    bool position_at_boundary = true;
};

struct PreprocessorConfig {
    std::string geometry_stl;
    std::string centerline_vtp;
    std::string stent_mesh_base;
    std::string coil_stl;
    int target_elements = 0;     // 0 means not set
    double target_dx = 0.0;      // 0 means not set
    std::string output_base_name = "geometry_";
    std::string output_dir = ".";
    int cut_width = 1;
    int distance = 4;
    double si_factor = 0.001;
    bool inhomogen = false;
    bool use_normal_for_face_selection = false;
    RotationConfig rotation;
    OutputMeshesConfig output_meshes;
    DebugConfig debug;
    std::string config_dir = ".";

    bool hasStent() const { return !stent_mesh_base.empty(); }
    bool hasCoil() const { return !coil_stl.empty(); }
    std::string resolvePath(const std::string& path) const;
};

// ============================================================================
// Data structures
// ============================================================================
struct StlTriangle {
    float normal[3];
    float v0[3], v1[3], v2[3];
};

struct StlMesh {
    std::vector<StlTriangle> triangles;
};

// Triangle as 3 points (tuples of 3 doubles) — used for voxelization
using Point3 = std::array<double, 3>;
using Triangle = std::array<Point3, 3>;
using Line2D = std::pair<Point3, Point3>;

struct VtpCenterline {
    std::vector<Point3> points;                     // all points
    std::vector<double> radii;                      // per-point radius
    std::vector<std::vector<int>> lines;            // each line: list of point indices
};

struct VoxelizationResult {
    BoolVol3D volume;
    double scale[3];
    double shift[3];
    int domain_size[3];
    double bbox_min[3];
    double bbox_max[3];
    double dx;
};

struct OpeningInfo {
    double radius;
    Point3 position;
    Point3 tangent;
};

struct OpeningData {
    std::vector<OpeningInfo> openings;
    std::vector<int> cut_list;   // face indices to cut [0-5]
};

struct GeometryResult {
    Vol3D volume;
    std::vector<int16_t> opening_index;
    std::vector<double> opening_radius;
    std::vector<double> opening_normalized_q_ratio;
    std::vector<Point3> opening_center;
    std::vector<Point3> opening_normal;
};

struct StentResult {
    Vol3D volume;
    Vol3D linear;
    Vol3D quadratic;
};

// ============================================================================
// JSON config parser
// ============================================================================
PreprocessorConfig loadConfig(const std::string& jsonPath);

// ============================================================================
// STL I/O
// ============================================================================
StlMesh readStlBinary(const std::string& path);
void writeStlBinary(const std::string& path, const StlMesh& mesh);
std::vector<Triangle> stlMeshToTriangles(const StlMesh& mesh);

// ============================================================================
// VTP I/O
// ============================================================================
VtpCenterline readVtp(const std::string& path);
void writeVtp(const std::string& path, const VtpCenterline& cl);

// ============================================================================
// Voxelization
// ============================================================================
struct DomainData {
    double scale[3];
    double shift[3];
    int domain[3];
    double bbox_min[3];
    double bbox_max[3];
};

DomainData calculateScaleAndShift(const std::vector<Triangle>& mesh,
                                  int targetElements, double target_dx);
std::vector<Triangle> scaleAndShiftMesh(const std::vector<Triangle>& mesh,
                                        const double scale[3], const double shift[3]);
std::vector<Triangle> swapAxis(const std::vector<Triangle>& mesh, int axis1, int axis2);
std::vector<Line2D> toIntersectingLines(const std::vector<Triangle>& mesh, int height);
void linesToVoxels(const std::vector<Line2D>& lineList, BoolGrid2D& pixels, bool isShell);
BoolVol3D voxelize(const std::string& stlFile, int targetElements,
                   bool isShell, const DomainData* domainData,
                   int rotation, double target_dx, DomainData& outDomain);
BoolVol3D voxelizeThreeProjections(const std::string& stlFile, int targetElements,
                                    const DomainData& domainData);

// ============================================================================
// Centerline extraction
// ============================================================================
std::vector<OpeningInfo> getOpeningsFromCenterline(const std::string& vtpFile);
std::vector<OpeningInfo> convertToVoxelspace(const std::vector<OpeningInfo>& openings,
                                              const double scale[3], const double shift[3]);

// ============================================================================
// Geometry utilities
// ============================================================================
bool inRange3D(const Point3& value, const Point3& ref, double distance);
int selectFaceFromNormal(const Point3& tangent);
int selectClosestFace(const Point3& pos, const int domainSize[3]);
std::vector<int> generateCutList(const int domainSize[3],
                                  const std::vector<OpeningInfo>& openings,
                                  int distance, bool useNormals);
std::string getOpeningFace(const Point3& center, int nx, int ny, int nz, double threshold);

// ============================================================================
// Wall creation
// ============================================================================
Vol3D createWalls(const BoolVol3D& input, const std::vector<int>& cutList,
                  int cutWidth, std::vector<int>& sliced);

// ============================================================================
// Opening detection
// ============================================================================
using VoxelCoord = std::tuple<int, int, int>;
struct OpeningDetectionResult {
    std::vector<std::vector<VoxelCoord>> inlets_outlets;
    Vol3D data;
};

OpeningDetectionResult detectOpenings(const Vol3D& inputArray);
struct PaintResult {
    std::vector<int16_t> opening_index;
    std::vector<Point3> opening_center;
    Vol3D painted_volume;
};
PaintResult paintInletsOutlets(const std::vector<std::vector<VoxelCoord>>& inlets_outlets,
                               const Vol3D& data, bool findBoundaryByArea);

// ============================================================================
// Boundary cuts
// ============================================================================
Vol3D applyBoundaryCuts(const Vol3D& volume, const std::vector<int>& cutList, int cutWidth);
BoolVol3D applyBoundaryCutsBool(const BoolVol3D& volume, const std::vector<int>& cutList, int cutWidth);

// ============================================================================
// Rotation
// ============================================================================
void calculateRotationMatrix(const Point3& source, const Point3& target, double R[9]);
Point3 parseTargetAxis(const std::string& axisString);
void rotateStl(const std::string& inPath, const double R[9], const std::string& outPath);
void rotateVtp(const std::string& inPath, const double R[9], const std::string& outPath);
void translateStl(const std::string& inPath, const Point3& translation, const std::string& outPath);
void translateVtp(const std::string& inPath, const Point3& translation, const std::string& outPath);
void getStlBounds(const std::string& stlPath, Point3& minCoords, Point3& maxCoords);
void getCenterlineOpening(const std::string& vtpPath, int openingIndex, Point3& position, Point3& normal);
void rotateGeometryToAlignInlet(const std::string& stlPath, const std::string& vtpPath,
                                 const std::string& targetAxis,
                                 const std::string& outputStl, const std::string& outputVtp,
                                 int inletIndex, bool positionAtBoundary,
                                 double R_out[9], Point3& translation_out);

// ============================================================================
// Mesh output
// ============================================================================
void transformAndSaveStl(const std::string& inputStl, const std::string& outputStl,
                         double siFactor, const float* bboxMin);
void saveRescaledMeshes(const PreprocessorConfig& config);

// ============================================================================
// Pipeline + NPZ output
// ============================================================================
void saveGeometry(const PreprocessorConfig& config,
                  const GeometryResult& geom,
                  const VoxelizationResult& voxResult,
                  const StentResult* stentResult);
void runPreprocessingPipeline(PreprocessorConfig& config);

// ============================================================================
// Utility
// ============================================================================
void ensureDirectoryExists(const std::string& path);

#endif // PREPROCESSOR_CORE_H
