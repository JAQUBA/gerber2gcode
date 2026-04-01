#pragma once
#include "Config/Config.h"
#include <string>
#include <vector>
#include <functional>

using LogCallback = std::function<void(const std::string&)>;

struct PipelineParams {
    std::string configPath;
    std::string kicadDir;
    std::string outputPath;
    double xOffset  = 0.0;
    double yOffset  = 0.0;
    bool flip       = false;
    bool ignoreVia  = false;
    std::string debugPath;
};

struct KicadFiles {
    std::string edgeCuts;
    std::string copper;
    std::vector<std::string> drills;
    bool isBack = false;
};

KicadFiles detectKicadFiles(const std::string& directory);
bool runPipeline(const PipelineParams& params, LogCallback log);
