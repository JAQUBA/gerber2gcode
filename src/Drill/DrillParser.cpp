#include "Drill/DrillParser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <map>
#include <stdexcept>
#include <cstdlib>

std::vector<DrillHole> parseDrill(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open())
        throw std::runtime_error("Cannot open drill file: " + filepath);

    std::vector<DrillHole> holes;
    std::map<std::string, double> tools;
    std::string currentTool;
    std::string fileFunction;
    bool isMetric = true;
    bool inHeader = false;
    bool inRoute  = false;  // M15..M16 rout mode — skip slot moves

    std::regex toolDefRe(R"(T(\d+)C([\d.]+))");
    std::regex toolSelRe(R"(^T(\d+)$)");
    std::regex coordRe(R"(X([-\d.]+)Y([-\d.]+))");

    std::string line;
    while (std::getline(f, line)) {
        // Trim whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        // File function attribute
        if (line.find(";") == 0 && line.find("TF.FileFunction") != std::string::npos) {
            size_t comma = line.find(',');
            if (comma != std::string::npos) {
                fileFunction = line.substr(comma + 1);
                // Trim leading spaces
                size_t start = fileFunction.find_first_not_of(' ');
                if (start != std::string::npos) fileFunction = fileFunction.substr(start);
            }
            continue;
        }
        if (line[0] == ';') continue;

        if (line == "M48") { inHeader = true; continue; }
        if (line == "%")   { inHeader = false; continue; }
        if (line == "M30" || line == "M00") break;

        // Rout mode (slots) — skip moves between M15..M16
        if (line.substr(0, 3) == "M15") { inRoute = true;  continue; }
        if (line.substr(0, 3) == "M16") { inRoute = false; continue; }
        if (inRoute) continue;

        // G85 slotted hole — skip (cannot mill with plunge drill)
        if (line.substr(0, 3) == "G85") continue;

        if (line.find("METRIC") == 0) { isMetric = true; continue; }
        if (line.find("INCH") == 0)   { isMetric = false; continue; }
        if (line.find("G90") == 0 || line.find("G05") == 0 || line.find("FMAT") == 0) continue;

        // Tool definition in header
        std::smatch m;
        if (inHeader && std::regex_match(line, m, toolDefRe)) {
            double d = std::stod(m[2].str());
            if (!isMetric) d *= 25.4;
            tools[m[1].str()] = d;
            continue;
        }

        // Tool selection
        if (std::regex_match(line, m, toolSelRe)) {
            currentTool = m[1].str();
            continue;
        }

        // Drill coordinate
        if (std::regex_search(line, m, coordRe) && !currentTool.empty()) {
            double x = std::stod(m[1].str());
            double y = std::stod(m[2].str());
            if (!isMetric) { x *= 25.4; y *= 25.4; }
            double dia = 0.0;
            auto it = tools.find(currentTool);
            if (it != tools.end()) dia = it->second;
            holes.push_back({x, y, dia, fileFunction});
        }
    }

    return holes;
}
