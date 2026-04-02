#include "GCode/GCodeGen.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <stdexcept>

// ── Drill ordering (nearest-neighbor + 2-opt) ────────────────────────────────

static double drillDist(const DrillHole& a, const DrillHole& b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<DrillHole> orderDrillHoles(const std::vector<DrillHole>& holes) {
    if (holes.size() <= 1) return holes;

    // Phase 1: Nearest-neighbor greedy
    std::vector<DrillHole> ordered;
    ordered.reserve(holes.size());
    ordered.push_back(holes[0]);

    std::vector<bool> used(holes.size(), false);
    used[0] = true;
    double cx = holes[0].x, cy = holes[0].y;

    for (size_t n = 1; n < holes.size(); n++) {
        int bestIdx = -1;
        double bestDist = std::numeric_limits<double>::max();
        for (size_t i = 0; i < holes.size(); i++) {
            if (used[i]) continue;
            double d = (holes[i].x - cx) * (holes[i].x - cx)
                     + (holes[i].y - cy) * (holes[i].y - cy);
            if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
        }
        if (bestIdx < 0) break;
        used[bestIdx] = true;
        ordered.push_back(holes[bestIdx]);
        cx = holes[bestIdx].x;
        cy = holes[bestIdx].y;
    }

    // Phase 2: 2-opt local improvement
    if (ordered.size() > 2) {
        bool improved = true;
        int maxIter = 100;
        while (improved && maxIter-- > 0) {
            improved = false;
            for (size_t i = 0; i + 2 < ordered.size(); i++) {
                for (size_t j = i + 2; j < ordered.size(); j++) {
                    double oldCost = drillDist(ordered[i], ordered[i + 1]);
                    if (j + 1 < ordered.size())
                        oldCost += drillDist(ordered[j], ordered[j + 1]);

                    // Try reversing [i+1 .. j]
                    std::reverse(ordered.begin() + i + 1, ordered.begin() + j + 1);

                    double newCost = drillDist(ordered[i], ordered[i + 1]);
                    if (j + 1 < ordered.size())
                        newCost += drillDist(ordered[j], ordered[j + 1]);

                    if (newCost < oldCost - 0.01) {
                        improved = true;
                    } else {
                        std::reverse(ordered.begin() + i + 1, ordered.begin() + j + 1);
                    }
                }
            }
        }
    }

    return ordered;
}

// ── Coordinate formatting ────────────────────────────────────────────────────

static std::string fmtXY(double x, double y, double xOff, double yOff) {
    char buf[80];
    _snprintf(buf, sizeof(buf), "X%.4f Y%.4f", x + xOff, y + yOff);
    return buf;
}

static std::string fmtZ(double z) {
    char buf[40];
    _snprintf(buf, sizeof(buf), "Z%.4f", z);
    return buf;
}

static std::string fmtF(double f) {
    char buf[40];
    _snprintf(buf, sizeof(buf), "F%.4f", f);
    return buf;
}

static std::string fmtS(double s) {
    char buf[40];
    _snprintf(buf, sizeof(buf), "S=%.4f", s);
    return buf;
}

// ── GCode generation (CNC engraver mode) ─────────────────────────────────────

std::string generateGCode(
    const std::vector<ToolpathContour>& contours,
    const std::vector<DrillHole>& holes,
    const std::vector<geo::Point>& cutoutPath,
    const Config& config,
    double xOffset, double yOffset)
{
    auto& mc  = config.machine;
    auto& job = config.job;

    double zTravel = mc.engraver_z_travel;
    double zCut    = mc.engraver_z_cut;

    // Validate all coordinates within machine bounds (skip when size = 0, meaning no limit)
    if (mc.x_size > 0 && mc.y_size > 0) {
        std::vector<std::string> oob;
        for (auto& contour : contours) {
            for (auto& pt : contour.points) {
                double fx = pt.x + xOffset, fy = pt.y + yOffset;
                if (fx < 0 || fx > mc.x_size || fy < 0 || fy > mc.y_size) {
                    char buf[80];
                    _snprintf(buf, sizeof(buf), "engrave (%.4f, %.4f)", fx, fy);
                    oob.push_back(buf);
                    break;
                }
            }
        }
        for (auto& hole : holes) {
            double fx = hole.x + xOffset, fy = hole.y + yOffset;
            if (fx < 0 || fx > mc.x_size || fy < 0 || fy > mc.y_size) {
                char buf[80];
                _snprintf(buf, sizeof(buf), "drill (%.4f, %.4f)", fx, fy);
                oob.push_back(buf);
            }
        }
        for (auto& pt : cutoutPath) {
            double fx = pt.x + xOffset, fy = pt.y + yOffset;
            if (fx < 0 || fx > mc.x_size || fy < 0 || fy > mc.y_size) {
                char buf[80];
                _snprintf(buf, sizeof(buf), "cutout (%.4f, %.4f)", fx, fy);
                oob.push_back(buf);
                break;
            }
        }
        if (!oob.empty()) {
            std::string msg = "Coordinates out of machine bounds:\n";
            for (size_t i = 0; i < oob.size() && i < 5; i++)
                msg += "  " + oob[i] + "\n";
            if (oob.size() > 5)
                msg += "  ... and " + std::to_string(oob.size() - 5) + " more\n";
            throw std::runtime_error(msg);
        }
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(4);

    out << "; gerber2gcode — CNC PCB isolation engraving\n";
    out << "G21 ; mm\n";
    out << "G90 ; absolute\n";
    out << "\n";

    // ── Engraver isolation milling ───────────────────────────────────────
    if (!contours.empty()) {
        out << "; === Engraver: isolation milling ===\n";
        out << "G0 " << fmtZ(zTravel) << "\n";

        for (auto& contour : contours) {
            if (contour.points.empty()) continue;
            auto& pts = contour.points;
            double sx = pts[0].x, sy = pts[0].y;

            // Rapid to start position
            out << "G0 " << fmtXY(sx, sy, xOffset, yOffset) << "\n";
            // Plunge to cutting depth
            out << "G1 " << fmtZ(zCut) << " " << fmtF(job.engraver_feedrate / 2.0) << "\n";

            // Cut contour
            for (size_t i = 1; i < pts.size(); i++) {
                out << "G1 " << fmtXY(pts[i].x, pts[i].y, xOffset, yOffset) << " "
                    << fmtF(job.engraver_feedrate) << "\n";
            }
            // Close contour
            out << "G1 " << fmtXY(sx, sy, xOffset, yOffset) << " "
                << fmtF(job.engraver_feedrate) << "\n";
            // Retract
            out << "G0 " << fmtZ(zTravel) << "\n";
        }

        out << "\n";
    }

    // ── Drilling ─────────────────────────────────────────────────────────
    if (!holes.empty()) {
        out << "; === Drilling ===\n";
        out << "; NOTE: Change to drill bit manually\n";
        out << "G0 " << fmtZ(mc.spindle_z_home) << "\n";

        for (auto& hole : holes) {
            out << "G0 " << fmtXY(hole.x, hole.y, xOffset, yOffset) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_pre_drill) << " "
                << fmtF(mc.move_feedrate) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_drill) << " "
                << fmtF(job.spindle_feedrate) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_pre_drill) << " "
                << fmtF(job.spindle_feedrate) << "\n";
            out << "G0 " << fmtZ(mc.spindle_z_home) << "\n";
        }

        out << "\n";
    }

    // ── Cutout (multi-pass depth) ────────────────────────────────────────
    if (!cutoutPath.empty()) {
        out << "; === Board cutout ===\n";
        out << "G0 " << fmtZ(zTravel) << "\n";

        // Calculate depth passes
        double depthPerPass = mc.cutout_z_cut;    // negative
        double finalZ = mc.cutout_z_final;         // negative (through material)
        if (depthPerPass >= 0) depthPerPass = -0.5;
        if (finalZ >= 0) finalZ = -1.6;

        double currentZ = 0.0;
        int passNum = 0;
        while (currentZ > finalZ + 0.001) {
            currentZ += depthPerPass;
            if (currentZ < finalZ) currentZ = finalZ;
            passNum++;

            out << "; --- Pass " << passNum << " Z=" << fmtZ(currentZ).substr(1) << " ---\n";

            // Rapid to start position
            out << "G0 " << fmtXY(cutoutPath[0].x, cutoutPath[0].y, xOffset, yOffset) << "\n";
            // Plunge to current pass depth
            out << "G1 " << fmtZ(currentZ) << " " << fmtF(job.engraver_feedrate / 2.0) << "\n";

            // Cut along outline
            for (size_t i = 1; i < cutoutPath.size(); i++) {
                out << "G1 " << fmtXY(cutoutPath[i].x, cutoutPath[i].y, xOffset, yOffset) << " "
                    << fmtF(job.engraver_feedrate) << "\n";
            }
            // Close the loop
            out << "G1 " << fmtXY(cutoutPath[0].x, cutoutPath[0].y, xOffset, yOffset) << " "
                << fmtF(job.engraver_feedrate) << "\n";
            // Retract
            out << "G0 " << fmtZ(zTravel) << "\n";
        }

        out << "\n";
    }

    out << "G0 " << fmtZ(mc.engraver_z_travel) << " ; safe Z\n";
    out << "G0 X0 Y0 ; return home\n";
    out << "M84 ; motors off\n";

    return out.str();
}

