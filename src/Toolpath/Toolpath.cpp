#include "Toolpath/Toolpath.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <limits>

// ── Contour-parallel toolpath generation ─────────────────────────────────────

std::vector<ToolpathContour> generateLaserToolpath(
    const geo::Paths& clearance, const Config& config)
{
    double beamRadius = config.machine.laser_beam_diameter / 2.0;
    double step = config.machine.laser_beam_diameter * (1.0 - config.cam.overlap);
    double initialOffset = config.cam.offset + beamRadius;
    double simplifyTol = step / 2.0;

    if (step <= 0)
        throw std::runtime_error("Invalid step size: beam=" +
            std::to_string(config.machine.laser_beam_diameter) +
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

// ── Nearest-start-point greedy ordering ──────────────────────────────────────

std::vector<ToolpathContour> orderContours(
    std::vector<ToolpathContour>& contours)
{
    if (contours.size() <= 1) return contours;

    std::vector<ToolpathContour> ordered;
    ordered.reserve(contours.size());
    ordered.push_back(std::move(contours[0]));

    std::vector<bool> used(contours.size(), false);
    used[0] = true;

    geo::Point curPos = ordered[0].points.back();

    for (size_t n = 1; n < contours.size(); n++) {
        int bestIdx = -1;
        double bestDist = std::numeric_limits<double>::max();
        bool bestReversed = false;

        for (size_t i = 1; i < contours.size(); i++) {
            if (used[i]) continue;
            auto& pts = contours[i].points;
            if (pts.empty()) continue;

            double dStart = (curPos.x - pts.front().x) * (curPos.x - pts.front().x)
                          + (curPos.y - pts.front().y) * (curPos.y - pts.front().y);
            if (dStart < bestDist) {
                bestDist = dStart;
                bestIdx = (int)i;
                bestReversed = false;
            }

            double dEnd = (curPos.x - pts.back().x) * (curPos.x - pts.back().x)
                        + (curPos.y - pts.back().y) * (curPos.y - pts.back().y);
            if (dEnd < bestDist) {
                bestDist = dEnd;
                bestIdx = (int)i;
                bestReversed = true;
            }
        }

        if (bestIdx < 0) break;
        used[bestIdx] = true;

        if (bestReversed)
            std::reverse(contours[bestIdx].points.begin(), contours[bestIdx].points.end());

        ordered.push_back(std::move(contours[bestIdx]));
        curPos = ordered.back().points.back();
    }

    return ordered;
}
