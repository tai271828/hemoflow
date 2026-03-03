#include "preprocessor_core.h"
#include "cnpy.h"
#include "tinyxml/tinyxml.h"

// Palabos wraps tinyxml in namespace plb
using plb::TiXmlDocument;
using plb::TiXmlElement;

#include <chrono>

// ============================================================================
// Utility
// ============================================================================

static void logInfo(const std::string& msg) {
    std::cout << "INFO: " << msg << std::endl;
}
[[maybe_unused]] static void logDebug(const std::string& /*msg*/) {
    // Could be gated by a verbosity flag
}
static void logWarning(const std::string& msg) {
    std::cerr << "WARNING: " << msg << std::endl;
}

void ensureDirectoryExists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
#ifdef _WIN32
        _mkdir(path.c_str());
#else
        mkdir(path.c_str(), 0755);
#endif
    }
}

static std::string pathJoin(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

static std::string pathDir(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

static std::string pathStem(const std::string& path) {
    size_t slashPos = path.find_last_of('/');
    std::string name = (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
    size_t dotPos = name.find_last_of('.');
    if (dotPos == std::string::npos) return name;
    return name.substr(0, dotPos);
}

static std::string pathExtension(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) return "";
    return path.substr(dotPos);
}

static void removeFile(const std::string& path) {
    std::remove(path.c_str());
}

static void renameFile(const std::string& from, const std::string& to) {
    std::rename(from.c_str(), to.c_str());
}

std::string PreprocessorConfig::resolvePath(const std::string& path) const {
    if (path.empty()) return path;
    if (path[0] == '/') return path;
    return pathJoin(config_dir, path);
}

// ============================================================================
// JSON config parser (minimal recursive descent)
// ============================================================================

namespace json {

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    enum Type { T_NULL, T_BOOL, T_NUMBER, T_STRING, T_OBJECT, T_ARRAY };
    Type type = T_NULL;
    bool boolVal = false;
    double numVal = 0;
    std::string strVal;
    Object objVal;
    Array arrVal;
};

static void skipWhitespace(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

static Value parseValue(const std::string& s, size_t& i);

static std::string parseString(const std::string& s, size_t& i) {
    assert(s[i] == '"');
    ++i;
    std::string result;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\') {
            ++i;
            if (i < s.size()) {
                switch (s[i]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    default: result += s[i]; break;
                }
            }
        } else {
            result += s[i];
        }
        ++i;
    }
    if (i < s.size()) ++i; // skip closing quote
    return result;
}

static Value parseObject(const std::string& s, size_t& i) {
    Value v;
    v.type = Value::T_OBJECT;
    assert(s[i] == '{');
    ++i;
    skipWhitespace(s, i);
    while (i < s.size() && s[i] != '}') {
        skipWhitespace(s, i);
        std::string key = parseString(s, i);
        skipWhitespace(s, i);
        assert(s[i] == ':');
        ++i;
        skipWhitespace(s, i);
        v.objVal[key] = parseValue(s, i);
        skipWhitespace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
        skipWhitespace(s, i);
    }
    if (i < s.size()) ++i; // skip '}'
    return v;
}

static Value parseArray(const std::string& s, size_t& i) {
    Value v;
    v.type = Value::T_ARRAY;
    assert(s[i] == '[');
    ++i;
    skipWhitespace(s, i);
    while (i < s.size() && s[i] != ']') {
        v.arrVal.push_back(parseValue(s, i));
        skipWhitespace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
        skipWhitespace(s, i);
    }
    if (i < s.size()) ++i; // skip ']'
    return v;
}

static Value parseValue(const std::string& s, size_t& i) {
    skipWhitespace(s, i);
    if (i >= s.size()) return Value();
    if (s[i] == '"') {
        Value v;
        v.type = Value::T_STRING;
        v.strVal = parseString(s, i);
        return v;
    }
    if (s[i] == '{') return parseObject(s, i);
    if (s[i] == '[') return parseArray(s, i);
    if (s[i] == 't' || s[i] == 'f') {
        Value v;
        v.type = Value::T_BOOL;
        if (s.substr(i, 4) == "true") { v.boolVal = true; i += 4; }
        else { v.boolVal = false; i += 5; }
        return v;
    }
    if (s[i] == 'n') {
        Value v;
        v.type = Value::T_NULL;
        i += 4; // "null"
        return v;
    }
    // number
    Value v;
    v.type = Value::T_NUMBER;
    size_t start = i;
    if (s[i] == '-') ++i;
    while (i < s.size() && (isdigit(s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
        if ((s[i] == '+' || s[i] == '-') && i > start && s[i-1] != 'e' && s[i-1] != 'E') break;
        ++i;
    }
    v.numVal = std::stod(s.substr(start, i - start));
    return v;
}

static Value parse(const std::string& jsonStr) {
    size_t i = 0;
    return parseValue(jsonStr, i);
}

// Helpers to get values with defaults
static std::string getStr(const Object& obj, const std::string& key, const std::string& def = "") {
    auto it = obj.find(key);
    if (it == obj.end()) return def;
    if (it->second.type == Value::T_STRING) return it->second.strVal;
    return def;
}

static int getInt(const Object& obj, const std::string& key, int def = 0) {
    auto it = obj.find(key);
    if (it == obj.end()) return def;
    if (it->second.type == Value::T_NUMBER) return (int)it->second.numVal;
    if (it->second.type == Value::T_STRING) {
        if (it->second.strVal.empty()) return def;
        return std::stoi(it->second.strVal);
    }
    return def;
}

static double getDouble(const Object& obj, const std::string& key, double def = 0.0) {
    auto it = obj.find(key);
    if (it == obj.end()) return def;
    if (it->second.type == Value::T_NUMBER) return it->second.numVal;
    if (it->second.type == Value::T_STRING) {
        if (it->second.strVal.empty()) return def;
        return std::stod(it->second.strVal);
    }
    return def;
}

static bool getBool(const Object& obj, const std::string& key, bool def = false) {
    auto it = obj.find(key);
    if (it == obj.end()) return def;
    if (it->second.type == Value::T_BOOL) return it->second.boolVal;
    return def;
}

} // namespace json

PreprocessorConfig loadConfig(const std::string& jsonPath) {
    std::ifstream ifs(jsonPath);
    if (!ifs.is_open())
        throw std::runtime_error("Cannot open config file: " + jsonPath);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    json::Value root = json::parse(content);
    if (root.type != json::Value::T_OBJECT)
        throw std::runtime_error("Config file root must be a JSON object");

    const auto& obj = root.objVal;

    PreprocessorConfig cfg;
    cfg.config_dir = pathDir(jsonPath);
    cfg.geometry_stl = json::getStr(obj, "geometry_original_stl");
    cfg.centerline_vtp = json::getStr(obj, "centerline_vtp");
    cfg.stent_mesh_base = json::getStr(obj, "stent_mesh_base");
    cfg.coil_stl = json::getStr(obj, "coil_stl");
    cfg.output_base_name = json::getStr(obj, "output_base_name", "geometry_");
    cfg.output_dir = json::getStr(obj, "output_dir", ".");
    cfg.cut_width = json::getInt(obj, "cutWidth", 1);
    cfg.distance = json::getInt(obj, "distance", 4);
    cfg.si_factor = json::getDouble(obj, "si_factor", 0.001);
    cfg.inhomogen = json::getBool(obj, "inhomogen", false);
    cfg.use_normal_for_face_selection = json::getBool(obj, "use_normal_for_face_selection", false);

    // target_elements / target_dx — may be string or number
    {
        auto it = obj.find("target_elements");
        if (it != obj.end()) {
            if (it->second.type == json::Value::T_NUMBER) {
                cfg.target_elements = (int)it->second.numVal;
            } else if (it->second.type == json::Value::T_STRING && !it->second.strVal.empty()) {
                cfg.target_elements = std::stoi(it->second.strVal);
            }
        }
    }
    {
        auto it = obj.find("target_dx");
        if (it != obj.end()) {
            if (it->second.type == json::Value::T_NUMBER) {
                cfg.target_dx = it->second.numVal;
            } else if (it->second.type == json::Value::T_STRING && !it->second.strVal.empty()) {
                cfg.target_dx = std::stod(it->second.strVal);
            }
        }
    }

    // Rotation
    {
        auto it = obj.find("rotation");
        if (it != obj.end() && it->second.type == json::Value::T_OBJECT) {
            const auto& rot = it->second.objVal;
            cfg.rotation.enabled = json::getBool(rot, "enabled", false);
            cfg.rotation.inlet_target_axis = json::getStr(rot, "inlet_target_axis", "-x");
            cfg.rotation.inlet_centerline_index = json::getInt(rot, "inlet_centerline_index", 0);
            cfg.rotation.position_at_boundary = json::getBool(rot, "position_at_boundary", true);
        }
    }

    // Output meshes
    {
        auto it = obj.find("output_meshes");
        if (it != obj.end() && it->second.type == json::Value::T_OBJECT) {
            const auto& m = it->second.objVal;
            cfg.output_meshes.enabled = json::getBool(m, "enabled", false);
            cfg.output_meshes.vasculature = json::getBool(m, "vasculature", true);
            cfg.output_meshes.coil = json::getBool(m, "coil", true);
        }
    }

    // Debug
    {
        auto it = obj.find("debug");
        if (it != obj.end() && it->second.type == json::Value::T_OBJECT) {
            const auto& d = it->second.objVal;
            cfg.debug.enabled = json::getBool(d, "enabled", false);
            cfg.debug.output_dir = json::getStr(d, "output_dir", ".");
            auto oit = d.find("outputs");
            if (oit != d.end() && oit->second.type == json::Value::T_ARRAY) {
                for (auto& v : oit->second.arrVal) {
                    if (v.type == json::Value::T_STRING)
                        cfg.debug.outputs.insert(v.strVal);
                }
            }
        }
    }

    if (cfg.target_elements == 0 && cfg.target_dx == 0.0)
        throw std::runtime_error("Must specify either target_elements or target_dx");

    return cfg;
}

// ============================================================================
// Binary STL Reader/Writer
// ============================================================================

StlMesh readStlBinary(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open())
        throw std::runtime_error("Cannot open STL file: " + path);

    char header[80];
    ifs.read(header, 80);

    uint32_t numTriangles = 0;
    ifs.read(reinterpret_cast<char*>(&numTriangles), 4);

    StlMesh mesh;
    mesh.triangles.resize(numTriangles);

    for (uint32_t i = 0; i < numTriangles; ++i) {
        auto& tri = mesh.triangles[i];
        ifs.read(reinterpret_cast<char*>(tri.normal), 12);
        ifs.read(reinterpret_cast<char*>(tri.v0), 12);
        ifs.read(reinterpret_cast<char*>(tri.v1), 12);
        ifs.read(reinterpret_cast<char*>(tri.v2), 12);
        uint16_t attrByteCount;
        ifs.read(reinterpret_cast<char*>(&attrByteCount), 2);
    }

    return mesh;
}

void writeStlBinary(const std::string& path, const StlMesh& mesh) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open())
        throw std::runtime_error("Cannot write STL file: " + path);

    char header[80] = {};
    ofs.write(header, 80);

    uint32_t numTriangles = (uint32_t)mesh.triangles.size();
    ofs.write(reinterpret_cast<const char*>(&numTriangles), 4);

    for (auto& tri : mesh.triangles) {
        ofs.write(reinterpret_cast<const char*>(tri.normal), 12);
        ofs.write(reinterpret_cast<const char*>(tri.v0), 12);
        ofs.write(reinterpret_cast<const char*>(tri.v1), 12);
        ofs.write(reinterpret_cast<const char*>(tri.v2), 12);
        uint16_t attrByteCount = 0;
        ofs.write(reinterpret_cast<const char*>(&attrByteCount), 2);
    }
}

