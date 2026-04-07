#include "Toolpath/Toolpath.h"
#include "Common/PathOptimization.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

// ── Contour-parallel toolpath generation ─────────────────────────────────────

std::vector<ToolpathContour> generateToolpath(
    const geo::Paths& clearance, const Config& config)
{
    double toolRadius = config.machine.engraver_tip_width / 2.0;
    double step = config.machine.engraver_tip_width * (1.0 - config.cam.overlap);
    double initialOffset = config.cam.offset + toolRadius;
    double simplifyTol = step / 2.0;

    if (step <= 0)
        throw std::runtime_error("Invalid step size: tool_width=" +
            std::to_string(config.machine.engraver_tip_width) +
            " overlap=" + std::to_string(config.cam.overlap));

    geo::Paths current = geo::offset(clearance, -initialOffset);
    std::vector<ToolpathContour> allContours;

    while (!geo::isEmpty(current)) {
        // Simplify for performance
        geo::Paths simplified = geo::simplifyPaths(current, simplifyTol);

        for (auto& poly : simplified) {
            if (poly.size() < 3) continue;
            ToolpathContour tc;
            tc.points.assign(poly.begin(), poly.end());
            allContours.push_back(std::move(tc));
        }

        current = geo::offset(current, -step);
    }

    if (!allContours.empty())
        allContours = orderContours(allContours);

    return allContours;
}

// ── Nearest-start-point greedy ordering + 2-opt improvement ──────────────────

std::vector<ToolpathContour> orderContours(
    std::vector<ToolpathContour>& contours)
{
    if (contours.size() <= 1) return contours;

    std::vector<pathopt::Chain2D> chains;
    std::vector<size_t> contourIndex;
    chains.reserve(contours.size());
    contourIndex.reserve(contours.size());
    for (size_t i = 0; i < contours.size(); i++) {
        const auto& c = contours[i];
        if (c.points.empty()) continue;
        pathopt::Chain2D ch;
        ch.sx = c.points.front().x;
        ch.sy = c.points.front().y;
        ch.ex = c.points.back().x;
        ch.ey = c.points.back().y;
        chains.push_back(ch);
        contourIndex.push_back(i);
    }

    if (chains.size() <= 1) return contours;

    std::vector<pathopt::ChainVisit> visits = pathopt::orderChainsNN2Opt(chains, 50, 0.01);
    std::vector<ToolpathContour> ordered;
    ordered.reserve(visits.size());

    for (const auto& v : visits) {
        ToolpathContour tc = std::move(contours[contourIndex[v.index]]);
        if (v.reversed)
            std::reverse(tc.points.begin(), tc.points.end());
        ordered.push_back(std::move(tc));
    }

    return ordered;
}
