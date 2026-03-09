#include "app.hpp"

#include <iostream>
#include <string>

static void PrintUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " [--stress] [--no-stress] [--stress-count N] [--stress-model PATH] [--stress-spacing S]\n\n"
        << "Examples:\n"
        << "  " << exe << " --stress\n"
        << "  " << exe << " --stress --stress-count 50000 --stress-spacing 1.0\n"
        << "  " << exe << " --scene <path> --stress --stress-model ../assets/models/tree1.obj\n";
}

int main(int argc, char** argv) {
    cvsim::StressConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "--help" || a == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }

        if (a == "--scene") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --scene requires a path\n";
                return 1;
            }
            cfg.scenePath = argv[++i];
            continue;
        }
        else if (a == "--stress") {
            cfg.enabled = true;
        }
        else if (a == "--no-stress") {
            cfg.enabled = false;
        }
        else if (a == "--stress-count") {
            if (i + 1 >= argc) { std::cerr << "--stress-count requires a value\n"; return 2; }
            cfg.count = std::stoi(argv[++i]);
        }
        else if (a == "--stress-model") {
            if (i + 1 >= argc) { std::cerr << "--stress-model requires a value\n"; return 2; }
            cfg.modelPath = argv[++i];
        }
        else if (a == "--stress-spacing") {
            if (i + 1 >= argc) { std::cerr << "--stress-spacing requires a value\n"; return 2; }
            cfg.spacing = std::stof(argv[++i]);
        }
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            PrintUsage(argv[0]);
            return 2;
        }
    }

    try {
        cvsim::SimApp app(cfg);
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}