std::vector<Triangle> stlMeshToTriangles(const StlMesh& mesh) {
    std::vector<Triangle> tris;
    tris.reserve(mesh.triangles.size());
    for (auto& t : mesh.triangles) {
        Triangle tri;
        tri[0] = {(double)t.v0[0], (double)t.v0[1], (double)t.v0[2]};
        tri[1] = {(double)t.v1[0], (double)t.v1[1], (double)t.v1[2]};
        tri[2] = {(double)t.v2[0], (double)t.v2[1], (double)t.v2[2]};
        tris.push_back(tri);
    }
    return tris;
}

// ============================================================================
// VTP Parser (using tinyxml)
// ============================================================================

// Parse whitespace-separated ASCII numbers from a string
static std::vector<double> parseAsciiDoubles(const std::string& text) {
    std::vector<double> vals;
    std::istringstream iss(text);
    double v;
    while (iss >> v) vals.push_back(v);
    return vals;
}

static std::vector<int64_t> parseAsciiInts(const std::string& text) {
    std::vector<int64_t> vals;
    std::istringstream iss(text);
    int64_t v;
    while (iss >> v) vals.push_back(v);
    return vals;
}

VtpCenterline readVtp(const std::string& path) {
    TiXmlDocument doc(path.c_str());
    if (!doc.LoadFile())
        throw std::runtime_error("Cannot parse VTP file: " + path);

    VtpCenterline cl;

    TiXmlElement* root = doc.RootElement(); // VTKFile
    if (!root) throw std::runtime_error("VTP: no root element");
    TiXmlElement* polyData = root->FirstChildElement("PolyData");
    if (!polyData) throw std::runtime_error("VTP: no PolyData element");
    TiXmlElement* piece = polyData->FirstChildElement("Piece");
    if (!piece) throw std::runtime_error("VTP: no Piece element");

    // Parse Points
    TiXmlElement* pointsElem = piece->FirstChildElement("Points");
    if (pointsElem) {
        TiXmlElement* da = pointsElem->FirstChildElement("DataArray");
        if (da && da->GetText()) {
            auto vals = parseAsciiDoubles(da->GetText());
            for (size_t i = 0; i + 2 < vals.size(); i += 3) {
                cl.points.push_back(Point3{{vals[i], vals[i+1], vals[i+2]}});
            }
        }
    }

    // Parse PointData -> Radius
    TiXmlElement* pointData = piece->FirstChildElement("PointData");
    if (pointData) {
        for (TiXmlElement* da = pointData->FirstChildElement("DataArray");
             da; da = da->NextSiblingElement("DataArray")) {
            const char* name = da->Attribute("Name");
            if (name && std::string(name) == "Radius" && da->GetText()) {
                auto vals = parseAsciiDoubles(da->GetText());
                cl.radii = vals;
            }
        }
    }

    // Parse Lines
    TiXmlElement* linesElem = piece->FirstChildElement("Lines");
    if (linesElem) {
        std::vector<int64_t> connectivity, offsets;
        for (TiXmlElement* da = linesElem->FirstChildElement("DataArray");
             da; da = da->NextSiblingElement("DataArray")) {
            const char* name = da->Attribute("Name");
            if (!name || !da->GetText()) continue;
            if (std::string(name) == "connectivity") {
                connectivity = parseAsciiInts(da->GetText());
            } else if (std::string(name) == "offsets") {
                offsets = parseAsciiInts(da->GetText());
            }
        }

        int64_t prevOffset = 0;
        for (auto off : offsets) {
            std::vector<int> line;
            for (int64_t j = prevOffset; j < off; ++j) {
                line.push_back((int)connectivity[j]);
            }
            cl.lines.push_back(line);
            prevOffset = off;
        }
    }

    return cl;
}

void writeVtp(const std::string& path, const VtpCenterline& cl) {
    std::ofstream ofs(path);
    if (!ofs.is_open())
        throw std::runtime_error("Cannot write VTP file: " + path);

    int numPoints = (int)cl.points.size();
    int numLines = (int)cl.lines.size();

    ofs << "<?xml version=\"1.0\"?>\n";
    ofs << "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    ofs << "  <PolyData>\n";
    ofs << "    <Piece NumberOfPoints=\"" << numPoints << "\" NumberOfLines=\"" << numLines
        << "\" NumberOfStrips=\"0\" NumberOfVerts=\"0\" NumberOfPolys=\"0\">\n";

    // PointData
    if (!cl.radii.empty()) {
        ofs << "      <PointData>\n";
        ofs << "        <DataArray type=\"Float64\" Name=\"Radius\" format=\"ascii\">\n";
        for (auto r : cl.radii) ofs << "          " << r << "\n";
        ofs << "        </DataArray>\n";
        ofs << "      </PointData>\n";
    }

    // Points
    ofs << "      <Points>\n";
    ofs << "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (auto& p : cl.points)
        ofs << "          " << (float)p[0] << " " << (float)p[1] << " " << (float)p[2] << "\n";
    ofs << "        </DataArray>\n";
    ofs << "      </Points>\n";

    // Lines
    if (numLines > 0) {
        ofs << "      <Lines>\n";
        ofs << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
        for (auto& line : cl.lines)
            for (int id : line) ofs << "          " << id << "\n";
        ofs << "        </DataArray>\n";
        ofs << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
        int64_t offset = 0;
        for (auto& line : cl.lines) {
            offset += (int64_t)line.size();
            ofs << "          " << offset << "\n";
        }
        ofs << "        </DataArray>\n";
        ofs << "      </Lines>\n";
    }

    ofs << "    </Piece>\n";
    ofs << "  </PolyData>\n";
    ofs << "</VTKFile>\n";
}

// ============================================================================
// Voxelization (porting slice.py, perimeter.py, voxelization.py, util.py)
// ============================================================================

static Point3 linearInterpolation(const Point3& p1, const Point3& p2, double distance) {
    double sx = p1[0] - p2[0];
    double sy = p1[1] - p2[1];
    double sz = p1[2] - p2[2];
    return {p1[0] - distance * sx, p1[1] - distance * sy, p1[2] - distance * sz};
}

static bool isAboveAndBelow(const Triangle& tri, int height) {
    int above = 0, below = 0, same = 0;
    for (int i = 0; i < 3; ++i) {
        if (tri[i][2] > height) ++above;
        else if (tri[i][2] < height) ++below;
        else ++same;
    }
    if (same == 3 || same == 2) return true;
    return (above > 0 && below > 0);
}

static bool isIntersectingTriangle(const Triangle& tri, int height) {
    int same = 0;
    for (int i = 0; i < 3; ++i)
        if (tri[i][2] == height) ++same;
    return same == 3;
}

static Point3 whereLineCrossesZ(Point3 p1, Point3 p2, int z) {
    if (p1[2] > p2[2]) std::swap(p1, p2);
    double distance = 0;
    if (p2[2] != p1[2]) distance = (z - p1[2]) / (p2[2] - p1[2]);
    return linearInterpolation(p1, p2, distance);
}

static Line2D triangleToIntersectingLines(const Triangle& tri, int height) {
    std::vector<Point3> above, below, same;
    for (int i = 0; i < 3; ++i) {
        if (tri[i][2] > height) above.push_back(tri[i]);
        else if (tri[i][2] < height) below.push_back(tri[i]);
        else same.push_back(tri[i]);
    }

    if (same.size() == 2) {
        return {same[0], same[1]};
    } else if (same.size() == 1) {
        Point3 side1 = whereLineCrossesZ(above[0], below[0], height);
        return {side1, same[0]};
    } else {
        // Build all above-below pairs
        std::vector<std::pair<Point3, Point3>> pairs;
        for (auto& a : above)
            for (auto& b : below)
                pairs.push_back(std::make_pair(b, a));
        Point3 side1 = whereLineCrossesZ(pairs[0].first, pairs[0].second, height);
        Point3 side2 = whereLineCrossesZ(pairs[1].first, pairs[1].second, height);
        return {side1, side2};
    }
}

std::vector<Line2D> toIntersectingLines(const std::vector<Triangle>& mesh, int height) {
    std::vector<Line2D> lines;
    for (auto& tri : mesh) {
        if (!isAboveAndBelow(tri, height)) continue;
        if (isIntersectingTriangle(tri, height)) continue;
        lines.push_back(triangleToIntersectingLines(tri, height));
    }
    return lines;
}

// Perimeter: findRelevantLines
static std::vector<const Line2D*> findRelevantLines(const std::vector<Line2D>& lineList, int x) {
    std::vector<const Line2D*> result;
    for (auto& line : lineList) {
        bool above = false, below = false, same = false;
        for (int p = 0; p < 2; ++p) {
            double px = (p == 0) ? line.first[0] : line.second[0];
            if (px > x) above = true;
            else if (px == x) same = true;
            else below = true;
        }
        if ((above && below) || (same && above))
            result.push_back(&line);
    }
    return result;
}

static double generateY(const Line2D& line, int x) {
    if (line.second[0] == line.first[0]) return -1;
    double ratio = (x - line.first[0]) / (line.second[0] - line.first[0]);
    double ydist = line.second[1] - line.first[1];
    return line.first[1] + ratio * ydist;
}

static bool onLine(const Line2D& line, int x, int y) {
    double newy = generateY(line, x);
    if ((int)newy != y) return false;
    if ((int)line.first[0] != x && (int)line.second[0] != x &&
        (std::max(line.first[0], line.second[0]) < x ||
         std::min(line.first[0], line.second[0]) > x))
        return false;
    if ((int)line.first[1] != y && (int)line.second[1] != y &&
        (std::max(line.first[1], line.second[1]) < y ||
         std::min(line.first[1], line.second[1]) > y))
        return false;
    return true;
}

