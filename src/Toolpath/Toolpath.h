#pragma once
#include "Geometry/Geometry.h"
#include "Config/Config.h"
#include <vector>

struct ToolpathContour {
    std::vector<geo::Point> points;
    bool arcEligible = false;  // true if contour is around a circular pad
};

std::vector<ToolpathContour> generateToolpath(
    const geo::Paths& clearance, const Config& config);

std::vector<ToolpathContour> orderContours(
    std::vector<ToolpathContour>& contours);
