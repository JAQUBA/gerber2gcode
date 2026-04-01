#pragma once
#include <clipper2/clipper.h>
#include <vector>
#include <cmath>

namespace geo {

using Point  = Clipper2Lib::PointD;
using Path   = Clipper2Lib::PathD;
using Paths  = Clipper2Lib::PathsD;

static constexpr int PRECISION = 4;
static constexpr double PI = 3.14159265358979323846;

Path  makeCircle(double cx, double cy, double radius, int segments = 32);
Path  makeRect(double cx, double cy, double w, double h);
Path  makeObround(double cx, double cy, double w, double h, int segments = 16);
Path  makeRegPoly(double cx, double cy, double outerDia, int nVerts, double rotDeg = 0.0);

Paths bufferLine(const Point& start, const Point& end, double width);
Paths bufferPath(const std::vector<Point>& pts, double width);

Paths unionAll(const Paths& paths);
Paths difference(const Paths& subject, const Paths& clip);
Paths intersect(const Paths& subject, const Paths& clip);
Paths offset(const Paths& paths, double delta);
Paths simplifyPaths(const Paths& paths, double tolerance);

Path  translate(const Path& path, double dx, double dy);
Paths translateAll(const Paths& paths, double dx, double dy);
Paths flipX(const Paths& paths, double boardWidth);

bool   isEmpty(const Paths& paths);
double totalArea(const Paths& paths);

} // namespace geo
