#include "Gerber/GerberParser.h"

#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>

using namespace geo;

// ═══════════════════════════════════════════════════════════════════════════════
// Internal types
// ═══════════════════════════════════════════════════════════════════════════════

enum class ApertureType { Circle, Rectangle, Obround, Polygon, Macro };

struct Aperture {
    ApertureType type;
    std::vector<double> params;
    std::string macroName;
};

struct MacroPrimitive {
    std::string raw;  // raw primitive line (e.g. "21,1,$4-$2,$7-$3,0,0,0")
};

struct MacroTemplate {
    std::string name;
    std::vector<MacroPrimitive> primitives;
};

enum class InterpMode { Linear, CW_Arc, CCW_Arc };
enum class Polarity   { Dark, Clear };
enum class FeatureType { Trace, Pad, Region };

struct ParserState {
    bool isMetric          = true;
    int  xFrac             = 6;
    int  yFrac             = 6;
    bool leadingZeroSupp   = true;
    InterpMode interpMode  = InterpMode::Linear;
    bool regionMode        = false;
    Polarity polarity      = Polarity::Dark;

    int  currentAperture   = -1;
    double curX = 0.0, curY = 0.0;

    std::map<int, Aperture>             apertures;
    std::map<std::string, MacroTemplate> macros;

    Paths darkPaths;
    Paths clearPaths;

    // Categorized dark paths (for GerberComponents output)
    Paths darkTraces;
    Paths darkPads;
    Paths darkRegions;

    // Pads grouped by aperture number (for per-aperture visibility)
    std::map<int, Paths> darkPadsByAperture;
    // D03 flash center positions per aperture (for arc eligibility)
    std::map<int, std::vector<Point>> darkPadCenters;

