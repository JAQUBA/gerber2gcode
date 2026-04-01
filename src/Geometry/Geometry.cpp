#include "Geometry/Geometry.h"

namespace geo {

Path makeCircle(double cx, double cy, double radius, int segments) {
    Path p;
    p.reserve(segments);
    for (int i = 0; i < segments; i++) {
        double a = 2.0 * PI * i / segments;
        p.push_back(Point(cx + radius * std::cos(a), cy + radius * std::sin(a)));
    }
    return p;
}

Path makeRect(double cx, double cy, double w, double h) {
    double hw = w / 2.0, hh = h / 2.0;
    return {
        Point(cx - hw, cy - hh), Point(cx + hw, cy - hh),
        Point(cx + hw, cy + hh), Point(cx - hw, cy + hh)
    };
}

Path makeObround(double cx, double cy, double w, double h, int segments) {
    // Obround = rectangle with semicircle caps on short sides
    Path p;
    double hw = w / 2.0, hh = h / 2.0;
    double r = std::min(hw, hh);
    if (w >= h) {
        double ix = hw - r;
        // Right semicircle
        for (int i = 0; i <= segments; i++) {
            double a = -PI / 2.0 + PI * i / segments;
            p.push_back(Point(cx + ix + r * std::cos(a), cy + r * std::sin(a)));
        }
        // Left semicircle
        for (int i = 0; i <= segments; i++) {
            double a = PI / 2.0 + PI * i / segments;
            p.push_back(Point(cx - ix + r * std::cos(a), cy + r * std::sin(a)));
        }
    } else {
        double iy = hh - r;
        // Top semicircle
        for (int i = 0; i <= segments; i++) {
            double a = PI * i / segments;
            p.push_back(Point(cx + r * std::cos(a), cy + iy + r * std::sin(a)));
        }
        // Bottom semicircle
        for (int i = 0; i <= segments; i++) {
            double a = PI + PI * i / segments;
            p.push_back(Point(cx + r * std::cos(a), cy - iy + r * std::sin(a)));
        }
    }
    return p;
}

Path makeRegPoly(double cx, double cy, double outerDia, int nVerts, double rotDeg) {
    Path p;
    double r = outerDia / 2.0;
    double rotRad = rotDeg * PI / 180.0;
    for (int i = 0; i < nVerts; i++) {
        double a = 2.0 * PI * i / nVerts + rotRad;
        p.push_back(Point(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
    return p;
}

Paths bufferLine(const Point& start, const Point& end, double width) {
    Clipper2Lib::PathD line = {start, end};
    Clipper2Lib::PathsD lines = {line};
    return Clipper2Lib::InflatePaths(lines, width / 2.0,
        Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Round, 2.0, PRECISION);
}

Paths bufferPath(const std::vector<Point>& pts, double width) {
    if (pts.size() < 2) return {};
    Clipper2Lib::PathD line(pts.begin(), pts.end());
    Clipper2Lib::PathsD lines = {line};
    return Clipper2Lib::InflatePaths(lines, width / 2.0,
        Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Round, 2.0, PRECISION);
}

Paths unionAll(const Paths& paths) {
    if (paths.empty()) return {};
    return Clipper2Lib::Union(paths, Clipper2Lib::FillRule::NonZero, PRECISION);
}

Paths difference(const Paths& subject, const Paths& clip) {
    if (subject.empty()) return {};
    if (clip.empty()) return subject;
    return Clipper2Lib::Difference(subject, clip, Clipper2Lib::FillRule::NonZero, PRECISION);
}

Paths intersect(const Paths& subject, const Paths& clip) {
    if (subject.empty() || clip.empty()) return {};
    return Clipper2Lib::Intersect(subject, clip, Clipper2Lib::FillRule::NonZero, PRECISION);
}

Paths offset(const Paths& paths, double delta) {
    if (paths.empty()) return {};
    return Clipper2Lib::InflatePaths(paths, delta,
        Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon, 2.0, PRECISION);
}

Paths simplifyPaths(const Paths& paths, double tolerance) {
    if (paths.empty()) return {};
    return Clipper2Lib::SimplifyPaths(paths, tolerance);
}

Path translate(const Path& path, double dx, double dy) {
    Path out;
    out.reserve(path.size());
    for (auto& pt : path) out.push_back(Point(pt.x + dx, pt.y + dy));
    return out;
}

Paths translateAll(const Paths& paths, double dx, double dy) {
    Paths out;
    out.reserve(paths.size());
    for (auto& p : paths) out.push_back(translate(p, dx, dy));
    return out;
}

Paths flipX(const Paths& paths, double boardWidth) {
    Paths out;
    out.reserve(paths.size());
    for (auto& p : paths) {
        Path np;
        np.reserve(p.size());
        for (auto& pt : p) np.push_back(Point(boardWidth - pt.x, pt.y));
        out.push_back(np);
    }
    return out;
}

bool isEmpty(const Paths& paths) {
    if (paths.empty()) return true;
    for (auto& p : paths) if (!p.empty()) return false;
    return true;
}

double totalArea(const Paths& paths) {
    double a = 0;
    for (auto& p : paths) a += std::abs(Clipper2Lib::Area(p));
    return a;
}

} // namespace geo
