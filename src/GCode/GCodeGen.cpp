#include "GCode/GCodeGen.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <stdexcept>

// ── Arc fitting ──────────────────────────────────────────────────────────────

static const double ARC_TOLERANCE   = 0.005;  // mm — max deviation from fitted arc
static const double MIN_ARC_RADIUS  = 0.05;   // mm — ignore tiny arcs (noise)
static const double MAX_ARC_RADIUS  = 100.0;  // mm — larger is essentially straight
static const double MIN_ARC_SWEEP   = 10.0;   // degrees — skip very shallow arcs
static const double MAX_ARC_SWEEP   = 355.0;  // degrees — avoid full-circle wrap issues
static const size_t MIN_ARC_POINTS  = 4;      // minimum points (3 seed + 1 validation)
static const double MIN_SIN_ANGLE   = 0.035;  // ~2° — collinearity gate

struct ArcSegment {
    bool   isArc;
    double ex, ey;          // endpoint
    double ci, cj;          // arc center offset from start (I, J)
    bool   clockwise;       // G2 if true, G3 if false
};

// Compute circumscribed circle of 3 points. Returns false if collinear.
static bool circumCircle(double x1, double y1,
                         double x2, double y2,
                         double x3, double y3,
                         double& cx, double& cy, double& r)
{
    double ax = x2 - x1, ay = y2 - y1;
    double bx = x3 - x1, by = y3 - y1;
    double D = 2.0 * (ax * by - ay * bx);
    if (std::abs(D) < 1e-12) return false;  // collinear

    double a2 = ax * ax + ay * ay;
    double b2 = bx * bx + by * by;
    cx = x1 + (by * a2 - ay * b2) / D;
    cy = y1 + (ax * b2 - bx * a2) / D;
    r = std::hypot(x1 - cx, y1 - cy);
    return true;
}