void linesToVoxels(const std::vector<Line2D>& lineList, BoolGrid2D& pixels, bool isShell) {
    for (int x = 0; x < pixels.nx; ++x) {
        auto relevantLines = findRelevantLines(lineList, x);
        // Compute target Y values
        std::vector<int> targetYs;
        for (auto* line : relevantLines)
            targetYs.push_back((int)generateY(*line, x));

        if (isShell) {
            for (int y = 0; y < pixels.ny; ++y) {
                bool found = false;
                for (int ty : targetYs) {
                    if (ty == y) { found = true; break; }
                }
                if (found) {
                    for (auto* line : relevantLines) {
                        if (onLine(*line, x, y)) {
                            pixels(x, y) = 1;
                        }
                    }
                }
            }
        } else {
            bool isBlack = false;
            for (int y = 0; y < pixels.ny; ++y) {
                if (isBlack) pixels(x, y) = 1;
                bool found = false;
                for (int ty : targetYs) {
                    if (ty == y) { found = true; break; }
                }
                if (found) {
                    for (auto* line : relevantLines) {
                        if (onLine(*line, x, y)) {
                            isBlack = !isBlack;
                            pixels(x, y) = 1;
                        }
                    }
                }
            }
        }
    }
}

DomainData calculateScaleAndShift(const std::vector<Triangle>& mesh,
                                  int targetElements, double target_dx) {
    double mins[3] = {1e30, 1e30, 1e30};
    double maxs[3] = {-1e30, -1e30, -1e30};
    for (auto& tri : mesh) {
        for (int v = 0; v < 3; ++v) {
            for (int d = 0; d < 3; ++d) {
                mins[d] = std::min(mins[d], tri[v][d]);
                maxs[d] = std::max(maxs[d], tri[v][d]);
            }
        }
    }

    double ds[3];
    for (int d = 0; d < 3; ++d) ds[d] = maxs[d] - mins[d];

    double ds3 = ds[0] * ds[1] * ds[2];
    double voxScale = std::pow((double)targetElements / ds3, 1.0 / 3.0);

    if (target_dx > 0) voxScale = 1.0 / target_dx;

    DomainData dd;
    for (int d = 0; d < 3; ++d) {
        dd.domain[d] = (int)(voxScale * ds[d]);
        dd.shift[d] = -mins[d];
        dd.bbox_min[d] = mins[d];
        dd.bbox_max[d] = maxs[d];
    }

    double xyscale = (double)dd.domain[0] / ds[0];
    for (int d = 0; d < 3; ++d) dd.scale[d] = xyscale;

    return dd;
}

static bool removeDups(const Triangle& tri) {
    // Check if any two vertices are identical after transformation
    std::set<std::tuple<double, double, double>> pts;
    for (int i = 0; i < 3; ++i)
        pts.insert({tri[i][0], tri[i][1], tri[i][2]});
    return pts.size() == 3;
}

std::vector<Triangle> scaleAndShiftMesh(const std::vector<Triangle>& mesh,
                                        const double scale[3], const double shift[3]) {
    std::vector<Triangle> result;
    for (auto& tri : mesh) {
        Triangle newTri;
        for (int v = 0; v < 3; ++v) {
            for (int d = 0; d < 3; ++d)
                newTri[v][d] = (tri[v][d] + shift[d]) * scale[d];
        }
        if (removeDups(newTri)) result.push_back(newTri);
    }
    return result;
}

std::vector<Triangle> swapAxis(const std::vector<Triangle>& mesh, int axis1, int axis2) {
    std::vector<Triangle> result;
    for (auto& tri : mesh) {
        Triangle newTri;
        for (int v = 0; v < 3; ++v) {
            newTri[v] = tri[v];
            newTri[v][axis1] = tri[v][axis2];
            newTri[v][axis2] = tri[v][axis1];
        }
        if (removeDups(newTri)) result.push_back(newTri);
    }
    return result;
}

// padVoxelArray: add 1-voxel border (from util.py)
static BoolVol3D padVoxelArray(const BoolVol3D& voxels, int newDomain[3]) {
    int nx = voxels.nx, ny = voxels.ny, nz = voxels.nz;
    int nnx = nx + 2, nny = ny + 2, nnz = nz + 2;
    BoolVol3D vol(nnx, nny, nnz);
    for (int a = 0; a < nx; ++a)
        for (int b = 0; b < ny; ++b)
            for (int c = 0; c < nz; ++c)
                vol(a + 1, b + 1, c + 1) = voxels(a, b, c);

    // newDomain matches Python: (new_shape[1], new_shape[2], new_shape[0])
    newDomain[0] = nny;
    newDomain[1] = nnz;
    newDomain[2] = nnx;
    return vol;
}

// Transpose BoolVol3D: swapaxes
static BoolVol3D swapAxesVol(const BoolVol3D& v, int a1, int a2) {
    int dims[3] = {v.nx, v.ny, v.nz};
    int newDims[3] = {dims[0], dims[1], dims[2]};
    std::swap(newDims[a1], newDims[a2]);

    BoolVol3D result(newDims[0], newDims[1], newDims[2]);
    for (int x = 0; x < v.nx; ++x) {
        for (int y = 0; y < v.ny; ++y) {
            for (int z = 0; z < v.nz; ++z) {
                int idx[3] = {x, y, z};
                int nidx[3] = {idx[0], idx[1], idx[2]};
                std::swap(nidx[a1], nidx[a2]);
                result(nidx[0], nidx[1], nidx[2]) = v(x, y, z);
            }
        }
    }
    return result;
}

BoolVol3D voxelize(const std::string& stlFile, int targetElements,
                   bool isShell, const DomainData* domainData,
                   int rotation, double target_dx, DomainData& outDomain) {
    StlMesh stlMesh = readStlBinary(stlFile);
    std::vector<Triangle> tris = stlMeshToTriangles(stlMesh);

    DomainData dd;
    if (domainData) {
        dd = *domainData;
    } else {
        dd = calculateScaleAndShift(tris, targetElements, target_dx);
    }
    outDomain = dd;

    tris = scaleAndShiftMesh(tris, dd.scale, dd.shift);

    int domain[3] = {dd.domain[0], dd.domain[1], dd.domain[2]};

    // If shell mesh, rotate for different projections
    if (rotation == 0) {
        tris = swapAxis(tris, 0, 1);
        std::swap(domain[0], domain[1]);
    } else if (rotation == 1) {
        tris = swapAxis(tris, 1, 2);
        std::swap(domain[1], domain[2]);
    }

    // vol is addressed [z][x][y] — we use BoolVol3D with dims (domain[2], domain[0], domain[1])
    BoolVol3D vol(domain[2], domain[0], domain[1]);

    for (int height = 0; height < domain[2]; ++height) {
        auto lines = toIntersectingLines(tris, height);
        BoolGrid2D prepixel(domain[0], domain[1]);
        linesToVoxels(lines, prepixel, isShell);
        // Copy to vol
        for (int x = 0; x < domain[0]; ++x)
            for (int y = 0; y < domain[1]; ++y)
                vol(height, x, y) = prepixel(x, y);
    }

    // Pad
    int newDomain[3];
    vol = padVoxelArray(vol, newDomain);
    domain[0] = newDomain[0];
    domain[1] = newDomain[1];
    domain[2] = newDomain[2];

    // If shell mesh, rotate back
    if (rotation == 0) {
        vol = swapAxesVol(vol, 1, 2);
    } else if (rotation == 1) {
        vol = swapAxesVol(vol, 0, 2);
    }

    // Fix [z][x][y] back to [x][y][z]
    vol = swapAxesVol(vol, 0, 2); // swap z and z first: vol is [z][x][y] -> [y][x][z]
    vol = swapAxesVol(vol, 0, 1); // [y][x][z] -> [x][y][z]

    outDomain.domain[0] = domain[0];
    outDomain.domain[1] = domain[1];
    outDomain.domain[2] = domain[2];

    return vol;
}

BoolVol3D voxelizeThreeProjections(const std::string& stlFile, int targetElements,
                                    const DomainData& domainData) {
    logInfo("  Voxelizing from 3 projections");

    DomainData outDomain;

    logInfo("    Projection #1 (default)");
    BoolVol3D vol1 = voxelize(stlFile, targetElements, true, &domainData, -1, 0, outDomain);

    logInfo("    Projection #2 (rotation 0)");
    BoolVol3D vol2 = voxelize(stlFile, targetElements, true, &domainData, 0, 0, outDomain);

    logInfo("    Projection #3 (rotation 1)");
    BoolVol3D vol3 = voxelize(stlFile, targetElements, true, &domainData, 1, 0, outDomain);

    logInfo("    Merging projections");
    // Logical OR merge — all volumes should have the same dimensions
    BoolVol3D merged(vol1.nx, vol1.ny, vol1.nz);
    for (size_t i = 0; i < merged.data.size(); ++i)
        merged.data[i] = vol1.data[i] | vol2.data[i] | vol3.data[i];

    return merged;
}

// ============================================================================
// Centerline extraction (porting centerline.py)
// ============================================================================

static Point3 normalize(const Point3& v) {
    double norm = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (norm == 0) return v;
    return {v[0]/norm, v[1]/norm, v[2]/norm};
}

static double vecNorm(const Point3& v) {
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

std::vector<OpeningInfo> getOpeningsFromCenterline(const std::string& vtpFile) {
    VtpCenterline cl = readVtp(vtpFile);

    std::vector<OpeningInfo> rTanData;
    int nLines = (int)cl.lines.size();

    for (int ll = 0; ll < nLines; ++ll) {
        auto& idList = cl.lines[ll];
        int numPts = (int)idList.size();

        // Get radii on this line
        double r1 = cl.radii[idList[0]];
        double r2 = cl.radii[idList[numPts - 1]];

        // Get points
        Point3 p0 = cl.points[idList[0]];
        Point3 p1 = cl.points[idList[1]];
        Point3 pLast = cl.points[idList[numPts - 1]];
        Point3 pPrev = cl.points[idList[numPts - 3]]; // last 2 points are sometimes the same

        if (ll == 0) {
            // Inlet: tangent points inward
            Point3 v1 = {p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2]};
            Point3 negV1 = {-v1[0], -v1[1], -v1[2]};
            rTanData.push_back(OpeningInfo{r1, p0, negV1});
        }

        // Outlet tangent: points outward
        Point3 v2 = {pLast[0] - pPrev[0], pLast[1] - pPrev[1], pLast[2] - pPrev[2]};
        Point3 negV2 = {-v2[0], -v2[1], -v2[2]};
        rTanData.push_back(OpeningInfo{r2, pLast, negV2});
    }

    // Sort by radius descending
    std::sort(rTanData.begin(), rTanData.end(),
              [](const OpeningInfo& a, const OpeningInfo& b) { return a.radius > b.radius; });

    return rTanData;
}

std::vector<OpeningInfo> convertToVoxelspace(const std::vector<OpeningInfo>& openings,
                                              const double scale[3], const double shift[3]) {
    std::vector<OpeningInfo> result;
    for (auto& o : openings) {
        OpeningInfo vo;
        vo.radius = o.radius;
        for (int d = 0; d < 3; ++d)
            vo.position[d] = (o.position[d] + shift[d]) * scale[d];
        vo.tangent = normalize(o.tangent);
        result.push_back(vo);
    }
    return result;
}

// ============================================================================
// Geometry utilities (porting geometry.py)
// ============================================================================

