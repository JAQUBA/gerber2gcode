#pragma once
#include "Config/Config.h"
#include "Drill/DrillParser.h"
#include <string>
#include <vector>

void generateDebugBMP(
    const std::string& gcodePath,
    const std::string& outputPath,
    const Config& config,
    const std::vector<DrillHole>& holes = {});
