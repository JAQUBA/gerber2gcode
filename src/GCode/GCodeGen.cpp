#include "GCode/GCodeGen.h"
#include "Common/ArcMath.h"
#include "Common/GCodeFormat.h"
#include "Common/PathOptimization.h"
#include "Common/RouteStats.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

using gcodefmt::fmtF;
using gcodefmt::fmtIJ;
using gcodefmt::fmtS;
using gcodefmt::fmtSpindleS;
using gcodefmt::fmtXY;
using gcodefmt::fmtZ;

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

static double polygonSignedArea(const std::vector<geo::Point>& pts)
{
    if (pts.size() < 3) return 0.0;

    double area2 = 0.0;
    for (size_t i = 0; i < pts.size(); i++) {
        const geo::Point& a = pts[i];
        const geo::Point& b = pts[(i + 1) % pts.size()];
        area2 += a.x * b.y - b.x * a.y;
    }
    return area2 * 0.5;
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
            bool ok = arcmath::fitCircle3(pts[i].x, pts[i].y,
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
                            bool fok = arcmath::fitCircle3(pts[i].x, pts[i].y,
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

// ── Drill ordering (nearest-neighbor + 2-opt) ────────────────────────────────

std::vector<DrillHole> orderDrillHoles(const std::vector<DrillHole>& holes) {
    if (holes.size() <= 1) return holes;

    std::vector<pathopt::Point2D> pts;
    pts.reserve(holes.size());
    for (const auto& h : holes)
        pts.push_back({h.x, h.y});

    std::vector<size_t> order = pathopt::orderPointsNN2Opt(pts, 100, 0.01);
    std::vector<DrillHole> ordered;
    ordered.reserve(order.size());
    for (size_t idx : order)
        ordered.push_back(holes[idx]);

    return ordered;
}
static bool emitExactCircleContour(std::ostringstream& out,
                                   const ToolpathContour& contour,
                                   double xOffset, double yOffset,
                                   double feed)
{
    if (!contour.hasExactCircle || contour.points.size() < 3)
        return false;

    if (contour.arcRadius < MIN_ARC_RADIUS || contour.arcRadius > MAX_ARC_RADIUS)
        return false;

    double dx = contour.points.front().x - contour.arcCenterX;
    double dy = contour.points.front().y - contour.arcCenterY;
    double d = std::hypot(dx, dy);
    if (d < 1e-9)
        return false;

    double ux = dx / d;
    double uy = dy / d;
    double sx = contour.arcCenterX + ux * contour.arcRadius;
    double sy = contour.arcCenterY + uy * contour.arcRadius;
    double mx = contour.arcCenterX - ux * contour.arcRadius;
    double my = contour.arcCenterY - uy * contour.arcRadius;
    bool clockwise = polygonSignedArea(contour.points) < 0.0;

    out << (clockwise ? "G2 " : "G3 ")
        << fmtXY(mx, my, xOffset, yOffset) << " "
        << fmtIJ(contour.arcCenterX - sx, contour.arcCenterY - sy) << " "
        << fmtF(feed) << "\n";

    out << (clockwise ? "G2 " : "G3 ")
        << fmtXY(sx, sy, xOffset, yOffset) << " "
        << fmtIJ(contour.arcCenterX - mx, contour.arcCenterY - my) << " "
        << fmtF(feed) << "\n";

    return true;
}

// Try to emit a closed near-circle polyline as two exact semicircles.
// This is a robust fallback for circular contours that were not matched to
// explicit Gerber circular-pad metadata.
static bool emitCircularPolylineContour(std::ostringstream& out,
                                        const std::vector<geo::Point>& pts,
                                        double xOffset, double yOffset,
                                        double feed)
{
    if (pts.size() < 8)
        return false;

    double cx = 0.0, cy = 0.0;
    for (auto& p : pts) { cx += p.x; cy += p.y; }
    cx /= (double)pts.size();
    cy /= (double)pts.size();

    double minR = 1e18, maxR = 0.0, sumR = 0.0;
    for (auto& p : pts) {
        double r = std::hypot(p.x - cx, p.y - cy);
        if (r < minR) minR = r;
        if (r > maxR) maxR = r;
        sumR += r;
    }

    double meanR = sumR / (double)pts.size();
    if (meanR < MIN_ARC_RADIUS || meanR > MAX_ARC_RADIUS) 
        return false;

    double span = maxR - minR;
    if (span > 0.03 && (span / meanR) > 0.02)
        return false;

    // Require a tight closure to avoid forcing circles on open/elongated loops.
    double closure = std::hypot(pts.front().x - pts.back().x,
                                pts.front().y - pts.back().y);
    if (closure > 0.03)
        return false;

    double dx = pts.front().x - cx;
    double dy = pts.front().y - cy;
    double d = std::hypot(dx, dy);
    if (d < 1e-9)
        return false;

    double ux = dx / d;
    double uy = dy / d;
    double sx = cx + ux * meanR;
    double sy = cy + uy * meanR;
    double mx = cx - ux * meanR;
    double my = cy - uy * meanR;
    bool clockwise = polygonSignedArea(pts) < 0.0;

    out << (clockwise ? "G2 " : "G3 ")
        << fmtXY(mx, my, xOffset, yOffset) << " "
        << fmtIJ(cx - sx, cy - sy) << " "
        << fmtF(feed) << "\n";

    out << (clockwise ? "G2 " : "G3 ")
        << fmtXY(sx, sy, xOffset, yOffset) << " "
        << fmtIJ(cx - mx, cy - my) << " "
        << fmtF(feed) << "\n";

    return true;
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

    out << "; gerber2gcode — CNC PCB isolation engraving\n";
    out << "G21 ; mm\n";
    out << "G90 ; absolute\n";
    out << "G17 ; XY plane\n";
    out << "G94 ; feed per minute\n";
    out << "G54 ; work offset\n";
    out << "G40 ; cancel cutter compensation\n";
    out << "G49 ; cancel tool length offset\n";
    out << "G80 ; cancel canned cycles\n";
    out << "M5 ; spindle off\n";
    out << "; post profile: FluidNC\n";
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

            bool useExactCircle = job.use_arcs && contour.hasExactCircle && pts.size() >= 3;
            if (useExactCircle) {
                double dx = pts[0].x - contour.arcCenterX;
                double dy = pts[0].y - contour.arcCenterY;
                double d = std::hypot(dx, dy);
                if (d > 1e-9) {
                    sx = contour.arcCenterX + (dx / d) * contour.arcRadius;
                    sy = contour.arcCenterY + (dy / d) * contour.arcRadius;
                } else {
                    useExactCircle = false;
                }
            }

            // Rapid to start position
            out << "G0 " << fmtXY(sx, sy, xOffset, yOffset) << "\n";
            // Plunge to cutting depth
            out << "G1 " << fmtZ(zCut) << " " << fmtF(engravePlungeFeed) << "\n";

            if (useExactCircle && emitExactCircleContour(out, contour, xOffset, yOffset, engraveFeed)) {
                // Exact circular pad-offset contours are emitted as two semicircles.
            } else if (job.use_arcs && contour.arcEligible &&
                       emitCircularPolylineContour(out, pts, xOffset, yOffset, engraveFeed)) {
                // Geometric circle fallback (no exact Gerber center required).
            } else if (job.use_arcs && contour.arcEligible && pts.size() >= 3) {
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

            // Keep cutout linear for maximum robustness on controllers.
            for (size_t i = 1; i < cutoutPath.size(); i++) {
                out << "G1 " << fmtXY(cutoutPath[i].x, cutoutPath[i].y, xOffset, yOffset) << " "
                        << fmtF(spindleFeed) << "\n";
            }
            // Close the loop
            out << "G1 " << fmtXY(cutoutPath[0].x, cutoutPath[0].y, xOffset, yOffset) << " "
                << fmtF(spindleFeed) << "\n";
            // Retract
            out << "G0 " << fmtZ(zRapidClear) << "\n";
        }

        out << "\n";
    }

    out << "G0 " << fmtZ(zProgramSafe) << " ; safe Z\n";
    out << "G28 ; return to home position\n";
    out << "M5 ; spindle off\n";
    out << "M2 ; program end\n";

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

    const double engFeed      = job.engraver_feedrate        > 0 ? job.engraver_feedrate        : 300.0;
    const double engPlungeFeed= job.engraver_plunge_feedrate > 0 ? job.engraver_plunge_feedrate : 100.0;
    const double spindleFeed  = job.spindle_feedrate         > 0 ? job.spindle_feedrate         : 60.0;
    const double rapidFeed    = mc.move_feedrate             > 0 ? mc.move_feedrate             : 2400.0;

    // Z coordinate model: Z=0 is bed, material top at Z=materialThickness
    const double mat          = mc.materialThickness;
    const double zRapidClear  = mat + 1.0;
    const double zCut         = mat - std::abs(mc.engraver_z_cut);
    const double zDrillPre    = mat + mc.spindle_z_pre_drill;
    const double zDrill       = std::max(0.0, mat - std::abs(mc.spindle_z_drill));
    // Seconds per plunge cycle (down to cut Z + back to rapid clear), using engraver plunge feed
    const double plungeTimeSec = engPlungeFeed > 0
        ? 2.0 * std::abs(zRapidClear - zCut) / (engPlungeFeed / 60.0) : 0.0;
    // Seconds per drill hole (approach + drill + retract)
    const double drillTimeSec = spindleFeed > 0
        ? 2.0 * std::abs(zDrillPre - zDrill) / (spindleFeed / 60.0)
          + 2.0 * std::abs(zRapidClear - zDrillPre) / (rapidFeed / 60.0)
          + job.drill_dwell
        : 0.0;

    routestats::RouteStats rs;

    // ── Isolation contours ──
    double prevX = 0, prevY = 0;
    for (auto& contour : contours) {
        if (contour.points.empty()) continue;
        double sx = contour.points[0].x, sy = contour.points[0].y;
        rs.addRapid(std::hypot(sx - prevX, sy - prevY));
        rs.addPlunge();
        rs.addContour();
        double px = sx, py = sy;
        for (size_t i = 1; i < contour.points.size(); i++) {
            double cx = contour.points[i].x, cy = contour.points[i].y;
            rs.addCut(std::hypot(cx - px, cy - py));
            px = cx; py = cy;
        }
        rs.addCut(std::hypot(sx - px, sy - py)); // closing segment
        prevX = sx; prevY = sy;
    }

    // ── Drill holes ──
    if (!holes.empty()) {
        prevX = 0; prevY = 0;
        for (auto& h : holes) {
            rs.addRapid(std::hypot(h.x - prevX, h.y - prevY));
            rs.addDrill();
            prevX = h.x; prevY = h.y;
        }
    }

    // ── Cutout (multi-pass) — accumulate separately at spindle feed ──
    double cutoutTimeSec = 0.0;
    if (!cutoutPath.empty()) {
        double stepDown = std::abs(mc.cutout_z_step);
        if (stepDown < 0.01) stepDown = 0.5;
        double perim = 0.0;
        for (size_t i = 1; i < cutoutPath.size(); i++)
            perim += std::hypot(cutoutPath[i].x - cutoutPath[i-1].x,
                               cutoutPath[i].y - cutoutPath[i-1].y);
        perim += std::hypot(cutoutPath[0].x - cutoutPath.back().x,
                           cutoutPath[0].y - cutoutPath.back().y);
        int nPasses = 0;
        double cz = mat;
        while (cz > 0.001) { cz -= stepDown; if (cz < 0.0) cz = 0.0; ++nPasses; }
        if (spindleFeed > 0)
            cutoutTimeSec = nPasses * (perim / (spindleFeed / 60.0))
                          + nPasses * 2.0 * std::abs(zRapidClear) / (spindleFeed / 60.0);
    }

    return rs.estimateTimeSec(rapidFeed, engFeed, plungeTimeSec, drillTimeSec) + cutoutTimeSec;
}