bool inRange3D(const Point3& value, const Point3& ref, double distance) {
    for (int i = 0; i < 3; ++i)
        if (std::abs(value[i] - ref[i]) >= distance) return false;
    return true;
}

int selectFaceFromNormal(const Point3& tangent) {
    double absT[3] = {std::abs(tangent[0]), std::abs(tangent[1]), std::abs(tangent[2])};
    int dominant = 0;
    if (absT[1] > absT[dominant]) dominant = 1;
    if (absT[2] > absT[dominant]) dominant = 2;

    if (tangent[dominant] > 0) return dominant * 2;       // X-, Y-, or Z-
    else return dominant * 2 + 1;                          // X+, Y+, or Z+
}

int selectClosestFace(const Point3& pos, const int domainSize[3]) {
    double dists[6] = {
        pos[0],                         // X-
        domainSize[0] - pos[0],         // X+
        pos[1],                         // Y-
        domainSize[1] - pos[1],         // Y+
        pos[2],                         // Z-
        domainSize[2] - pos[2]          // Z+
    };
    int minIdx = 0;
    for (int i = 1; i < 6; ++i)
        if (dists[i] < dists[minIdx]) minIdx = i;
    return minIdx;
}

std::vector<int> generateCutList(const int domainSize[3],
                                  const std::vector<OpeningInfo>& openings,
                                  int distance, bool useNormals) {
    int sidesToCut[6] = {};
    const char* faceNames[] = {"X-", "X+", "Y-", "Y+", "Z-", "Z+"};

    for (size_t idx = 0; idx < openings.size(); ++idx) {
        auto& o = openings[idx];
        double tangentNorm = vecNorm(o.tangent);

        int selectedFace;
        if (useNormals && tangentNorm > 0.1) {
            int normalFace = selectFaceFromNormal(o.tangent);
            int closestFace = selectClosestFace(o.position, domainSize);

            int axis = normalFace / 2;
            double distToNormalFace = (normalFace % 2 == 0) ? o.position[axis] :
                                     domainSize[axis] - o.position[axis];

            if (distToNormalFace < distance * 3.0) {
                selectedFace = normalFace;
            } else {
                selectedFace = closestFace;
            }
        } else {
            selectedFace = selectClosestFace(o.position, domainSize);
        }

        sidesToCut[selectedFace] = 1;
        logInfo(std::string("  Opening ") + std::to_string(idx) + ": face=" +
                faceNames[selectedFace]);
    }

    std::vector<int> cutFaces;
    for (int i = 0; i < 6; ++i)
        if (sidesToCut[i]) cutFaces.push_back(i);

    std::string msg = "  Faces to cut:";
    for (int f : cutFaces) msg += std::string(" ") + faceNames[f];
    logInfo(msg);

    return cutFaces;
}

std::string getOpeningFace(const Point3& center, int nx, int ny, int nz, double threshold) {
    double x = center[0], y = center[1], z = center[2];
    struct FaceDist { std::string name; double dist; };
    std::vector<FaceDist> faces;

    if (x < threshold) faces.push_back(FaceDist{"X-", x});
    if (x > nx - threshold) faces.push_back(FaceDist{"X+", (double)nx - x});
    if (y < threshold) faces.push_back(FaceDist{"Y-", y});
    if (y > ny - threshold) faces.push_back(FaceDist{"Y+", (double)ny - y});
    if (z < threshold) faces.push_back(FaceDist{"Z-", z});
    if (z > nz - threshold) faces.push_back(FaceDist{"Z+", (double)nz - z});

    if (faces.empty()) return "";
    if (faces.size() == 1) return faces[0].name;

    // Return closest face
    auto it = std::min_element(faces.begin(), faces.end(),
        [](const FaceDist& a, const FaceDist& b) { return a.dist < b.dist; });
    return it->name;
}

// ============================================================================
// Wall creation (porting wall_creation.py)
// ============================================================================

// roll_zeropad for a Vol3D along a given axis
static Vol3D rollZeropad(const Vol3D& a, int shift, int axis) {
    if (shift == 0) return a;

    Vol3D res(a.nx, a.ny, a.nz, 0);
    int n = (axis == 0) ? a.nx : (axis == 1) ? a.ny : a.nz;

    for (int x = 0; x < a.nx; ++x) {
        for (int y = 0; y < a.ny; ++y) {
            for (int z = 0; z < a.nz; ++z) {
                int srcIdx[3] = {x, y, z};
                int dstIdx[3] = {x, y, z};

                int srcPos = srcIdx[axis];
                int dstPos = srcPos + shift;
                if (dstPos < 0 || dstPos >= n) continue;
                dstIdx[axis] = dstPos;
                res(dstIdx[0], dstIdx[1], dstIdx[2]) = a(x, y, z);
            }
        }
    }
    return res;
}

static Vol3D removeUnusedOuterLayers(const Vol3D& data, std::vector<int>& sliced) {
    int nx = data.nx, ny = data.ny, nz = data.nz;
    sliced = {0, nx, 0, ny, 0, nz};

    // X- boundary
    while (sliced[0] < sliced[1]) {
        bool allZero = true;
        for (int y = sliced[2]; y < sliced[3] && allZero; ++y)
            for (int z = sliced[4]; z < sliced[5] && allZero; ++z)
                if (data(sliced[0], y, z) != 0) allZero = false;
        if (!allZero) break;
        sliced[0]++;
    }
    // X+ boundary
    while (sliced[1] > sliced[0]) {
        bool allZero = true;
        for (int y = sliced[2]; y < sliced[3] && allZero; ++y)
            for (int z = sliced[4]; z < sliced[5] && allZero; ++z)
                if (data(sliced[1] - 1, y, z) != 0) allZero = false;
        if (!allZero) break;
        sliced[1]--;
    }
    // Y- boundary
    while (sliced[2] < sliced[3]) {
        bool allZero = true;
        for (int x = sliced[0]; x < sliced[1] && allZero; ++x)
            for (int z = sliced[4]; z < sliced[5] && allZero; ++z)
                if (data(x, sliced[2], z) != 0) allZero = false;
        if (!allZero) break;
        sliced[2]++;
    }
    // Y+ boundary
    while (sliced[3] > sliced[2]) {
        bool allZero = true;
        for (int x = sliced[0]; x < sliced[1] && allZero; ++x)
            for (int z = sliced[4]; z < sliced[5] && allZero; ++z)
                if (data(x, sliced[3] - 1, z) != 0) allZero = false;
        if (!allZero) break;
        sliced[3]--;
    }
    // Z- boundary
    while (sliced[4] < sliced[5]) {
        bool allZero = true;
        for (int x = sliced[0]; x < sliced[1] && allZero; ++x)
            for (int y = sliced[2]; y < sliced[3] && allZero; ++y)
                if (data(x, y, sliced[4]) != 0) allZero = false;
        if (!allZero) break;
        sliced[4]++;
    }
    // Z+ boundary
    while (sliced[5] > sliced[4]) {
        bool allZero = true;
        for (int x = sliced[0]; x < sliced[1] && allZero; ++x)
            for (int y = sliced[2]; y < sliced[3] && allZero; ++y)
                if (data(x, y, sliced[5] - 1) != 0) allZero = false;
        if (!allZero) break;
        sliced[5]--;
    }

    int nnx = sliced[1] - sliced[0];
    int nny = sliced[3] - sliced[2];
    int nnz = sliced[5] - sliced[4];
    Vol3D result(nnx, nny, nnz);
    for (int x = 0; x < nnx; ++x)
        for (int y = 0; y < nny; ++y)
            for (int z = 0; z < nnz; ++z)
                result(x, y, z) = data(x + sliced[0], y + sliced[2], z + sliced[4]);

    return result;
}

Vol3D applyBoundaryCuts(const Vol3D& volume, const std::vector<int>& cutList, int cutWidth) {
    int x0 = 0, x1 = volume.nx;
    int y0 = 0, y1 = volume.ny;
    int z0 = 0, z1 = volume.nz;

    for (int face : cutList) {
        switch (face) {
            case 0: x0 += cutWidth; break;  // X-
            case 1: x1 -= cutWidth; break;  // X+
            case 2: y0 += cutWidth; break;  // Y-
            case 3: y1 -= cutWidth; break;  // Y+
            case 4: z0 += cutWidth; break;  // Z-
            case 5: z1 -= cutWidth; break;  // Z+
        }
    }

    int nnx = x1 - x0, nny = y1 - y0, nnz = z1 - z0;
    Vol3D result(nnx, nny, nnz);
    for (int x = 0; x < nnx; ++x)
        for (int y = 0; y < nny; ++y)
            for (int z = 0; z < nnz; ++z)
                result(x, y, z) = volume(x + x0, y + y0, z + z0);

    return result;
}

BoolVol3D applyBoundaryCutsBool(const BoolVol3D& volume, const std::vector<int>& cutList, int cutWidth) {
    int x0 = 0, x1 = volume.nx;
    int y0 = 0, y1 = volume.ny;
    int z0 = 0, z1 = volume.nz;

    for (int face : cutList) {
        switch (face) {
            case 0: x0 += cutWidth; break;
            case 1: x1 -= cutWidth; break;
            case 2: y0 += cutWidth; break;
            case 3: y1 -= cutWidth; break;
            case 4: z0 += cutWidth; break;
            case 5: z1 -= cutWidth; break;
        }
    }

    int nnx = x1 - x0, nny = y1 - y0, nnz = z1 - z0;
    BoolVol3D result(nnx, nny, nnz);
    for (int x = 0; x < nnx; ++x)
        for (int y = 0; y < nny; ++y)
            for (int z = 0; z < nnz; ++z)
                result(x, y, z) = volume(x + x0, y + y0, z + z0);

    return result;
}

Vol3D createWalls(const BoolVol3D& input, const std::vector<int>& cutList,
                  int cutWidth, std::vector<int>& sliced) {
    int nx = input.nx, ny = input.ny, nz = input.nz;

    // data1 = input (bool)
    // data2 = copy of data1

    // Morphological dilation via 3x3x3 neighborhood OR
    logInfo("Calculating boundary...");

    // We need two int volumes for the OR operations
    Vol3D data2(nx, ny, nz, 0);
    // Copy input to data2
    for (int x = 0; x < nx; ++x)
        for (int y = 0; y < ny; ++y)
            for (int z = 0; z < nz; ++z)
                data2(x, y, z) = input(x, y, z) ? 1 : 0;

    // Dilate: for each shift in [-1,0,1]^3, OR into data2
    // We need to build the input as a Vol3D first
    Vol3D data1(nx, ny, nz, 0);
    for (int x = 0; x < nx; ++x)
        for (int y = 0; y < ny; ++y)
            for (int z = 0; z < nz; ++z)
                data1(x, y, z) = input(x, y, z) ? 1 : 0;

    for (int l = -1; l <= 1; ++l) {
        Vol3D a = rollZeropad(data1, l, 0);
        for (int m = -1; m <= 1; ++m) {
            Vol3D b = rollZeropad(a, m, 1);
            for (int n = -1; n <= 1; ++n) {
                Vol3D c = rollZeropad(b, n, 2);
                for (size_t i = 0; i < data2.data.size(); ++i)
                    data2.data[i] |= c.data[i];
            }
        }
    }

    // data3 = data2 - data1  (wall = dilated - original)
    Vol3D data3(nx, ny, nz, 0);
    for (size_t i = 0; i < data3.data.size(); ++i)
        data3.data[i] = data2.data[i] - data1.data[i];

    // Paint fluid cells back: where data1==1, set data3=2
    for (size_t i = 0; i < data3.data.size(); ++i)
        if (data1.data[i] == 1) data3.data[i] = VOXEL_FLUID;

    // Remove unused outer layers
    data3 = removeUnusedOuterLayers(data3, sliced);

    // Apply boundary cuts
    data3 = applyBoundaryCuts(data3, cutList, cutWidth);

    return data3;
}

