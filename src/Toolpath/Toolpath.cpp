#include "Toolpath/Toolpath.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <limits>

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

// ── Helper: rapid distance between consecutive contours ──────────────────────

static double rapidDist(const ToolpathContour& a, const ToolpathContour& b) {
    if (a.points.empty() || b.points.empty()) return 0.0;
    double dx = a.points.back().x - b.points.front().x;
    double dy = a.points.back().y - b.points.front().y;
    return std::sqrt(dx * dx + dy * dy);
}

static double totalRapid(const std::vector<ToolpathContour>& v) {
    double sum = 0;
    for (size_t i = 0; i + 1 < v.size(); i++)
        sum += rapidDist(v[i], v[i + 1]);
    return sum;
}

// ── Nearest-start-point greedy ordering + 2-opt improvement ──────────────────

std::vector<ToolpathContour> orderContours(
    std::vector<ToolpathContour>& contours)
{
    if (contours.size() <= 1) return contours;

    // Phase 1: Nearest-neighbor greedy ordering
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

    // Phase 2: 2-opt local improvement
    if (ordered.size() > 2) {
        bool improved = true;
        int maxIter = 50;
        while (improved && maxIter-- > 0) {
            improved = false;
            for (size_t i = 0; i + 2 < ordered.size(); i++) {
                for (size_t j = i + 2; j < ordered.size(); j++) {
                    // Current cost: rapid(i→i+1) + rapid(j→j+1 or end)
                    double oldCost = rapidDist(ordered[i], ordered[i + 1]);
                    if (j + 1 < ordered.size())
                        oldCost += rapidDist(ordered[j], ordered[j + 1]);

                    // Reverse contour points in the sub-sequence for evaluation
                    // Try reversing the sub-sequence [i+1 .. j]
                    std::reverse(ordered.begin() + i + 1, ordered.begin() + j + 1);
                    // Also reverse internal points of each contour in the sub-range
                    for (size_t k = i + 1; k <= j; k++)
                        std::reverse(ordered[k].points.begin(), ordered[k].points.end());

                    double newCost = rapidDist(ordered[i], ordered[i + 1]);
                    if (j + 1 < ordered.size())
                        newCost += rapidDist(ordered[j], ordered[j + 1]);

                    if (newCost < oldCost - 0.01) {
                        improved = true;  // Keep the reversal
                    } else {
                        // Undo the reversal
                        for (size_t k = i + 1; k <= j; k++)
                            std::reverse(ordered[k].points.begin(), ordered[k].points.end());
                        std::reverse(ordered.begin() + i + 1, ordered.begin() + j + 1);
                    }
                }
            }
        }
    }

    return ordered;
}