// Convert a polyline to a mix of lines and arcs.
// Returns a vector of ArcSegments starting from pts[0].
//
// Algorithm:
//  1. Seed circumscribed circle from 3 consecutive points
//  2. Verify 4th point lies on the circle (first real validation)
//  3. Determine CW/CCW from cross product of first two edge vectors
//  4. Extend arc window while points stay on the circle AND maintain
//     the same curvature direction
//  5. Validate minimum subtended angle (reject near-straight segments)
//  6. Recompute center from start/mid/end for accurate IJ (eliminates
//     endpoint radius mismatch that CNC controllers would reject)
//  7. Re-verify all intermediate points against the refined circle
static std::vector<ArcSegment> fitArcs(const std::vector<geo::Point>& pts,
                                        double tolerance)
{
    std::vector<ArcSegment> result;
    if (pts.size() < 2) return result;

    size_t i = 0;
    while (i < pts.size() - 1) {
        bool arcFound = false;

        // Need at least MIN_ARC_POINTS to attempt arc fitting
        if (i + MIN_ARC_POINTS - 1 < pts.size()) {
            // Seed circle from first 3 points
            double cx, cy, r;
            bool ok = circumCircle(pts[i].x, pts[i].y,
                                   pts[i + 1].x, pts[i + 1].y,
                                   pts[i + 2].x, pts[i + 2].y,
                                   cx, cy, r);

            if (ok && r >= MIN_ARC_RADIUS && r <= MAX_ARC_RADIUS) {
                // Validate 4th point (first real test — 3 seed points always
                // lie exactly on their circumscribed circle)
                double d3 = std::hypot(pts[i + 3].x - cx,
                                       pts[i + 3].y - cy);
                if (std::abs(d3 - r) <= tolerance) {

                    // Direction from cross product of first two edge vectors
                    double v1x = pts[i + 1].x - pts[i].x;
                    double v1y = pts[i + 1].y - pts[i].y;
                    double v2x = pts[i + 2].x - pts[i + 1].x;
                    double v2y = pts[i + 2].y - pts[i + 1].y;
                    double cross = v1x * v2y - v1y * v2x;

                    // Collinearity gate — reject nearly straight segments
                    double v1len = std::hypot(v1x, v1y);
                    double v2len = std::hypot(v2x, v2y);
                    if (v1len > 1e-9 && v2len > 1e-9 &&
                        std::abs(cross) / (v1len * v2len) > MIN_SIN_ANGLE) {

                        bool cw = (cross < 0.0);  // negative = CW in math coords

                        // Greedily extend the arc window
                        size_t end = i + 3;
                        while (end + 1 < pts.size()) {
                            double dist = std::hypot(pts[end + 1].x - cx,
                                                     pts[end + 1].y - cy);
                            if (std::abs(dist - r) > tolerance) break;

                            // Direction consistency — reject if curvature flips
                            double vax = pts[end].x - pts[end - 1].x;
                            double vay = pts[end].y - pts[end - 1].y;
                            double vbx = pts[end + 1].x - pts[end].x;
                            double vby = pts[end + 1].y - pts[end].y;
                            double c = vax * vby - vay * vbx;
                            if (std::abs(c) > 1e-12 && (c < 0.0) != cw) break;

                            end++;
                        }

                        // Sweep angle validation
                        double angS = std::atan2(pts[i].y - cy, pts[i].x - cx);
                        double angE = std::atan2(pts[end].y - cy, pts[end].x - cx);
                        double sweep = angE - angS;
                        if (cw) {
                            while (sweep > 0)          sweep -= 2.0 * geo::PI;
                            while (sweep < -2.0 * geo::PI) sweep += 2.0 * geo::PI;
                        } else {
                            while (sweep < 0)          sweep += 2.0 * geo::PI;
                            while (sweep > 2.0 * geo::PI) sweep -= 2.0 * geo::PI;
                        }
                        double sweepDeg = std::abs(sweep) * 180.0 / geo::PI;

                        if (sweepDeg >= MIN_ARC_SWEEP && sweepDeg <= MAX_ARC_SWEEP) {
                            // Recompute center using start, mid-point, and end
                            // for maximum endpoint accuracy (IJ precision)
                            size_t mid = (i + end) / 2;
                            double fcx, fcy, fr;
                            bool fok = circumCircle(pts[i].x, pts[i].y,
                                                    pts[mid].x, pts[mid].y,
                                                    pts[end].x, pts[end].y,
                                                    fcx, fcy, fr);

                            if (fok && fr >= MIN_ARC_RADIUS && fr <= MAX_ARC_RADIUS) {
                                // Verify all intermediate points against refined circle
                                bool allOk = true;
                                for (size_t k = i + 1; k < end; k++) {
                                    double d = std::hypot(pts[k].x - fcx,
                                                          pts[k].y - fcy);
                                    if (std::abs(d - fr) > tolerance * 2.0) {
                                        allOk = false;
                                        break;
                                    }
                                }

                                if (allOk) {
                                    ArcSegment seg;
                                    seg.isArc     = true;
                                    seg.ex        = pts[end].x;
                                    seg.ey        = pts[end].y;
                                    seg.ci        = fcx - pts[i].x;
                                    seg.cj        = fcy - pts[i].y;
                                    seg.clockwise = cw;
                                    result.push_back(seg);
                                    i = end;
                                    arcFound = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!arcFound) {
            ArcSegment seg;
            seg.isArc     = false;
            seg.ex        = pts[i + 1].x;
            seg.ey        = pts[i + 1].y;
            seg.ci = seg.cj = 0;
            seg.clockwise = false;
            result.push_back(seg);
            i++;
        }
    }

    return result;
}

// Format I/J offsets for arc commands
static std::string fmtIJ(double ci, double cj) {
    char buf[80];
    _snprintf(buf, sizeof(buf), "I%.4f J%.4f", ci, cj);
    return buf;
}

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

static std::string fmtSpindleS(double s) {
    char buf[40];
    _snprintf(buf, sizeof(buf), "S%.0f", std::max(0.0, s));
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
    double engraveFeed = job.engraver_feedrate > 0 ? job.engraver_feedrate : 300.0;
    double engravePlungeFeed = job.engraver_plunge_feedrate > 0 ? job.engraver_plunge_feedrate : 100.0;
    double spindleFeed = job.spindle_feedrate > 0 ? job.spindle_feedrate : 60.0;
    double spindlePower = job.spindle_power;

    // Z coordinate model: Z=0 is machine bed (bottom of material)
    // Material top surface at Z = materialThickness
    double mat     = mc.materialThickness;
    double zCut    = std::max(0.0, mat - std::abs(mc.engraver_z_cut));    // e.g., max(0, 1.5 - 0.05) = 1.45
    double zDrillPre  = std::max(mat, mat + mc.spindle_z_pre_drill);      // keep pre-drill at/above material top
    double zDrill  = std::max(0.0, mat - std::abs(mc.spindle_z_drill));   // e.g., max(0, 1.5 - 2.0) = 0
    double zProgramSafe = mat + std::max(mc.engraver_z_travel, mc.spindle_z_home); // full safe Z at program start/end only
    double zRapidClear  = mat + 1.0;                                      // 1mm above material — between operations

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

    bool isFluidNC = (job.postProfile == PostProfile::FluidNC);

    out << "; gerber2gcode — CNC PCB isolation engraving\n";
    out << "G21 ; mm\n";
    out << "G90 ; absolute\n";
    out << "G17 ; XY plane\n";
    out << "G94 ; feed per minute\n";
    if (!isFluidNC) out << "G54 ; work offset\n";
    out << "G40 ; cancel cutter compensation\n";
    if (!isFluidNC) {
        out << "G49 ; cancel tool length offset\n";
        out << "G80 ; cancel canned cycles\n";
    }
    out << "M5 ; spindle off\n";
    out << (isFluidNC ? "; post profile: FluidNC\n" : "; post profile: Mach3\n");
    out << "G0 " << fmtZ(zProgramSafe) << " ; initial safe Z\n";
    out << "G28.1 ; store current position as home\n";
    out << "\n";

    bool spindleOn = false;
    auto ensureSpindleOn = [&]() {
        if (spindleOn) return;
        if (spindlePower > 0.0)
            out << "M3 " << fmtSpindleS(spindlePower) << " ; spindle on\n";
        else
            out << "M3 ; spindle on\n";
        out << "G4 P1.0 ; spindle settle\n";
        spindleOn = true;
    };

    // ── Engraver isolation milling ───────────────────────────────────────
    if (!contours.empty()) {
        out << "; === Engraver: isolation milling ===\n";
        out << "; Tool diameter: " << mc.engraver_tip_width << " mm\n";
        out << "; XY feed: " << engraveFeed << " mm/min, plunge: " << engravePlungeFeed << " mm/min\n";

        if (job.engraver_spindle_on) {
            ensureSpindleOn();
        }

        out << "G0 " << fmtZ(zRapidClear) << "\n";

        for (auto& contour : contours) {
            if (contour.points.empty()) continue;
            auto& pts = contour.points;
            double sx = pts[0].x, sy = pts[0].y;

            // Rapid to start position
            out << "G0 " << fmtXY(sx, sy, xOffset, yOffset) << "\n";
            // Plunge to cutting depth
            out << "G1 " << fmtZ(zCut) << " " << fmtF(engravePlungeFeed) << "\n";

            if (job.use_arcs && contour.arcEligible && pts.size() >= 3) {
                std::vector<geo::Point> loop(pts.begin(), pts.end());
                loop.push_back(pts[0]);
                auto segments = fitArcs(loop, ARC_TOLERANCE);
                for (auto& seg : segments) {
                    if (seg.isArc) {
                        out << (seg.clockwise ? "G2 " : "G3 ")
                            << fmtXY(seg.ex, seg.ey, xOffset, yOffset) << " "
                            << fmtIJ(seg.ci, seg.cj) << " "
                            << fmtF(engraveFeed) << "\n";
                    } else {
                        out << "G1 " << fmtXY(seg.ex, seg.ey, xOffset, yOffset) << " "
                            << fmtF(engraveFeed) << "\n";
                    }
                }
            } else {
                // Cut contour (G1 only)
                for (size_t i = 1; i < pts.size(); i++) {
                    out << "G1 " << fmtXY(pts[i].x, pts[i].y, xOffset, yOffset) << " "
                            << fmtF(engraveFeed) << "\n";
                }
                // Close contour
                out << "G1 " << fmtXY(sx, sy, xOffset, yOffset) << " "
                    << fmtF(engraveFeed) << "\n";
            }
            // Retract
            out << "G0 " << fmtZ(zRapidClear) << "\n";
        }

        out << "\n";
    }

    // ── Drilling ─────────────────────────────────────────────────────────
    if (!holes.empty()) {
        out << "; === Drilling ===\n";
        out << "; Spindle tool diameter: " << mc.spindle_tool_diameter << " mm\n";
        out << "; Spindle feed: " << spindleFeed << " mm/min\n";
        ensureSpindleOn();
        out << "G0 " << fmtZ(zRapidClear) << "\n";

        for (auto& hole : holes) {
            out << "G0 " << fmtXY(hole.x, hole.y, xOffset, yOffset) << "\n";
            out << "G1 " << fmtZ(zDrillPre) << " "
                << fmtF(mc.move_feedrate) << "\n";
            out << "G1 " << fmtZ(zDrill) << " "
                << fmtF(spindleFeed) << "\n";
            if (job.drill_dwell > 0.0) {
                char dwBuf[40];
                _snprintf(dwBuf, sizeof(dwBuf), "G4 P%.3f ; dwell\n", job.drill_dwell);
                out << dwBuf;
            }
            out << "G1 " << fmtZ(zDrillPre) << " "
                << fmtF(spindleFeed) << "\n";
            out << "G0 " << fmtZ(zRapidClear) << "\n";
        }

        out << "\n";
    }

    // ── Cutout (multi-pass depth) ────────────────────────────────────────
    if (!cutoutPath.empty()) {
        out << "; === Board cutout ===\n";
        out << "; Spindle tool diameter: " << mc.spindle_tool_diameter << " mm\n";
        out << "; Spindle feed: " << spindleFeed << " mm/min\n";
        ensureSpindleOn();
        out << "G0 " << fmtZ(zRapidClear) << "\n";

        // Multi-pass from material top down to Z=0 (bed)
        double stepDown = std::abs(mc.cutout_z_step);
        if (stepDown < 0.01) stepDown = 0.5;

        double currentZ = mat;
        int passNum = 0;
        while (currentZ > 0.001) {
            currentZ -= stepDown;
            if (currentZ < 0.0) currentZ = 0.0;
            passNum++;

            out << "; --- Pass " << passNum << " Z=" << fmtZ(currentZ).substr(1) << " ---\n";

            // Rapid to start position
            out << "G0 " << fmtXY(cutoutPath[0].x, cutoutPath[0].y, xOffset, yOffset) << "\n";
            // Plunge to current pass depth
            out << "G1 " << fmtZ(currentZ) << " " << fmtF(spindleFeed) << "\n";

            if (job.use_arcs && cutoutPath.size() >= 3) {
                // Build closed loop
                std::vector<geo::Point> loop(cutoutPath.begin(), cutoutPath.end());
                loop.push_back(cutoutPath[0]);
                auto segments = fitArcs(loop, ARC_TOLERANCE);
                for (auto& seg : segments) {
                    if (seg.isArc) {
                        out << (seg.clockwise ? "G2 " : "G3 ")
                            << fmtXY(seg.ex, seg.ey, xOffset, yOffset) << " "
                            << fmtIJ(seg.ci, seg.cj) << " "
                            << fmtF(spindleFeed) << "\n";
                    } else {
                        out << "G1 " << fmtXY(seg.ex, seg.ey, xOffset, yOffset) << " "
                            << fmtF(spindleFeed) << "\n";
                    }
                }
            } else {
                // Cut along outline (G1 only)
                for (size_t i = 1; i < cutoutPath.size(); i++) {
                    out << "G1 " << fmtXY(cutoutPath[i].x, cutoutPath[i].y, xOffset, yOffset) << " "
                            << fmtF(spindleFeed) << "\n";
                }
                // Close the loop
                out << "G1 " << fmtXY(cutoutPath[0].x, cutoutPath[0].y, xOffset, yOffset) << " "
                    << fmtF(spindleFeed) << "\n";
            }
            // Retract
            out << "G0 " << fmtZ(zRapidClear) << "\n";
        }

        out << "\n";
    }

    out << "G0 " << fmtZ(zProgramSafe) << " ; safe Z\n";
    out << "G28 ; return to home position\n";
    out << "M5 ; spindle off\n";
    out << (isFluidNC ? "M2 ; program end\n" : "M30 ; program end\n");

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
    double engFeed = job.engraver_feedrate > 0 ? job.engraver_feedrate : 300.0;
    double engPlungeFeed = job.engraver_plunge_feedrate > 0 ? job.engraver_plunge_feedrate : 100.0;
    double spindleFeed = job.spindle_feedrate > 0 ? job.spindle_feedrate : 60.0;

    // Z coordinate model: Z=0 is bed, material top at Z=materialThickness
    double mat     = mc.materialThickness;
    double zRapidClear = mat + 1.0;                                       // 1mm above material between operations
    double zCut    = mat - std::abs(mc.engraver_z_cut);
    double zDrillPre  = mat + mc.spindle_z_pre_drill;
    double zDrill  = std::max(0.0, mat - std::abs(mc.spindle_z_drill));

    double prevX = 0, prevY = 0;

    for (auto& contour : contours) {
        if (contour.points.empty()) continue;
        double sx = contour.points[0].x, sy = contour.points[0].y;
        // Rapid to start
        total += std::hypot(sx - prevX, sy - prevY) / (mc.move_feedrate / 60.0);
        // Plunge + retract
        double zDelta = std::abs(zRapidClear - zCut);
        total += 2.0 * zDelta / (engPlungeFeed / 60.0);

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
            total += std::abs(zRapidClear - zDrillPre) / (mc.move_feedrate / 60.0);
            total += std::abs(zDrillPre - zDrill) / (spindleFeed / 60.0);
            if (job.drill_dwell > 0.0) total += job.drill_dwell;
            total += std::abs(zDrillPre - zDrill) / (spindleFeed / 60.0);
            total += std::abs(zRapidClear - zDrillPre) / (mc.move_feedrate / 60.0);
            prevX = h.x; prevY = h.y;
        }
    }

    // Cutout time estimation (multi-pass from materialThickness to Z=0)
    if (!cutoutPath.empty()) {
        double stepDown = std::abs(mc.cutout_z_step);
        if (stepDown < 0.01) stepDown = 0.5;

        // Compute outline perimeter
        double perim = 0.0;
        for (size_t i = 1; i < cutoutPath.size(); i++)
            perim += std::hypot(cutoutPath[i].x - cutoutPath[i-1].x,
                               cutoutPath[i].y - cutoutPath[i-1].y);
        perim += std::hypot(cutoutPath[0].x - cutoutPath.back().x,
                           cutoutPath[0].y - cutoutPath.back().y);

        int nPasses = 0;
        double cz = mat;
        while (cz > 0.001) {
            cz -= stepDown;
            if (cz < 0.0) cz = 0.0;
            nPasses++;
        }
        double zDeltaCut = std::abs(zRapidClear);
        total += nPasses * (perim / (spindleFeed / 60.0));                // cutting
        total += nPasses * 2.0 * zDeltaCut / (spindleFeed / 60.0);       // plunge/retract
    }

    return total;
}