// ============================================================================
// Opening detection (porting opening_detection.py)
// ============================================================================

static bool hasNeighboringUnusedVoxel(int x, int y, int z, const Vol3D& data) {
    // data is indexed as data(z, x, y) in Python but data(x,y,z) in C++
    // In Python: data[z-1][x][y], data[z+1][x][y], etc.
    // Since Python uses [z][x][y] indexing, our C++ Vol3D(x,y,z) maps differently.
    // We need to match the Python semantics exactly.
    //
    // Python data is [z][x][y] which in our Vol3D means the first dim is z, second is x, third is y.
    // So Python data[z][x][y] = our padData(z, x, y) where padData has dims (nz, nx, ny).
    //
    // Rather than re-indexing everything, we'll pass the data with the Python convention:
    // the Vol3D created in detectOpenings will have (nz, nx, ny) dimensions, and we'll
    // access it as data(z, x, y).

    if (data(z - 1, x, y) == VOXEL_UNUSED) return true;
    if (data(z + 1, x, y) == VOXEL_UNUSED) return true;
    if (data(z, x - 1, y) == VOXEL_UNUSED) return true;
    if (data(z, x + 1, y) == VOXEL_UNUSED) return true;
    if (data(z, x, y - 1) == VOXEL_UNUSED) return true;
    if (data(z, x, y + 1) == VOXEL_UNUSED) return true;
    return false;
}

static std::string getNeighbouringUnusedVoxelPlane(int x, int y, int z, const Vol3D& data) {
    if (data(z - 1, x, y) == VOXEL_UNUSED || data(z + 1, x, y) == VOXEL_UNUSED) return "z";
    if (data(z, x - 1, y) == VOXEL_UNUSED || data(z, x + 1, y) == VOXEL_UNUSED) return "x";
    if (data(z, x, y - 1) == VOXEL_UNUSED || data(z, x, y + 1) == VOXEL_UNUSED) return "y";
    return "";
}

// Iterative flood fill (replacing Python's recursive version)
static std::vector<VoxelCoord> findInletOutlet(int x, int y, int z, const Vol3D& data,
                                                const std::string& plane) {
    std::vector<VoxelCoord> result;
    std::set<VoxelCoord> visited;

    std::stack<VoxelCoord> st;
    st.push(std::make_tuple(x, y, z));
    visited.insert(std::make_tuple(x, y, z));

    std::vector<int> dzList = {-1, 0, 1};
    std::vector<int> dxList = {-1, 0, 1};
    std::vector<int> dyList = {-1, 0, 1};
    if (plane == "z") dzList = {0};
    if (plane == "x") dxList = {0};
    if (plane == "y") dyList = {0};

    while (!st.empty()) {
        auto [cx, cy, cz] = st.top();
        st.pop();
        result.push_back(std::make_tuple(cx, cy, cz));

        for (int dz : dzList) {
            for (int dx : dxList) {
                for (int dy : dyList) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    int nx_ = cx + dx, ny_ = cy + dy, nz_ = cz + dz;
                    VoxelCoord nc = std::make_tuple(nx_, ny_, nz_);
                    if (visited.count(nc)) continue;
                    if (data(nz_, nx_, ny_) == VOXEL_FLUID &&
                        hasNeighboringUnusedVoxel(nx_, ny_, nz_, data)) {
                        visited.insert(nc);
                        st.push(nc);
                    }
                }
            }
        }
    }

    return result;
}

OpeningDetectionResult detectOpenings(const Vol3D& inputArray) {
    // inputArray is [x][y][z] — but Python code uses [z][x][y].
    // We need to create a padded version in Python's [z][x][y] order.

    int inx = inputArray.nx, iny = inputArray.ny, inz = inputArray.nz;

    // Pad with 1 layer of UNUSED on all sides
    // In Python: data = np.pad(inputArray, 1, 'constant')
    // Python indexing is [z][x][y], so we need to think about this carefully.
    // In Python, inputArray has shape (nx, ny, nz) — or does it?
    // Actually in the Python code, wall_creation outputs data3 as [x][y][z] (from volume ops).
    // Then detectOpenings receives it and uses data[z][x][y] indexing.
    // Wait — the Python code has the volume as a numpy array. Let me reread.
    //
    // Python wall_creation operates on numpy arrays where shape is (nx, ny, nz).
    // Then opening_detection.py uses data[z][x][y] — this means data is indexed
    // with the first axis being z. But the wall_creation output shape is (nx, ny, nz).
    //
    // Wait, Python np.pad(inputArray, 1) adds 1 to each dimension.
    // Then data[z-1][x][y] — so the array is treated as (z, x, y) shape.
    //
    // But the input comes from wall_creation which produces shape (nx, ny, nz).
    // This means the Python code is using [x][y][z] indexing but writing data[z][x][y],
    // which means data[first_dim][second_dim][third_dim] = data[x_dim][y_dim][z_dim].
    //
    // So Python data[z][x][y] = C++ data(z, x, y) where data has shape (first=nx, second=ny, third=nz)
    // and z iterates over first dim, x over second, y over third.
    // This means Python is actually using the first dimension as "z", second as "x", third as "y"
    // even though the shape is (nx, ny, nz).
    //
    // Actually I think the Python code just uses [z][x][y] as index names but the actual
    // numpy array dimensions are whatever they are. The variable names z, x, y in the
    // opening detection are just iteration variables. The key thing is:
    //   - np.where(data == WALL) returns indices for each dimension
    //   - These are used as (z, x, y) in wall_voxels
    //   - Then checked as data[z-1][x][y] etc.
    //
    // So the convention in opening_detection.py is: first dim = "z", second dim = "x", third dim = "y"
    // Our Vol3D in createWalls has shape (nx, ny, nz) — first dim = x, second = y, third = z.
    // So we need to transpose for opening detection.

    // Create padded data in Python's [z][x][y] order, which is our [dim0+1][dim1+1][dim2+1]
    // from inputArray with dims (nx, ny, nz).
    // Python pads all dims: padded shape = (nx+2, ny+2, nz+2).
    // Python treats dim0 as z, dim1 as x, dim2 as y.
    // So padded.dim0 = nx+2 acts as z, padded.dim1 = ny+2 acts as x, padded.dim2 = nz+2 acts as y.

    int pnz = inx + 2; // Python's z-dim = our x-dim + 2
    int pnx = iny + 2; // Python's x-dim = our y-dim + 2
    int pny = inz + 2; // Python's y-dim = our z-dim + 2

    Vol3D padData(pnz, pnx, pny, VOXEL_UNUSED);
    for (int x = 0; x < inx; ++x)
        for (int y = 0; y < iny; ++y)
            for (int z = 0; z < inz; ++z)
                padData(x + 1, y + 1, z + 1) = inputArray(x, y, z);

    // Detect all fluid voxels adjacent to wall voxels that also neighbor unused
    logInfo("Detecting all fluid voxels surrounded by an unused voxel...");

    // Find all wall voxels (iterate over padded data)
    // In Python: wall_voxels = zip(*np.where(data == WALL)) -> (z, x, y) tuples
    struct FluidVoxel { int x, y, z; };
    std::vector<FluidVoxel> foundFluidVoxels;

    for (int pz = 1; pz < pnz - 1; ++pz) {
        for (int px = 1; px < pnx - 1; ++px) {
            for (int py = 1; py < pny - 1; ++py) {
                if (padData(pz, px, py) != VOXEL_WALL) continue;
                // Check 6-neighbors for fluid with neighboring unused
                // Python checks data[z-1][x][y], data[z+1][x][y], data[z][x-1][y], etc.
                if (padData(pz - 1, px, py) == VOXEL_FLUID) {
                    if (hasNeighboringUnusedVoxel(px, py, pz - 1, padData))
                        foundFluidVoxels.push_back(FluidVoxel{px, py, pz - 1});
                } else if (padData(pz + 1, px, py) == VOXEL_FLUID) {
                    if (hasNeighboringUnusedVoxel(px, py, pz + 1, padData))
                        foundFluidVoxels.push_back(FluidVoxel{px, py, pz + 1});
                } else if (padData(pz, px - 1, py) == VOXEL_FLUID) {
                    if (hasNeighboringUnusedVoxel(px - 1, py, pz, padData))
                        foundFluidVoxels.push_back(FluidVoxel{px - 1, py, pz});
                } else if (padData(pz, px + 1, py) == VOXEL_FLUID) {
                    if (hasNeighboringUnusedVoxel(px + 1, py, pz, padData))
                        foundFluidVoxels.push_back(FluidVoxel{px + 1, py, pz});
                } else if (padData(pz, px, py - 1) == VOXEL_FLUID) {
                    if (hasNeighboringUnusedVoxel(px, py - 1, pz, padData))
                        foundFluidVoxels.push_back(FluidVoxel{px, py - 1, pz});
                } else if (padData(pz, px, py + 1) == VOXEL_FLUID) {
                    if (hasNeighboringUnusedVoxel(px, py + 1, pz, padData))
                        foundFluidVoxels.push_back(FluidVoxel{px, py + 1, pz});
                }
            }
        }
    }

    // Process found fluid voxels — group connected ones into openings
    std::vector<std::vector<VoxelCoord>> inlets_outlets;

    // Check if voxel already processed
    auto isProcessed = [&](int x, int y, int z) -> bool {
        VoxelCoord vc = {x, y, z};
        for (auto& io : inlets_outlets)
            for (auto& v : io)
                if (v == vc) return true;
        return false;
    };

    for (auto& fv : foundFluidVoxels) {
        if (!isProcessed(fv.x, fv.y, fv.z)) {
            std::string plane = getNeighbouringUnusedVoxelPlane(fv.x, fv.y, fv.z, padData);
            auto opening = findInletOutlet(fv.x, fv.y, fv.z, padData, plane);
            if (opening.size() > 1) {
                inlets_outlets.push_back(opening);
            }
        }
    }

    OpeningDetectionResult result;
    result.inlets_outlets = inlets_outlets;
    result.data = padData;
    return result;
}