    std::vector<Point> regionPoints;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Coordinate parsing
// ═══════════════════════════════════════════════════════════════════════════════

static double parseCoord(const std::string& raw, int fracDigits, bool leadingSuppression) {
    if (raw.empty()) return 0.0;
    // If it contains a decimal point, parse directly
    if (raw.find('.') != std::string::npos)
        return std::stod(raw);

    bool neg = false;
    std::string s = raw;
    if (s[0] == '+') s = s.substr(1);
    else if (s[0] == '-') { neg = true; s = s.substr(1); }

    if (leadingSuppression) {
        // Pad leading zeros (right-justify)
        while ((int)s.size() < fracDigits + 1)
            s = "0" + s;
    } else {
        // Pad trailing zeros (left-justify)
        while ((int)s.size() < fracDigits + 1)
            s += "0";
    }

    int splitPos = (int)s.size() - fracDigits;
    if (splitPos < 0) splitPos = 0;
    std::string intPart  = s.substr(0, splitPos);
    std::string fracPart = s.substr(splitPos);
    if (intPart.empty()) intPart = "0";

    double val = std::stod(intPart + "." + fracPart);
    return neg ? -val : val;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Macro expression evaluator (handles $N, +, -, *, /, x, X, parentheses)
// ═══════════════════════════════════════════════════════════════════════════════

class ExprEval {
    const std::string& expr;
    const std::vector<double>& vars;
    size_t pos;
public:
    ExprEval(const std::string& e, const std::vector<double>& v)
        : expr(e), vars(v), pos(0) {}

    double evaluate() {
        double r = parseExpr();
        return r;
    }
private:
    void skipSpaces() {
        while (pos < expr.size() && expr[pos] == ' ') pos++;
    }

    double parseExpr() {
        double left = parseTerm();
        while (pos < expr.size()) {
            skipSpaces();
            if (pos >= expr.size()) break;
            char op = expr[pos];
            if (op == '+') { pos++; left += parseTerm(); }
            else if (op == '-') { pos++; left -= parseTerm(); }
            else break;
        }
        return left;
    }

    double parseTerm() {
        double left = parseFactor();
        while (pos < expr.size()) {
            skipSpaces();
            if (pos >= expr.size()) break;
            char op = expr[pos];
            if (op == '*' || op == 'x' || op == 'X') { pos++; left *= parseFactor(); }
            else if (op == '/') {
                pos++;
                double d = parseFactor();
                left = (d != 0.0) ? left / d : 0.0;
            }
            else break;
        }
        return left;
    }

    double parseFactor() {
        skipSpaces();
        if (pos >= expr.size()) return 0.0;

        // Unary minus
        if (expr[pos] == '-') {
            pos++;
            return -parseFactor();
        }
        // Parentheses
        if (expr[pos] == '(') {
            pos++;
            double v = parseExpr();
            skipSpaces();
            if (pos < expr.size() && expr[pos] == ')') pos++;
            return v;
        }
        // Variable $N
        if (expr[pos] == '$') {
            pos++;
            int idx = 0;
            while (pos < expr.size() && std::isdigit((unsigned char)expr[pos])) {
                idx = idx * 10 + (expr[pos] - '0');
                pos++;
            }
            if (idx >= 1 && idx <= (int)vars.size()) return vars[idx - 1];
            return 0.0;
        }
        // Number
        size_t start = pos;
        if (pos < expr.size() && expr[pos] == '+') pos++;
        while (pos < expr.size() && (std::isdigit((unsigned char)expr[pos]) || expr[pos] == '.'))
            pos++;
        if (pos == start) return 0.0;
        return std::stod(expr.substr(start, pos - start));
    }
};

static double evalExpr(const std::string& expr, const std::vector<double>& vars) {
    ExprEval ev(expr, vars);
    return ev.evaluate();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Arc → line segments conversion
// ═══════════════════════════════════════════════════════════════════════════════

static std::vector<Point> arcToSegments(
    Point start, Point end, Point center, bool clockwise)
{
    double cx = center.x, cy = center.y;
    double r = (std::hypot(start.x - cx, start.y - cy)
              + std::hypot(end.x - cx, end.y - cy)) / 2.0;
    if (r < 1e-9) return {end};

    double startAngle = std::atan2(start.y - cy, start.x - cx);
    double endAngle   = std::atan2(end.y - cy, end.x - cx);

    if (clockwise) {
        if (endAngle >= startAngle) endAngle -= 2.0 * PI;
    } else {
        if (endAngle <= startAngle) endAngle += 2.0 * PI;
    }

    double sweep = std::abs(endAngle - startAngle);
    int nSeg = std::max(2, (int)(sweep * 180.0 / PI));

    std::vector<Point> pts;
    pts.reserve(nSeg);
    for (int i = 1; i <= nSeg; i++) {
        double t = (double)i / nSeg;
        double a = startAngle + t * (endAngle - startAngle);
        pts.push_back(Point(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
    if (!pts.empty()) pts.back() = end;
    return pts;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Aperture → shape polygon
// ═══════════════════════════════════════════════════════════════════════════════

static Path apertureToShape(const Aperture& ap) {
    switch (ap.type) {
        case ApertureType::Circle:
            if (ap.params.size() >= 1 && ap.params[0] > 0)
                return makeCircle(0, 0, ap.params[0] / 2.0);
            break;
        case ApertureType::Rectangle:
            if (ap.params.size() >= 2 && ap.params[0] > 0 && ap.params[1] > 0)
                return makeRect(0, 0, ap.params[0], ap.params[1]);
            break;
        case ApertureType::Obround:
            if (ap.params.size() >= 2 && ap.params[0] > 0 && ap.params[1] > 0)
                return makeObround(0, 0, ap.params[0], ap.params[1]);
            break;
        case ApertureType::Polygon:
            if (ap.params.size() >= 2) {
                int n = (int)ap.params[1];
                double rot = (ap.params.size() >= 3) ? ap.params[2] : 0.0;
                return makeRegPoly(0, 0, ap.params[0], n, rot);
            }
            break;
        default:
            break;
    }
    return {};
}

static double apertureWidth(const Aperture& ap) {
    switch (ap.type) {
        case ApertureType::Circle:
            return (ap.params.size() >= 1) ? ap.params[0] : 0.0;
        case ApertureType::Rectangle:
            return (ap.params.size() >= 2) ? std::min(ap.params[0], ap.params[1]) : 0.0;
        case ApertureType::Obround:
            return (ap.params.size() >= 2) ? std::min(ap.params[0], ap.params[1]) : 0.0;
        case ApertureType::Polygon:
            return (ap.params.size() >= 1) ? ap.params[0] : 0.0;
        case ApertureType::Macro:
            return (ap.params.size() >= 1) ? ap.params[0] : 0.0;
    }
    return 0.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Macro instantiation → polygon
// ═══════════════════════════════════════════════════════════════════════════════

static void splitCSV(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    std::string token;
    int paren = 0;
    for (char c : s) {
        if (c == '(') paren++;
        else if (c == ')') paren--;
        if (c == ',' && paren == 0) {
            out.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    if (!token.empty()) out.push_back(token);
}

static Paths renderMacro(const MacroTemplate& tmpl, const std::vector<double>& vars) {
    Paths result;

    for (auto& prim : tmpl.primitives) {
        std::string raw = prim.raw;
        // Trim whitespace
        while (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t' || raw[0] == '\n' || raw[0] == '\r'))
            raw.erase(raw.begin());
        if (raw.empty()) continue;

        std::vector<std::string> fields;
        splitCSV(raw, fields);
        if (fields.empty()) continue;

        int code = (int)evalExpr(fields[0], vars);
        if (code == 0) continue; // Comment

        auto val = [&](int idx) -> double {
            return (idx < (int)fields.size()) ? evalExpr(fields[idx], vars) : 0.0;
        };

        if (code == 1) {
            // Circle: exposure, diameter, cx, cy [, rotation]
            double d   = val(2);
            double cx  = val(3);
            double cy  = val(4);
            if (d > 0) {
                Path circle = makeCircle(cx, cy, d / 2.0);
                result.push_back(circle);
            }
        } else if (code == 20) {
            // Vector line: exposure, width, sx, sy, ex, ey, rotation
            double w  = val(2);
            double sx = val(3), sy = val(4);
            double ex = val(5), ey = val(6);
            if (w > 0) {
                auto buf = bufferLine(Point(sx, sy), Point(ex, ey), w);
                result.insert(result.end(), buf.begin(), buf.end());
            }
        } else if (code == 21) {
            // Center line: exposure, width, height, cx, cy, rotation
            double w  = val(2);
            double h  = val(3);
            double cx = val(4);
            double cy = val(5);
            if (w > 0 && h > 0) {
                Path rect = makeRect(cx, cy, w, h);
                result.push_back(rect);
            }
        } else if (code == 4) {
            // Outline: exposure, n_vertices, x0, y0, ..., xn, yn, rotation
            int nv = (int)val(2);
            Path outline;
            for (int i = 0; i <= nv; i++) {
                double x = val(3 + i * 2);
                double y = val(4 + i * 2);
                outline.push_back(Point(x, y));
            }
            if ((int)outline.size() >= 3)
                result.push_back(outline);
        } else if (code == 5) {
            // Polygon: exposure, n_vertices, cx, cy, diameter, rotation
            int nv   = (int)val(2);
            double cx = val(3), cy = val(4);
            double d  = val(5);
            double rot = val(6);
            if (nv >= 3 && d > 0) {
                Path poly = makeRegPoly(cx, cy, d, nv, rot);
                result.push_back(poly);
            }
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Gerber extended command processing
// ═══════════════════════════════════════════════════════════════════════════════

static void processExtendedBlock(ParserState& st, const std::string& block) {
    // Split block on '*' (within the % delimiters)
    std::vector<std::string> parts;
    std::string cur;
    for (char c : block) {
        if (c == '*') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(cur);

    if (parts.empty()) return;
    std::string& first = parts[0];

    // Format specification: FSLAX46Y46
    if (first.size() >= 4 && first.substr(0, 2) == "FS") {
        st.leadingZeroSupp = (first.find('L') != std::string::npos);
        // Find X spec
        size_t xp = first.find('X');
        if (xp != std::string::npos && xp + 2 < first.size()) {
            st.xFrac = first[xp + 2] - '0';
        }
        size_t yp = first.find('Y');
        if (yp != std::string::npos && yp + 2 < first.size()) {
            st.yFrac = first[yp + 2] - '0';
        }
        return;
    }

    // Units
    if (first == "MOMM") { st.isMetric = true; return; }
    if (first == "MOIN") { st.isMetric = false; return; }

    // Layer polarity
    if (first == "LPD") { st.polarity = Polarity::Dark;  return; }
    if (first == "LPC") { st.polarity = Polarity::Clear; return; }

    // Aperture macro definition
    if (first.size() >= 2 && first.substr(0, 2) == "AM") {
        MacroTemplate tmpl;
        tmpl.name = first.substr(2);
        for (size_t i = 1; i < parts.size(); i++) {
            std::string p = parts[i];
            // Trim
            while (!p.empty() && (p[0] == ' ' || p[0] == '\n' || p[0] == '\r'))
                p.erase(p.begin());
            while (!p.empty() && (p.back() == ' ' || p.back() == '\n' || p.back() == '\r'))
                p.pop_back();
            if (p.empty()) continue;
            if (p[0] == '0') continue; // Comment primitive
            tmpl.primitives.push_back({p});
        }
        st.macros[tmpl.name] = tmpl;
        return;
    }

    // Aperture definition: ADD<code><type>,<params>
    if (first.size() >= 4 && first.substr(0, 2) == "AD") {
        // Parse D-code
        size_t i = 3; // skip "ADD" — first 'D' is at index 2
        int dCode = 0;
        while (i < first.size() && std::isdigit((unsigned char)first[i])) {
            dCode = dCode * 10 + (first[i] - '0');
            i++;
        }
        std::string rest = first.substr(i);

        // Split type and params at comma
        size_t comma = rest.find(',');
        std::string typeStr;
        std::vector<double> params;

        if (comma != std::string::npos) {
            typeStr = rest.substr(0, comma);
            std::string paramStr = rest.substr(comma + 1);
            // Split params on 'X'
            std::string tok;
            for (char c : paramStr) {
                if (c == 'X' || c == 'x') {
                    if (!tok.empty()) {
                        try { params.push_back(std::stod(tok)); } catch (...) {}
                        tok.clear();
                    }
                } else {
                    tok += c;
                }
            }
            if (!tok.empty()) {
                try { params.push_back(std::stod(tok)); } catch (...) {}
            }
        } else {
            typeStr = rest;
        }

        Aperture ap;
        if (typeStr == "C")      ap.type = ApertureType::Circle;
        else if (typeStr == "R") ap.type = ApertureType::Rectangle;
        else if (typeStr == "O") ap.type = ApertureType::Obround;
        else if (typeStr == "P") ap.type = ApertureType::Polygon;
        else {
            ap.type = ApertureType::Macro;
            ap.macroName = typeStr;
        }
        ap.params = params;
        st.apertures[dCode] = ap;
        return;
    }

    // Ignore other extended commands (TF, TA, TD, SR, etc.)
}

// ═══════════════════════════════════════════════════════════════════════════════
// Data command parsing
// ═══════════════════════════════════════════════════════════════════════════════

struct DataCmd {
    bool hasX = false, hasY = false, hasI = false, hasJ = false;
    std::string xStr, yStr, iStr, jStr;
    int dCode = -1;       // D01/D02/D03 or aperture select (D10+)
    int gCode = -1;       // G01/G02/G03/G36/G37/G75 etc.
};

static DataCmd parseDataCmd(const std::string& cmd) {
    DataCmd dc;
    size_t i = 0;
    while (i < cmd.size()) {
        char c = cmd[i];
        if (c == 'G') {
            i++;
            std::string num;
            while (i < cmd.size() && std::isdigit((unsigned char)cmd[i]))
                num += cmd[i++];
            if (!num.empty()) dc.gCode = std::stoi(num);
        } else if (c == 'D') {
            i++;
            std::string num;
            while (i < cmd.size() && std::isdigit((unsigned char)cmd[i]))
                num += cmd[i++];
            if (!num.empty()) dc.dCode = std::stoi(num);
        } else if (c == 'X') {
            i++;
            dc.hasX = true;
            while (i < cmd.size() && (std::isdigit((unsigned char)cmd[i]) || cmd[i] == '-' || cmd[i] == '+' || cmd[i] == '.'))
                dc.xStr += cmd[i++];
        } else if (c == 'Y') {
            i++;
            dc.hasY = true;
            while (i < cmd.size() && (std::isdigit((unsigned char)cmd[i]) || cmd[i] == '-' || cmd[i] == '+' || cmd[i] == '.'))
                dc.yStr += cmd[i++];
        } else if (c == 'I') {
            i++;
            dc.hasI = true;
            while (i < cmd.size() && (std::isdigit((unsigned char)cmd[i]) || cmd[i] == '-' || cmd[i] == '+' || cmd[i] == '.'))
                dc.iStr += cmd[i++];
        } else if (c == 'J') {
            i++;
            dc.hasJ = true;
            while (i < cmd.size() && (std::isdigit((unsigned char)cmd[i]) || cmd[i] == '-' || cmd[i] == '+' || cmd[i] == '.'))
                dc.jStr += cmd[i++];
        } else if (c == 'M') {
            // M02 end
            break;
        } else {
            i++;
        }
    }
    return dc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Add geometry to dark/clear lists
// ═══════════════════════════════════════════════════════════════════════════════

static void addPaths(ParserState& st, const Paths& paths, FeatureType ft = FeatureType::Trace) {
    auto& target = (st.polarity == Polarity::Dark) ? st.darkPaths : st.clearPaths;
    target.insert(target.end(), paths.begin(), paths.end());
    if (st.polarity == Polarity::Dark) {
        auto& cat = (ft == FeatureType::Pad) ? st.darkPads :
                    (ft == FeatureType::Region) ? st.darkRegions : st.darkTraces;
        cat.insert(cat.end(), paths.begin(), paths.end());
        if (ft == FeatureType::Pad && st.currentAperture >= 0) {
            auto& apPads = st.darkPadsByAperture[st.currentAperture];
            apPads.insert(apPads.end(), paths.begin(), paths.end());
        }
    }
}

static void addPath(ParserState& st, const Path& path, FeatureType ft = FeatureType::Region) {
    if (path.empty()) return;
    auto& target = (st.polarity == Polarity::Dark) ? st.darkPaths : st.clearPaths;
    target.push_back(path);
    if (st.polarity == Polarity::Dark) {
        auto& cat = (ft == FeatureType::Pad) ? st.darkPads :
                    (ft == FeatureType::Region) ? st.darkRegions : st.darkTraces;
        cat.push_back(path);
        if (ft == FeatureType::Pad && st.currentAperture >= 0) {
            st.darkPadsByAperture[st.currentAperture].push_back(path);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Process a single data command
// ═══════════════════════════════════════════════════════════════════════════════

static void processDataCommand(ParserState& st, const std::string& cmdStr) {
    if (cmdStr.empty()) return;

    // End of file
    if (cmdStr.find("M02") != std::string::npos || cmdStr.find("M00") != std::string::npos)
        return;

    DataCmd dc = parseDataCmd(cmdStr);

    // G-code mode switches
    if (dc.gCode == 1)  st.interpMode = InterpMode::Linear;
    if (dc.gCode == 2)  st.interpMode = InterpMode::CW_Arc;
    if (dc.gCode == 3)  st.interpMode = InterpMode::CCW_Arc;
    if (dc.gCode == 36) { st.regionMode = true;  st.regionPoints.clear(); return; }
    if (dc.gCode == 37) {
        // End region — close polygon
        if (st.regionPoints.size() >= 3) {
            Path p(st.regionPoints.begin(), st.regionPoints.end());
            addPath(st, p);
        }
        st.regionMode = false;
        st.regionPoints.clear();
        return;
    }
    // G75 multi-quadrant — just set flag (we always use multi-quadrant)
    // G04 comment — ignore
    if (dc.gCode == 4 || dc.gCode == 75 || dc.gCode == 74) return;

    // Aperture select (D10+)
    if (dc.dCode >= 10 && !dc.hasX && !dc.hasY) {
        st.currentAperture = dc.dCode;
        return;
    }

    // Parse coordinates
    double newX = st.curX, newY = st.curY;
    if (dc.hasX) {
        double raw = parseCoord(dc.xStr, st.xFrac, st.leadingZeroSupp);
        newX = st.isMetric ? raw : raw * 25.4;
    }
    if (dc.hasY) {
        double raw = parseCoord(dc.yStr, st.yFrac, st.leadingZeroSupp);
        newY = st.isMetric ? raw : raw * 25.4;
    }

    double prevX = st.curX, prevY = st.curY;

    int dOp = dc.dCode;
    // dOp: 1=draw, 2=move, 3=flash

    if (st.regionMode) {
        if (dOp == 2 || dOp == -1) {
            // Move or first point — start new contour or just update position
            if (st.regionPoints.empty() || dOp == 2) {
                if (st.regionPoints.size() >= 3) {
                    Path p(st.regionPoints.begin(), st.regionPoints.end());
                    addPath(st, p);
                    st.regionPoints.clear();
                }
                st.regionPoints.push_back(Point(newX, newY));
            }
        } else if (dOp == 1) {
            if (st.regionPoints.empty())
                st.regionPoints.push_back(Point(prevX, prevY));

            if (st.interpMode == InterpMode::Linear) {
                st.regionPoints.push_back(Point(newX, newY));
            } else {
                // Arc in region
                double iOff = 0, jOff = 0;
                if (dc.hasI) iOff = parseCoord(dc.iStr, st.xFrac, st.leadingZeroSupp);
                if (dc.hasJ) jOff = parseCoord(dc.jStr, st.yFrac, st.leadingZeroSupp);
                if (!st.isMetric) { iOff *= 25.4; jOff *= 25.4; }

                Point ctr(prevX + iOff, prevY + jOff);
                bool cw = (st.interpMode == InterpMode::CW_Arc);
                auto arcPts = arcToSegments(Point(prevX, prevY), Point(newX, newY), ctr, cw);
                st.regionPoints.insert(st.regionPoints.end(), arcPts.begin(), arcPts.end());
            }
        }
    } else {
        // Normal mode
        if (dOp == 2) {
            // Move — no drawing
        } else if (dOp == 3) {
            // Flash
            auto it = st.apertures.find(st.currentAperture);
            if (it != st.apertures.end()) {
                auto& ap = it->second;
                if (ap.type == ApertureType::Macro) {
                    auto mit = st.macros.find(ap.macroName);
                    if (mit != st.macros.end()) {
                        Paths macroPaths = renderMacro(mit->second, ap.params);
                        if (!macroPaths.empty()) {
                            Paths merged = unionAll(macroPaths);
                            Paths translated = translateAll(merged, newX, newY);
                            addPaths(st, translated, FeatureType::Pad);
                        }
                    }
                } else {
                    Path shape = apertureToShape(ap);
                    if (!shape.empty()) {
                        Path translated = geo::translate(shape, newX, newY);
                        addPath(st, translated, FeatureType::Pad);
                    }
                }
                // Record flash center for arc eligibility
                if (st.polarity == Polarity::Dark && st.currentAperture >= 0)
                    st.darkPadCenters[st.currentAperture].push_back(Point(newX, newY));
            }
        } else if (dOp == 1) {
            // Draw (interpolate)
            auto it = st.apertures.find(st.currentAperture);
            double w = 0.0;
            if (it != st.apertures.end()) w = apertureWidth(it->second);

            if (st.interpMode == InterpMode::Linear) {
                if (w > 0) {
                    Paths buf = bufferLine(Point(prevX, prevY), Point(newX, newY), w);
                    addPaths(st, buf, FeatureType::Trace);
                }
            } else {
                // Arc draw
                double iOff = 0, jOff = 0;
                if (dc.hasI) iOff = parseCoord(dc.iStr, st.xFrac, st.leadingZeroSupp);
                if (dc.hasJ) jOff = parseCoord(dc.jStr, st.yFrac, st.leadingZeroSupp);
                if (!st.isMetric) { iOff *= 25.4; jOff *= 25.4; }

                Point ctr(prevX + iOff, prevY + jOff);
                bool cw = (st.interpMode == InterpMode::CW_Arc);
                auto arcPts = arcToSegments(Point(prevX, prevY), Point(newX, newY), ctr, cw);

                if (w > 0 && !arcPts.empty()) {
                    std::vector<Point> fullPath;
                    fullPath.push_back(Point(prevX, prevY));
                    fullPath.insert(fullPath.end(), arcPts.begin(), arcPts.end());
                    Paths buf = bufferPath(fullPath, w);
                    addPaths(st, buf, FeatureType::Trace);
                }
            }
        }
    }

    st.curX = newX;
    st.curY = newY;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main Gerber parser
// ═══════════════════════════════════════════════════════════════════════════════

static std::string readFileContents(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open Gerber file: " + path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static ParserState runParser(const std::string& filepath) {
    std::string content = readFileContents(filepath);
    ParserState st;

    enum { Normal, Extended } mode = Normal;
    std::string currentCmd;
    std::string extBlock;

    for (size_t i = 0; i < content.size(); i++) {
        char c = content[i];

        if (c == '%') {
            if (mode == Normal) {
                mode = Extended;
                extBlock.clear();
            } else {
                processExtendedBlock(st, extBlock);
                mode = Normal;
                extBlock.clear();
            }
            continue;
        }

        if (mode == Extended) {
            extBlock += c;
            continue;
        }

        if (c == '*') {
            if (!currentCmd.empty()) {
                processDataCommand(st, currentCmd);
                currentCmd.clear();
            }
            continue;
        }

        if (c == '\n' || c == '\r') continue;
        currentCmd += c;
    }

    return st;
}

geo::Paths parseGerber(const std::string& filepath) {
    ParserState st = runParser(filepath);

    if (st.darkPaths.empty()) return {};

    Paths result = unionAll(st.darkPaths);
    if (!st.clearPaths.empty()) {
        Paths clear = unionAll(st.clearPaths);
        result = difference(result, clear);
    }
    return result;
}

GerberComponents parseGerberComponents(const std::string& filepath) {
    ParserState st = runParser(filepath);

    GerberComponents gc;
    if (!st.darkTraces.empty())  gc.traces  = unionAll(st.darkTraces);
    if (!st.darkPads.empty())    gc.pads    = unionAll(st.darkPads);
    if (!st.darkRegions.empty()) gc.regions = unionAll(st.darkRegions);

    // Apply clear polarity subtraction to each category
    Paths clear;
    if (!st.clearPaths.empty())
        clear = unionAll(st.clearPaths);

    if (!clear.empty()) {
        if (!gc.traces.empty())  gc.traces  = difference(gc.traces, clear);
        if (!gc.pads.empty())    gc.pads    = difference(gc.pads, clear);
        if (!gc.regions.empty()) gc.regions = difference(gc.regions, clear);
    }

    // Build pad groups from per-aperture map
    for (auto& kv : st.darkPadsByAperture) {
        int apNum = kv.first;
        auto& apPaths = kv.second;
        if (apPaths.empty()) continue;

        PadGroup pg;
        pg.apNum = apNum;
        pg.count = (int)apPaths.size();

        // Generate name and set arc-eligibility from aperture info
        auto ait = st.apertures.find(apNum);
        if (ait != st.apertures.end()) {
            auto& ap = ait->second;
            char buf[64];
            switch (ap.type) {
                case ApertureType::Circle:
                    _snprintf(buf, sizeof(buf), "Circle \xC3\x98%.3fmm", ap.params.size() > 0 ? ap.params[0] : 0);
                    pg.name = buf;
                    pg.isCircular = true;
                    pg.apertureRadius = (ap.params.size() > 0) ? ap.params[0] / 2.0 : 0;
                    break;
                case ApertureType::Rectangle:
                    _snprintf(buf, sizeof(buf), "Rect %.2f\xC3\x97%.2fmm",
                             ap.params.size() > 0 ? ap.params[0] : 0,
                             ap.params.size() > 1 ? ap.params[1] : 0);
                    pg.name = buf;
                    break;
                case ApertureType::Obround:
                    _snprintf(buf, sizeof(buf), "Obround %.2f\xC3\x97%.2fmm",
                             ap.params.size() > 0 ? ap.params[0] : 0,
                             ap.params.size() > 1 ? ap.params[1] : 0);
                    pg.name = buf;
                    break;
                case ApertureType::Polygon:
                    _snprintf(buf, sizeof(buf), "Polygon %d-gon",
                             ap.params.size() > 1 ? (int)ap.params[1] : 0);
                    pg.name = buf;
                    break;
                case ApertureType::Macro:
                    pg.name = ap.macroName;
                    break;
            }
        } else {
            char buf[32];
            _snprintf(buf, sizeof(buf), "D%d", apNum);
            pg.name = buf;
        }

        // Copy flash centers for this aperture
        auto cit = st.darkPadCenters.find(apNum);
        if (cit != st.darkPadCenters.end())
            pg.centers = cit->second;

        // Union paths for this group and apply clear subtraction
        pg.paths = unionAll(apPaths);
        if (!clear.empty() && !pg.paths.empty())
            pg.paths = difference(pg.paths, clear);

        if (!pg.paths.empty())
            gc.padGroups.push_back(pg);
    }

    return gc;
}

geo::Paths GerberComponents::combined() const {
    Paths all;
    all.insert(all.end(), traces.begin(), traces.end());
    all.insert(all.end(), pads.begin(), pads.end());
    all.insert(all.end(), regions.begin(), regions.end());
    if (all.empty()) return {};
    return unionAll(all);
}

geo::Paths GerberComponents::visiblePads() const {
    Paths result;
    for (auto& pg : padGroups) {
        if (pg.visible)
            result.insert(result.end(), pg.paths.begin(), pg.paths.end());
    }
    if (result.empty()) return {};
    return unionAll(result);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Board outline parser (Edge_Cuts)
// ═══════════════════════════════════════════════════════════════════════════════

geo::Path parseBoardOutline(const std::string& filepath) {
    std::string content = readFileContents(filepath);
    ParserState st;

    // Parse just enough to get format spec and extract line/arc segments
    struct Segment {
        Point start, end;
    };
    std::vector<Segment> segments;

    // We need a mini parse to get coordinates from edge cuts
    enum { Normal, Extended } mode = Normal;
    std::string currentCmd;
    std::string extBlock;

    double curX = 0, curY = 0;
    InterpMode imode = InterpMode::Linear;

    for (size_t i = 0; i < content.size(); i++) {
        char c = content[i];

        if (c == '%') {
            if (mode == Normal) {
                mode = Extended;
                extBlock.clear();
            } else {
                processExtendedBlock(st, extBlock);
                mode = Normal;
                extBlock.clear();
            }
            continue;
        }

        if (mode == Extended) { extBlock += c; continue; }
        if (c == '\n' || c == '\r') continue;

        if (c == '*') {
            if (!currentCmd.empty()) {
                DataCmd dc = parseDataCmd(currentCmd);
                if (dc.gCode == 1) imode = InterpMode::Linear;
                if (dc.gCode == 2) imode = InterpMode::CW_Arc;
                if (dc.gCode == 3) imode = InterpMode::CCW_Arc;

                double newX = curX, newY = curY;
                if (dc.hasX) {
                    double raw = parseCoord(dc.xStr, st.xFrac, st.leadingZeroSupp);
                    newX = st.isMetric ? raw : raw * 25.4;
                }
                if (dc.hasY) {
                    double raw = parseCoord(dc.yStr, st.yFrac, st.leadingZeroSupp);
                    newY = st.isMetric ? raw : raw * 25.4;
                }

                if (dc.dCode == 1) {
                    if (imode == InterpMode::Linear) {
                        segments.push_back({Point(curX, curY), Point(newX, newY)});
                    } else {
                        double iOff = 0, jOff = 0;
                        if (dc.hasI) iOff = parseCoord(dc.iStr, st.xFrac, st.leadingZeroSupp);
                        if (dc.hasJ) jOff = parseCoord(dc.jStr, st.yFrac, st.leadingZeroSupp);
                        if (!st.isMetric) { iOff *= 25.4; jOff *= 25.4; }
                        Point ctr(curX + iOff, curY + jOff);
                        bool cw = (imode == InterpMode::CW_Arc);
                        auto arcPts = arcToSegments(Point(curX, curY), Point(newX, newY), ctr, cw);
                        Point prev(curX, curY);
                        for (auto& p : arcPts) {
                            segments.push_back({prev, p});
                            prev = p;
                        }
                    }
                }
                if (dc.hasX || dc.hasY) {
                    curX = newX;
                    curY = newY;
                }
                currentCmd.clear();
            }
            continue;
        }
        currentCmd += c;
    }

    if (segments.empty())
        throw std::runtime_error("No segments found in Edge_Cuts file: " + filepath);

    // Order segments into contour (nearest-end matching)
    static constexpr double EPS = 1e-4;
    Path contour;
    contour.push_back(segments[0].start);
    contour.push_back(segments[0].end);
    std::vector<bool> used(segments.size(), false);
    used[0] = true;

    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < segments.size(); i++) {
            if (used[i]) continue;
            Point last = contour.back();
            Point& s = segments[i].start;
            Point& e = segments[i].end;

            if (std::abs(last.x - s.x) < EPS && std::abs(last.y - s.y) < EPS) {
                contour.push_back(e);
                used[i] = true;
                progress = true;
            } else if (std::abs(last.x - e.x) < EPS && std::abs(last.y - e.y) < EPS) {
                contour.push_back(s);
                used[i] = true;
                progress = true;
            }
        }
    }

    if (contour.size() < 3)
        throw std::runtime_error("Edge_Cuts does not form a valid polygon: " + filepath);

    return contour;
}
