#pragma once
#include "Geometry/Geometry.h"
#include "Config/Config.h"
#include <vector>

struct ToolpathContour {
    std::vector<geo::Point> points;
};

std::vector<ToolpathContour> generateLaserToolpath(
    const geo::Paths& clearance, const Config& config);

std::vector<ToolpathContour> orderContours(
    std::vector<ToolpathContour>& contours);