PaintResult paintInletsOutlets(const std::vector<std::vector<VoxelCoord>>& inlets_outlets,
                               const Vol3D& data, bool findBoundaryByArea) {
    int numOpenings = (int)inlets_outlets.size();
    std::vector<int16_t> openingIdx(numOpenings, 0);

    Vol3D dataResult = data; // copy

    int inletIdx, pressureOutletIdx;
    if (findBoundaryByArea) {
        // Largest area = inlet
        inletIdx = 0;
        pressureOutletIdx = 0;
        for (int i = 1; i < numOpenings; ++i) {
            if (inlets_outlets[i].size() > inlets_outlets[inletIdx].size()) inletIdx = i;
            if (inlets_outlets[i].size() < inlets_outlets[pressureOutletIdx].size()) pressureOutletIdx = i;
        }
    } else {
        inletIdx = 0;
        pressureOutletIdx = numOpenings - 1;
    }

    openingIdx[inletIdx] = VOXEL_INLET;
    openingIdx[pressureOutletIdx] = VOXEL_OUTLET;

    int outletCount = 0;
    for (int i = 0; i < numOpenings; ++i) {
        if (openingIdx[i] == 0) {
            openingIdx[i] = VOXEL_OUTLET_REST + outletCount;
            outletCount++;
        }
    }

    logInfo("Number of inlets: 1");
    logInfo("Number of pressure outlets: 1");
    logInfo(std::string("Number of velocity outlet(s): ") + std::to_string(numOpenings - 2));

    std::vector<Point3> openingCenter;
    for (int ioID = 0; ioID < numOpenings; ++ioID) {
        double cx = 0, cy = 0, cz = 0;
        for (auto& [x, y, z] : inlets_outlets[ioID]) {
            dataResult(z, x, y) = openingIdx[ioID];
            cz += z; cx += x; cy += y;
        }
        int n = (int)inlets_outlets[ioID].size();
        openingCenter.push_back(Point3{{cz / n, cx / n, cy / n}});
    }

    // Remove outer padding layer: data[1:-1, 1:-1, 1:-1]
    int onx = data.nx - 2, ony = data.ny - 2, onz = data.nz - 2;
    Vol3D stripped(onx, ony, onz);
    for (int x = 0; x < onx; ++x)
        for (int y = 0; y < ony; ++y)
            for (int z = 0; z < onz; ++z)
                stripped(x, y, z) = dataResult(x + 1, y + 1, z + 1);

    return {openingIdx, openingCenter, stripped};
}

// ============================================================================
// Rotation (porting rotation.py)
// ============================================================================

static double dot3(const Point3& a, const Point3& b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static Point3 cross3(const Point3& a, const Point3& b) {
    return {a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0]};
}

static bool allClose(const Point3& a, const Point3& b, double tol = 1e-8) {
    return std::abs(a[0]-b[0]) < tol && std::abs(a[1]-b[1]) < tol && std::abs(a[2]-b[2]) < tol;
}

// Matrix multiply: result = R * v  (R is row-major 3x3)
static Point3 matVec(const double R[9], const Point3& v) {
    return {R[0]*v[0] + R[1]*v[1] + R[2]*v[2],
            R[3]*v[0] + R[4]*v[1] + R[5]*v[2],
            R[6]*v[0] + R[7]*v[1] + R[8]*v[2]};
}

void calculateRotationMatrix(const Point3& sourceVec, const Point3& targetVec, double R[9]) {
    Point3 source = normalize(sourceVec);
    Point3 target = normalize(targetVec);

    Point3 negTarget = {-target[0], -target[1], -target[2]};

    if (allClose(source, target)) {
        // Identity
        double I[9] = {1,0,0, 0,1,0, 0,0,1};
        std::memcpy(R, I, sizeof(I));
        return;
    }

    if (allClose(source, negTarget)) {
        // 180 degree rotation
        Point3 perp;
        if (std::abs(source[0]) < 0.9) perp = {1, 0, 0};
        else perp = {0, 1, 0};
        double d = dot3(perp, source);
        perp = {perp[0] - d * source[0], perp[1] - d * source[1], perp[2] - d * source[2]};
        perp = normalize(perp);

        // R = 2 * outer(perp, perp) - I
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                R[i*3+j] = 2.0 * perp[i] * perp[j] - (i == j ? 1.0 : 0.0);
        return;
    }

    // Rodrigues' rotation formula
    Point3 v = cross3(source, target);
    double c = dot3(source, target);
    double s = vecNorm(v);

    // Skew-symmetric cross-product matrix
    double K[9] = {0, -v[2], v[1],
                   v[2], 0, -v[0],
                   -v[1], v[0], 0};

    // K*K
    double K2[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            K2[i*3+j] = 0;
            for (int k = 0; k < 3; ++k)
                K2[i*3+j] += K[i*3+k] * K[k*3+j];
        }

    double factor = (1.0 - c) / (s * s);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            R[i*3+j] = (i == j ? 1.0 : 0.0) + K[i*3+j] + K2[i*3+j] * factor;
}

Point3 parseTargetAxis(const std::string& axisString) {
    std::string s = axisString;
    // Trim and lowercase
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    for (auto& c : s) c = tolower(c);

    if (s.size() != 2 || (s[0] != '+' && s[0] != '-') ||
        (s[1] != 'x' && s[1] != 'y' && s[1] != 'z'))
        throw std::runtime_error("Invalid axis string: " + axisString);

    double sign = (s[0] == '+') ? 1.0 : -1.0;
    int idx = (s[1] == 'x') ? 0 : (s[1] == 'y') ? 1 : 2;
    Point3 vec = {0, 0, 0};
    vec[idx] = sign;
    return vec;
}

void rotateStl(const std::string& inPath, const double R[9], const std::string& outPath) {
    StlMesh mesh = readStlBinary(inPath);
    for (auto& tri : mesh.triangles) {
        float* verts[3] = {tri.v0, tri.v1, tri.v2};
        for (int v = 0; v < 3; ++v) {
            Point3 p = {(double)verts[v][0], (double)verts[v][1], (double)verts[v][2]};
            Point3 rp = matVec(R, p);
            verts[v][0] = (float)rp[0]; verts[v][1] = (float)rp[1]; verts[v][2] = (float)rp[2];
        }
        // Recalculate normal
        Point3 v0 = {(double)tri.v0[0], (double)tri.v0[1], (double)tri.v0[2]};
        Point3 v1 = {(double)tri.v1[0], (double)tri.v1[1], (double)tri.v1[2]};
        Point3 v2 = {(double)tri.v2[0], (double)tri.v2[1], (double)tri.v2[2]};
        Point3 e1 = {v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]};
        Point3 e2 = {v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]};
        Point3 n = cross3(e1, e2);
        n = normalize(n);
        tri.normal[0] = (float)n[0]; tri.normal[1] = (float)n[1]; tri.normal[2] = (float)n[2];
    }
    writeStlBinary(outPath, mesh);
}

void rotateVtp(const std::string& inPath, const double R[9], const std::string& outPath) {
    VtpCenterline cl = readVtp(inPath);

    // Rotate points
    for (auto& p : cl.points) {
        Point3 rp = matVec(R, p);
        p = rp;
    }

    writeVtp(outPath, cl);
}

void translateStl(const std::string& inPath, const Point3& translation, const std::string& outPath) {
    StlMesh mesh = readStlBinary(inPath);
    for (auto& tri : mesh.triangles) {
        float* verts[3] = {tri.v0, tri.v1, tri.v2};
        for (int v = 0; v < 3; ++v) {
            verts[v][0] += (float)translation[0];
            verts[v][1] += (float)translation[1];
            verts[v][2] += (float)translation[2];
        }
    }
    writeStlBinary(outPath, mesh);
}

void translateVtp(const std::string& inPath, const Point3& translation, const std::string& outPath) {
    VtpCenterline cl = readVtp(inPath);
    for (auto& p : cl.points) {
        p[0] += translation[0];
        p[1] += translation[1];
        p[2] += translation[2];
    }
    writeVtp(outPath, cl);
}

void getStlBounds(const std::string& stlPath, Point3& minCoords, Point3& maxCoords) {
    StlMesh mesh = readStlBinary(stlPath);
    minCoords = {1e30, 1e30, 1e30};
    maxCoords = {-1e30, -1e30, -1e30};
    for (auto& tri : mesh.triangles) {
        const float* verts[3] = {tri.v0, tri.v1, tri.v2};
        for (int v = 0; v < 3; ++v)
            for (int d = 0; d < 3; ++d) {
                minCoords[d] = std::min(minCoords[d], (double)verts[v][d]);
                maxCoords[d] = std::max(maxCoords[d], (double)verts[v][d]);
            }
    }
}

void getCenterlineOpening(const std::string& vtpPath, int openingIndex,
                          Point3& position, Point3& normal) {
    VtpCenterline cl = readVtp(vtpPath);
    int lineIdx = 0;

    for (auto& line : cl.lines) {
        int numPoints = (int)line.size();

        if (openingIndex == 0 && lineIdx == 0) {
            // First opening: start of first line
            int pid = line[0];
            position = cl.points[pid];

            // Approximate normal from first two points
            Point3 nextPt = cl.points[line[1]];
            normal = {position[0] - nextPt[0], position[1] - nextPt[1], position[2] - nextPt[2]};
            normal = normalize(normal);
            return;
        } else if (lineIdx + 1 == openingIndex) {
            // Other openings: end of line
            int pid = line[numPoints - 1];
            position = cl.points[pid];

            int prevPid = line[numPoints - 2];
            Point3 prevPt = cl.points[prevPid];
            normal = {position[0] - prevPt[0], position[1] - prevPt[1], position[2] - prevPt[2]};
            normal = normalize(normal);
            return;
        }
        lineIdx++;
    }

    throw std::runtime_error("Opening index " + std::to_string(openingIndex) +
                             " not found in centerline");
}

void rotateGeometryToAlignInlet(const std::string& stlPath, const std::string& vtpPath,
                                 const std::string& targetAxis,
                                 const std::string& outputStl, const std::string& outputVtp,
                                 int inletIndex, bool positionAtBoundary,
                                 double R_out[9], Point3& translation_out) {
    logInfo("Starting geometry rotation and alignment");

    Point3 boundaryFaceVector = parseTargetAxis(targetAxis);
    Point3 targetNormalDirection = {-boundaryFaceVector[0], -boundaryFaceVector[1], -boundaryFaceVector[2]};

    Point3 inletPos, inletNormal;
    getCenterlineOpening(vtpPath, inletIndex, inletPos, inletNormal);

    // inlet_normal points INTO vessel, -inlet_normal points OUT of vessel (into domain)
    Point3 negInletNormal = {-inletNormal[0], -inletNormal[1], -inletNormal[2]};
    calculateRotationMatrix(negInletNormal, targetNormalDirection, R_out);

    // Temp files
    std::string tempStl = outputStl + ".temp.stl";
    std::string tempVtp = outputVtp + ".temp.vtp";

    rotateStl(stlPath, R_out, tempStl);
    rotateVtp(vtpPath, R_out, tempVtp);

    translation_out = {0, 0, 0};

    if (positionAtBoundary) {
        Point3 rotatedInletPos = matVec(R_out, inletPos);

        Point3 minC, maxC;
        getStlBounds(tempStl, minC, maxC);

        int axisIdx = 0;
        if (std::abs(boundaryFaceVector[1]) > std::abs(boundaryFaceVector[axisIdx])) axisIdx = 1;
        if (std::abs(boundaryFaceVector[2]) > std::abs(boundaryFaceVector[axisIdx])) axisIdx = 2;

        double targetCoord = (boundaryFaceVector[axisIdx] > 0) ? maxC[axisIdx] : minC[axisIdx];
        translation_out[axisIdx] = targetCoord - rotatedInletPos[axisIdx];

        translateStl(tempStl, translation_out, outputStl);
        translateVtp(tempVtp, translation_out, outputVtp);

        removeFile(tempStl);
        removeFile(tempVtp);
    } else {
        renameFile(tempStl, outputStl);
        renameFile(tempVtp, outputVtp);
    }

    logInfo("Geometry rotation and alignment complete");
}

