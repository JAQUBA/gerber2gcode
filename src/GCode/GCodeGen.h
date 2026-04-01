#pragma once
#include "Toolpath/Toolpath.h"
#include "Drill/DrillParser.h"
#include "Config/Config.h"
#include <string>
#include <vector>

std::string generateGCode(
    const std::vector<ToolpathContour>& contours,
    const std::vector<DrillHole>& holes,
    const Config& config,
    double xOffset, double yOffset);

double estimateJobTime(
    const std::vector<ToolpathContour>& contours,
    const std::vector<DrillHole>& holes,
    const Config& config);

std::vector<DrillHole> orderDrillHoles(const std::vector<DrillHole>& holes);