// ── Time estimation ──────────────────────────────────────────────────────────

double estimateJobTime(
    const std::vector<ToolpathContour>& contours,
    const std::vector<DrillHole>& holes,
    const std::vector<geo::Point>& cutoutPath,
    const Config& config)
{
    auto& mc  = config.machine;
    auto& job = config.job;
    double total = 0.0;

    double prevX = 0, prevY = 0;
    double engFeed = job.engraver_feedrate > 0 ? job.engraver_feedrate : 300.0;

    for (auto& contour : contours) {
        if (contour.points.empty()) continue;
        double sx = contour.points[0].x, sy = contour.points[0].y;
        // Rapid to start
        total += std::hypot(sx - prevX, sy - prevY) / (mc.move_feedrate / 60.0);
        // Plunge + retract
        double zDelta = std::abs(mc.engraver_z_travel - mc.engraver_z_cut);
        total += 2.0 * zDelta / (engFeed / 2.0 / 60.0);

        // Cut path
        double cutLen = 0.0;
        double px = sx, py = sy;
        for (size_t i = 1; i < contour.points.size(); i++) {
            double cx = contour.points[i].x, cy = contour.points[i].y;
            cutLen += std::hypot(cx - px, cy - py);
            px = cx; py = cy;
        }
        cutLen += std::hypot(sx - px, sy - py);
        total += cutLen / (engFeed / 60.0);
        prevX = sx; prevY = sy;
    }

    if (!holes.empty()) {
        prevX = 0; prevY = 0;
        for (auto& h : holes) {
            total += std::hypot(h.x - prevX, h.y - prevY) / (mc.move_feedrate / 60.0);
            total += std::abs(mc.spindle_z_home - mc.spindle_z_pre_drill) / (mc.move_feedrate / 60.0);
            total += std::abs(mc.spindle_z_pre_drill - mc.spindle_z_drill) / (job.spindle_feedrate / 60.0);
            total += std::abs(mc.spindle_z_pre_drill - mc.spindle_z_drill) / (job.spindle_feedrate / 60.0);
            total += std::abs(mc.spindle_z_home - mc.spindle_z_pre_drill) / (mc.move_feedrate / 60.0);
            prevX = h.x; prevY = h.y;
        }
    }

    // Cutout time estimation (multi-pass)
    if (!cutoutPath.empty()) {
        double depthPerPass = mc.cutout_z_cut;
        double finalZ = mc.cutout_z_final;
        if (depthPerPass >= 0) depthPerPass = -0.5;
        if (finalZ >= 0) finalZ = -1.6;

        // Compute outline perimeter
        double perim = 0.0;
        for (size_t i = 1; i < cutoutPath.size(); i++)
            perim += std::hypot(cutoutPath[i].x - cutoutPath[i-1].x,
                               cutoutPath[i].y - cutoutPath[i-1].y);
        perim += std::hypot(cutoutPath[0].x - cutoutPath.back().x,
                           cutoutPath[0].y - cutoutPath.back().y);

        int nPasses = 0;
        double cz = 0.0;
        while (cz > finalZ + 0.001) {
            cz += depthPerPass;
            if (cz < finalZ) cz = finalZ;
            nPasses++;
        }
        double zTravel = mc.engraver_z_travel;
        double zDelta = std::abs(zTravel - finalZ);
        total += nPasses * (perim / (engFeed / 60.0));               // cutting
        total += nPasses * 2.0 * zDelta / (engFeed / 2.0 / 60.0);   // plunge/retract
    }

    return total;
}