// ============================================================================
// Mesh output (porting mesh_output.py)
// ============================================================================

void transformAndSaveStl(const std::string& inputStl, const std::string& outputStl,
                         double siFactor, const float* bboxMin) {
    StlMesh mesh = readStlBinary(inputStl);

    float bmin[3];
    if (bboxMin) {
        bmin[0] = bboxMin[0]; bmin[1] = bboxMin[1]; bmin[2] = bboxMin[2];
    } else {
        // Calculate from mesh
        bmin[0] = bmin[1] = bmin[2] = 1e30f;
        for (auto& tri : mesh.triangles) {
            const float* verts[3] = {tri.v0, tri.v1, tri.v2};
            for (int v = 0; v < 3; ++v)
                for (int d = 0; d < 3; ++d)
                    bmin[d] = std::min(bmin[d], verts[v][d]);
        }
    }

    for (auto& tri : mesh.triangles) {
        float* verts[3] = {tri.v0, tri.v1, tri.v2};
        for (int v = 0; v < 3; ++v)
            for (int d = 0; d < 3; ++d) {
                verts[v][d] -= bmin[d];
                verts[v][d] *= (float)siFactor;
            }
        // Recalculate normal
        Point3 v0 = {(double)tri.v0[0], (double)tri.v0[1], (double)tri.v0[2]};
        Point3 v1 = {(double)tri.v1[0], (double)tri.v1[1], (double)tri.v1[2]};
        Point3 v2 = {(double)tri.v2[0], (double)tri.v2[1], (double)tri.v2[2]};
        Point3 e1 = {v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]};
        Point3 e2 = {v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]};
        Point3 n = cross3(e1, e2);
        n = normalize(n);
        tri.normal[0] = (float)n[0]; tri.normal[1] = (float)n[1]; tri.normal[2] = (float)n[2];
    }

    writeStlBinary(outputStl, mesh);
    logInfo(std::string("  Saved rescaled STL: ") + outputStl);
}

void saveRescaledMeshes(const PreprocessorConfig& config) {
    if (!config.output_meshes.enabled) return;

    logInfo("Outputting rescaled surface meshes in SI units");

    std::string vesselStl = config.resolvePath(config.geometry_stl);
    std::string outputDir = config.resolvePath(config.output_dir);
    ensureDirectoryExists(outputDir);

    // Calculate bounding box from vessel
    Point3 bboxMin, bboxMax;
    getStlBounds(vesselStl, bboxMin, bboxMax);

    float bmin[3] = {(float)bboxMin[0], (float)bboxMin[1], (float)bboxMin[2]};

    if (config.output_meshes.vasculature) {
        std::string outVessel = pathJoin(outputDir, config.output_base_name + "vessel_SI.stl");
        logInfo("Saving rescaled vessel mesh:");
        transformAndSaveStl(vesselStl, outVessel, config.si_factor, bmin);
    }

    if (config.hasCoil() && config.output_meshes.coil) {
        std::string coilStl = config.resolvePath(config.coil_stl);
        std::string outCoil = pathJoin(outputDir, config.output_base_name + "coil_SI.stl");
        logInfo("Saving rescaled coil mesh:");
        transformAndSaveStl(coilStl, outCoil, config.si_factor, bmin);
    }
}

// ============================================================================
// NPZ output (using cnpy)
// ============================================================================

void saveGeometry(const PreprocessorConfig& config,
                  const GeometryResult& geom,
                  const VoxelizationResult& voxResult,
                  const StentResult* stentResult) {
    logInfo("Saving final output");

    std::string outputDir = config.resolvePath(config.output_dir);
    ensureDirectoryExists(outputDir);
    std::string outputPath = pathJoin(outputDir, config.output_base_name + "c.npz");
    logInfo(std::string("  File: ") + outputPath);

    // Remove existing file to write fresh
    removeFile(outputPath);

    // geometryFlag: 3D array of int16_t, shape (nx, ny, nz)
    {
        std::vector<size_t> shape = {(size_t)geom.volume.nx,
                                     (size_t)geom.volume.ny,
                                     (size_t)geom.volume.nz};
        cnpy::npz_save(outputPath, "geometryFlag", geom.volume.data.data(), shape, "w");
    }

    // dx: double array [1]
    {
        std::vector<double> dx = {voxResult.dx};
        cnpy::npz_save(outputPath, "dx", dx.data(), {1}, "a");
    }

    // openingIndex: int16_t array
    {
        cnpy::npz_save(outputPath, "openingIndex",
                       geom.opening_index.data(),
                       {geom.opening_index.size()}, "a");
    }

    // openingRadius: double array
    {
        cnpy::npz_save(outputPath, "openingRadius",
                       geom.opening_radius.data(),
                       {geom.opening_radius.size()}, "a");
    }

    // openingNormalizedQRatio: double array
    {
        cnpy::npz_save(outputPath, "openingNormalizedQRatio",
                       geom.opening_normalized_q_ratio.data(),
                       {geom.opening_normalized_q_ratio.size()}, "a");
    }

    // openingCenter: double array, shape (nOpenings, 3)
    {
        std::vector<double> flat;
        for (auto& c : geom.opening_center) {
            flat.push_back(c[0]); flat.push_back(c[1]); flat.push_back(c[2]);
        }
        cnpy::npz_save(outputPath, "openingCenter",
                       flat.data(),
                       {geom.opening_center.size(), 3}, "a");
    }

    // openingNormal: double array, shape (nOpenings, 3)
    {
        std::vector<double> flat;
        for (auto& n : geom.opening_normal) {
            flat.push_back(n[0]); flat.push_back(n[1]); flat.push_back(n[2]);
        }
        cnpy::npz_save(outputPath, "openingNormal",
                       flat.data(),
                       {geom.opening_normal.size(), 3}, "a");
    }

    // Stent data
    if (stentResult) {
        std::vector<size_t> shape = {(size_t)stentResult->volume.nx,
                                     (size_t)stentResult->volume.ny,
                                     (size_t)stentResult->volume.nz};
        cnpy::npz_save(outputPath, "stent", stentResult->volume.data.data(), shape, "a");
        cnpy::npz_save(outputPath, "linear", stentResult->linear.data.data(), shape, "a");
        cnpy::npz_save(outputPath, "quadratic", stentResult->quadratic.data.data(), shape, "a");
    }
}

// ============================================================================
// Pipeline orchestration (porting pipeline.py)
// ============================================================================

static void applyGeometryRotation(PreprocessorConfig& config) {
    if (!config.rotation.enabled) return;

    logInfo("Applying geometry rotation preprocessing");

    std::string stlPath = config.resolvePath(config.geometry_stl);
    std::string vtpPath = config.resolvePath(config.centerline_vtp);

    std::string outputDir = config.resolvePath(config.output_dir);
    ensureDirectoryExists(outputDir);

    std::string stlName = pathStem(stlPath);
    std::string vtpName = pathStem(vtpPath);
    std::string stlExt = pathExtension(stlPath);
    std::string vtpExt = pathExtension(vtpPath);

    std::string outputStl = pathJoin(outputDir, stlName + "_rotated" + stlExt);
    std::string outputVtp = pathJoin(outputDir, vtpName + "_rotated" + vtpExt);

    logInfo(std::string("  Input STL: ") + stlPath);
    logInfo(std::string("  Input VTP: ") + vtpPath);
    logInfo(std::string("  Target axis: ") + config.rotation.inlet_target_axis);

    double R[9];
    Point3 translation;
    rotateGeometryToAlignInlet(stlPath, vtpPath, config.rotation.inlet_target_axis,
                                outputStl, outputVtp,
                                config.rotation.inlet_centerline_index,
                                config.rotation.position_at_boundary,
                                R, translation);

    config.geometry_stl = outputStl;
    config.centerline_vtp = outputVtp;

    logInfo(std::string("  Rotated STL saved to: ") + outputStl);
    logInfo(std::string("  Rotated VTP saved to: ") + outputVtp);

    // Apply same transformation to coil if present
    if (config.hasCoil()) {
        logInfo("  Applying same transformation to coil geometry");
        std::string coilPath = config.resolvePath(config.coil_stl);
        std::string coilName = pathStem(coilPath);
        std::string coilExt = pathExtension(coilPath);
        std::string outputCoil = pathJoin(outputDir, coilName + "_rotated" + coilExt);

        std::string tempCoil = pathJoin(outputDir, coilName + "_temp" + coilExt);
        rotateStl(coilPath, R, tempCoil);

        if (config.rotation.position_at_boundary) {
            translateStl(tempCoil, translation, outputCoil);
            removeFile(tempCoil);
        } else {
            renameFile(tempCoil, outputCoil);
        }

        config.coil_stl = outputCoil;
        logInfo(std::string("  Rotated coil saved to: ") + outputCoil);
    }
}

static VoxelizationResult voxelizeGeometry(const PreprocessorConfig& config) {
    logInfo("Voxelizing vessel geometry");

    std::string vesselStl = config.resolvePath(config.geometry_stl);

    DomainData outDomain;
    BoolVol3D voxelVol = voxelize(vesselStl, config.target_elements,
                                   false, nullptr, -1, config.target_dx, outDomain);

    double sx = outDomain.scale[0];
    double dx = (1.0 / sx) * config.si_factor;

    logInfo(std::string("  Voxelized domain size: ") +
            std::to_string(voxelVol.nx) + " x " + std::to_string(voxelVol.ny) + " x " + std::to_string(voxelVol.nz));
    logInfo(std::string("  dx [m]: ") + std::to_string(dx));

    VoxelizationResult result;
    result.volume = voxelVol;
    for (int d = 0; d < 3; ++d) {
        result.scale[d] = outDomain.scale[d];
        result.shift[d] = outDomain.shift[d];
        result.domain_size[d] = outDomain.domain[d];
        result.bbox_min[d] = outDomain.bbox_min[d];
        result.bbox_max[d] = outDomain.bbox_max[d];
    }
    result.dx = dx;

    return result;
}

