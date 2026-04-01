#include "GCode/GCodeGen.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <stdexcept>

// ── Drill ordering (nearest-neighbor greedy) ─────────────────────────────────

std::vector<DrillHole> orderDrillHoles(const std::vector<DrillHole>& holes) {
    if (holes.size() <= 1) return holes;

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

// ── GCode generation ─────────────────────────────────────────────────────────

std::string generateGCode(
    const std::vector<ToolpathContour>& contours,
    const std::vector<DrillHole>& holes,
    const Config& config,
    double xOffset, double yOffset)
{
    auto& mc  = config.machine;
    auto& job = config.job;

    // Validate all coordinates within machine bounds
    std::vector<std::string> oob;
    for (auto& contour : contours) {
        for (auto& pt : contour.points) {
            double fx = pt.x + xOffset, fy = pt.y + yOffset;
            if (fx < 0 || fx > mc.x_size || fy < 0 || fy > mc.y_size) {
                char buf[80];
                _snprintf(buf, sizeof(buf), "laser (%.4f, %.4f)", fx, fy);
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
    if (!oob.empty()) {
        std::string msg = "Coordinates out of machine bounds:\n";
        for (size_t i = 0; i < oob.size() && i < 5; i++)
            msg += "  " + oob[i] + "\n";
        if (oob.size() > 5)
            msg += "  ... and " + std::to_string(oob.size() - 5) + " more\n";
        throw std::runtime_error(msg);
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(4);

    out << "; init printer\n";
    out << "G28\n";
    out << "G21\n";
    out << "G90\n";
    out << "\n";

    if (!contours.empty()) {
        out << "; start lasering\n";
        out << "USE_LASER\n";
        out << "G0 " << fmtZ(mc.laser_z) << " " << fmtF(mc.move_feedrate) << "\n";

        for (auto& contour : contours) {
            if (contour.points.empty()) continue;
            auto& pts = contour.points;
            double sx = pts[0].x, sy = pts[0].y;

            out << "G1 " << fmtXY(sx, sy, xOffset, yOffset) << " "
                << fmtF(mc.move_feedrate) << "\n";
            out << "LASER_SET " << fmtS(job.laser_power) << "\n";

            for (size_t i = 1; i < pts.size(); i++) {
                out << "G1 " << fmtXY(pts[i].x, pts[i].y, xOffset, yOffset) << " "
                    << fmtF(job.laser_feedrate) << "\n";
            }
            // Close contour
            out << "G1 " << fmtXY(sx, sy, xOffset, yOffset) << " "
                << fmtF(job.laser_feedrate) << "\n";
            out << "LASER_OFF\n";
        }

        out << "LASER_OFF\n";
        out << "G28\n";
        out << "\n";
    }

    if (!holes.empty()) {
        out << "; start drilling\n";
        out << "USE_SPINDLE\n";
        out << "G0 " << fmtZ(mc.spindle_z_home) << " " << fmtF(mc.move_feedrate) << "\n";

        // Spindle ramp-up
        char sBuf[40];
        _snprintf(sBuf, sizeof(sBuf), "S=%.4f", job.spindle_power / 2.0);
        out << "SPINDLE_SET " << sBuf << "\n";
        out << "G4 P5000\n";
        _snprintf(sBuf, sizeof(sBuf), "S=%.4f", job.spindle_power);
        out << "SPINDLE_SET " << sBuf << "\n";

        for (auto& hole : holes) {
            out << "G0 " << fmtXY(hole.x, hole.y, xOffset, yOffset) << " "
                << fmtF(mc.move_feedrate) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_pre_drill) << " "
                << fmtF(mc.move_feedrate) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_drill) << " "
                << fmtF(job.spindle_feedrate) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_pre_drill) << " "
                << fmtF(job.spindle_feedrate) << "\n";
            out << "G1 " << fmtZ(mc.spindle_z_home) << " "
                << fmtF(mc.move_feedrate) << "\n";
        }

        out << "SPINDLE_OFF\n";
        out << "G28\n";
    }

    out << "\n";
    return out.str();
}

// ── Time estimation ──────────────────────────────────────────────────────────

double estimateJobTime(
    const std::vector<ToolpathContour>& contours,
    const std::vector<DrillHole>& holes,
    const Config& config)
{
    auto& mc  = config.machine;
    auto& job = config.job;
    double total = 0.0;

    double prevX = 0, prevY = 0;
    for (auto& contour : contours) {
        if (contour.points.empty()) continue;
        double sx = contour.points[0].x, sy = contour.points[0].y;
        total += std::hypot(sx - prevX, sy - prevY) / (mc.move_feedrate / 60.0);

        double lase = 0.0;
        double px = sx, py = sy;
        for (size_t i = 1; i < contour.points.size(); i++) {
            double cx = contour.points[i].x, cy = contour.points[i].y;
            lase += std::hypot(cx - px, cy - py);
            px = cx; py = cy;
        }
        lase += std::hypot(sx - px, sy - py);
        total += lase / (job.laser_feedrate / 60.0);
        prevX = sx; prevY = sy;
    }

    if (!holes.empty()) {
        total += 5.0; // Spindle ramp-up
        prevX = 0; prevY = 0;
        for (auto& h : holes) {
            total += std::hypot(h.x - prevX, h.y - prevY) / (mc.move_feedrate / 60.0);
            total += (mc.spindle_z_home - mc.spindle_z_pre_drill) / (mc.move_feedrate / 60.0);
            total += (mc.spindle_z_pre_drill - mc.spindle_z_drill) / (job.spindle_feedrate / 60.0);
            total += (mc.spindle_z_pre_drill - mc.spindle_z_drill) / (job.spindle_feedrate / 60.0);
            total += (mc.spindle_z_home - mc.spindle_z_pre_drill) / (mc.move_feedrate / 60.0);
            prevX = h.x; prevY = h.y;
        }
    }

    return total;
}
