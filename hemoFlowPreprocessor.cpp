#include "preprocessor_core.h"

#include <iostream>
#include <string>
#include <cstring>

static void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <config.json> [options]\n\n"
              << "HemoFlow geometry preprocessor - voxelizes vessel geometry\n"
              << "and prepares simulation input.\n\n"
              << "Options:\n"
              << "  --output-dir <dir>    Override output directory from config\n"
              << "  --log-level <level>   Set log level (DEBUG, INFO, WARNING, ERROR)\n"
              << "  --no-debug            Disable all debug outputs\n"
              << "  --help                Show this help message\n\n"
              << "Examples:\n"
              << "  " << progName << " preprocessor_config.json\n"
              << "  " << progName << " preprocessor_config.json --output-dir output/\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Check for --help
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::string configPath = argv[1];

    // Parse optional arguments
    std::string outputDir;
    bool noDebug = false;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--no-debug") == 0) {
            noDebug = true;
        } else if (std::strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            ++i; // skip value (not currently used beyond parsing)
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    try {
        PreprocessorConfig config = loadConfig(configPath);

        // Apply CLI overrides
        if (!outputDir.empty()) {
            config.output_dir = outputDir;
        }
        if (noDebug) {
            config.debug.enabled = false;
        }

        runPreprocessingPipeline(config);

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