static std::vector<double> calculateMurrayFlowRatios(const std::vector<OpeningInfo>& openings) {
    // Sum of r^3 for all outlets (skip first opening = inlet)
    double r3_tot = 0;
    for (size_t i = 1; i < openings.size(); ++i)
        r3_tot += openings[i].radius * openings[i].radius * openings[i].radius;

    std::vector<double> ratios;
    for (auto& o : openings)
        ratios.push_back(o.radius * o.radius * o.radius / r3_tot);

    return ratios;
}

static OpeningData extractOpenings(const PreprocessorConfig& config,
                                   const VoxelizationResult& voxResult) {
    logInfo("Extracting opening information from centerline");

    std::string clFile = config.resolvePath(config.centerline_vtp);
    auto openings = getOpeningsFromCenterline(clFile);
    auto voxelOpenings = convertToVoxelspace(openings, voxResult.scale, voxResult.shift);

    auto cutList = generateCutList(voxResult.domain_size, voxelOpenings,
                                    config.distance, config.use_normal_for_face_selection);

    return OpeningData{voxelOpenings, cutList};
}

static void doCreateWalls(const PreprocessorConfig& config,
                          const BoolVol3D& volume,
                          const std::vector<int>& cutList,
                          Vol3D& wallVolume,
                          std::vector<int>& sliced) {
    logInfo("Creating walls and opening inlets/outlets");

    wallVolume = createWalls(volume, cutList, config.cut_width, sliced);

    logInfo(std::string("  Size after cutting layers: ") +
            std::to_string(wallVolume.nx) + " x " + std::to_string(wallVolume.ny) + " x " + std::to_string(wallVolume.nz));

    long long total = (long long)wallVolume.nx * wallVolume.ny * wallVolume.nz;
    long long fluids = 0, walls = 0;
    for (auto v : wallVolume.data) {
        if (v == VOXEL_FLUID) fluids++;
        if (v == VOXEL_WALL) walls++;
    }

    logInfo(std::string("  Volume: ") + std::to_string(total));
    logInfo(std::string("  Fluid nodes: ") + std::to_string(fluids));
    logInfo(std::string("  Fluid ratio: ") + std::to_string((double)fluids / total));
    logInfo(std::string("  Walls: ") + std::to_string(walls));
}

static GeometryResult doDetectOpenings(const PreprocessorConfig& config,
                                       const Vol3D& volume,
                                       const OpeningData& openingData) {
    logInfo("Detecting and assigning voxel openings");

    auto detResult = detectOpenings(volume);
    auto& inletOutlets = detResult.inlets_outlets;

    // Calculate opening centers from detected voxels
    // In Python: oC += (z, x, y) — matching Python's convention from paintInletsOutlets
    std::vector<Point3> openingCenters;
    for (auto& io : inletOutlets) {
        double cz = 0, cx = 0, cy = 0;
        for (auto& [x, y, z] : io) {
            cz += z; cx += x; cy += y;
        }
        int n = (int)io.size();
        openingCenters.push_back(Point3{{cz / n, cx / n, cy / n}});
    }

    if (openingCenters.size() != openingData.openings.size()) {
        throw std::runtime_error(
            "Number of voxelized openings (" + std::to_string(openingCenters.size()) +
            ") differs from centerline openings (" + std::to_string(openingData.openings.size()) + ")");
    }

    // Match voxelized openings to centerline data
    auto flowRatios = calculateMurrayFlowRatios(openingData.openings);

    std::vector<double> matchedRadius;
    std::vector<double> matchedQRatio;
    std::vector<Point3> matchedCenter;
    std::vector<Point3> matchedNormal;
    std::vector<std::vector<VoxelCoord>> sortedInlets;

    for (size_t ccCL = 0; ccCL < openingData.openings.size(); ++ccCL) {
        for (size_t ccVox = 0; ccVox < openingCenters.size(); ++ccVox) {
            auto& cVox = openingCenters[ccVox];
            auto& rCL = openingData.openings[ccCL];

            Point3 cCL = rCL.position;
            if (inRange3D(cVox, cCL, (double)config.distance)) {
                matchedRadius.push_back(rCL.radius * config.si_factor);
                matchedQRatio.push_back(flowRatios[ccCL]);
                matchedCenter.push_back(cVox);
                matchedNormal.push_back(rCL.tangent);
                sortedInlets.push_back(inletOutlets[ccVox]);
            }
        }
    }

    // Reorder openings for rotation if enabled
    if (config.rotation.enabled) {
        std::map<std::string, std::string> axisToFace = {
            {"-x", "X-"}, {"+x", "X+"},
            {"-y", "Y-"}, {"+y", "Y+"},
            {"-z", "Z-"}, {"+z", "Z+"}
        };

        std::string lowerAxis = config.rotation.inlet_target_axis;
        for (auto& c : lowerAxis) c = tolower(c);
        auto it = axisToFace.find(lowerAxis);
        if (it != axisToFace.end()) {
            std::string targetFace = it->second;
            int inletIdx = -1;
            for (size_t i = 0; i < matchedCenter.size(); ++i) {
                std::string face = getOpeningFace(matchedCenter[i],
                                                   volume.nx, volume.ny, volume.nz, 5.0);
                logInfo(std::string("  Opening ") + std::to_string(i) +
                        ": face=" + face + ", radius=" + std::to_string(matchedRadius[i]) + "m");
                if (face == targetFace) {
                    inletIdx = (int)i;
                    logInfo(std::string("  -> Identified as INLET (on target face ") + targetFace + ")");
                }
            }

            if (inletIdx > 0) {
                logInfo(std::string("  Reordering: moving opening ") +
                        std::to_string(inletIdx) + " to position 0 (inlet)");
                std::swap(matchedRadius[0], matchedRadius[inletIdx]);
                std::swap(matchedQRatio[0], matchedQRatio[inletIdx]);
                std::swap(matchedCenter[0], matchedCenter[inletIdx]);
                std::swap(matchedNormal[0], matchedNormal[inletIdx]);
                std::swap(sortedInlets[0], sortedInlets[inletIdx]);
            }
        }
    }

    auto paintResult = paintInletsOutlets(sortedInlets, detResult.data, false);

    GeometryResult geomResult;
    geomResult.volume = paintResult.painted_volume;
    geomResult.opening_index = paintResult.opening_index;
    geomResult.opening_radius = matchedRadius;
    geomResult.opening_normalized_q_ratio = matchedQRatio;
    geomResult.opening_center = matchedCenter;
    geomResult.opening_normal = matchedNormal;

    return geomResult;
}

static BoolVol3D sliceBoolVol(const BoolVol3D& vol, const std::vector<int>& sliced) {
    int nnx = sliced[1] - sliced[0];
    int nny = sliced[3] - sliced[2];
    int nnz = sliced[5] - sliced[4];
    BoolVol3D result(nnx, nny, nnz);
    for (int x = 0; x < nnx; ++x)
        for (int y = 0; y < nny; ++y)
            for (int z = 0; z < nnz; ++z)
                result(x, y, z) = vol(x + sliced[0], y + sliced[2], z + sliced[4]);
    return result;
}

static void processCoil(const PreprocessorConfig& config,
                         GeometryResult& geomResult,
                         const VoxelizationResult& voxResult,
                         const std::vector<int>& sliced,
                         const std::vector<int>& cutList) {
    logInfo("Voxelizing coil geometry from 3 projections");

    std::string coilStl = config.resolvePath(config.coil_stl);
    DomainData dd;
    for (int d = 0; d < 3; ++d) {
        dd.scale[d] = voxResult.scale[d];
        dd.shift[d] = voxResult.shift[d];
        dd.domain[d] = voxResult.domain_size[d];
        dd.bbox_min[d] = voxResult.bbox_min[d];
        dd.bbox_max[d] = voxResult.bbox_max[d];
    }

    BoolVol3D coilDomain = voxelizeThreeProjections(coilStl, config.target_elements, dd);

    logInfo(std::string("  Merged coil shape: ") +
            std::to_string(coilDomain.nx) + "x" + std::to_string(coilDomain.ny) + "x" + std::to_string(coilDomain.nz));

    // Apply slicing
    coilDomain = sliceBoolVol(coilDomain, sliced);
    logInfo(std::string("  After slicing: ") +
            std::to_string(coilDomain.nx) + "x" + std::to_string(coilDomain.ny) + "x" + std::to_string(coilDomain.nz));

    // Apply boundary cuts
    coilDomain = applyBoundaryCutsBool(coilDomain, cutList, config.cut_width);
    logInfo(std::string("  After cutting: ") +
            std::to_string(coilDomain.nx) + "x" + std::to_string(coilDomain.ny) + "x" + std::to_string(coilDomain.nz));

    // Check shape match
    if (geomResult.volume.nx != coilDomain.nx ||
        geomResult.volume.ny != coilDomain.ny ||
        geomResult.volume.nz != coilDomain.nz) {
        logWarning("Shape mismatch between geometry and coil domain!");
        return;
    }

    // Mark coil voxels as WALL
    long long coilCount = 0;
    for (int x = 0; x < coilDomain.nx; ++x) {
        for (int y = 0; y < coilDomain.ny; ++y) {
            for (int z = 0; z < coilDomain.nz; ++z) {
                if (coilDomain(x, y, z)) {
                    geomResult.volume(x, y, z) = VOXEL_WALL;
                    coilCount++;
                }
            }
        }
    }

    logInfo(std::string("  Marked ") + std::to_string(coilCount) + " coil voxels as wall");
}

void runPreprocessingPipeline(PreprocessorConfig& config) {
    logInfo("============================================================");
    logInfo("HemoFlow Geometry Preprocessor (C++)");
    logInfo("============================================================");

    auto startTime = std::chrono::steady_clock::now();

    // Step 1: Apply geometry rotation if enabled
    applyGeometryRotation(config);

    // Step 1.5: Save rescaled meshes
    saveRescaledMeshes(config);

    // Step 2: Voxelize geometry
    VoxelizationResult voxResult = voxelizeGeometry(config);

    // Step 3: Extract openings from centerline
    OpeningData openingData = extractOpenings(config, voxResult);

    // Step 4: Create walls
    Vol3D wallVolume;
    std::vector<int> sliced;
    doCreateWalls(config, voxResult.volume, openingData.cut_list, wallVolume, sliced);

    // Step 5: Detect and label openings
    GeometryResult geomResult = doDetectOpenings(config, wallVolume, openingData);

    // Step 6: Process stent if configured
    StentResult* stentResult = nullptr;
    // Stent processing would go here if needed

    // Step 7: Process coil if configured
    if (config.hasCoil()) {
        processCoil(config, geomResult, voxResult, sliced, openingData.cut_list);
    }

    // Step 8: Save final geometry
    saveGeometry(config, geomResult, voxResult, stentResult);

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

    logInfo("============================================================");
    logInfo(std::string("Preprocessing completed successfully in ") +
            std::to_string(elapsed) + "s");
    logInfo("============================================================");
}
