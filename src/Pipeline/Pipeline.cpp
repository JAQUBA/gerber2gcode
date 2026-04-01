#include "Pipeline/Pipeline.h"
#include "Config/Config.h"
#include "Gerber/GerberParser.h"
#include "Drill/DrillParser.h"
#include "Geometry/Geometry.h"
#include "Toolpath/Toolpath.h"
#include "GCode/GCodeGen.h"
#include "Debug/DebugImage.h"

#include <windows.h>
#include <Util/StringUtils.h>

#include <fstream>
#include <algorithm>
#include <cmath>
#include <sstream>

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string baseName(const std::string& path) {
    size_t sep = path.find_last_of("\\/");
    return (sep != std::string::npos) ? path.substr(sep + 1) : path;
}

// ── KiCad file auto-detection ────────────────────────────────────────────────

KicadFiles detectKicadFiles(const std::string& directory) {
    KicadFiles result;
    std::string fCu, bCu;

    std::wstring pattern = StringUtils::utf8ToWide(directory + "\\*");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open directory: " + directory);

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = StringUtils::wideToUtf8(fd.cFileName);
        std::string fullPath = directory + "\\" + name;

        if (endsWith(name, "-Edge_Cuts.gbr"))      result.edgeCuts = fullPath;
        else if (endsWith(name, "-F_Cu.gbr"))       fCu = fullPath;
        else if (endsWith(name, "-B_Cu.gbr"))       bCu = fullPath;
        else if (endsWith(name, "-PTH.drl"))        result.drills.push_back(fullPath);
        else if (endsWith(name, "-NPTH.drl"))       result.drills.push_back(fullPath);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (result.edgeCuts.empty())
        throw std::runtime_error("No Edge_Cuts.gbr file found in " + directory);

    if (!fCu.empty()) {
        result.copper = fCu;
        result.isBack = false;
    } else if (!bCu.empty()) {
        result.copper = bCu;
        result.isBack = true;
    } else {
        throw std::runtime_error("No F_Cu.gbr or B_Cu.gbr found in " + directory);
    }

    return result;
}

// ── Main pipeline ────────────────────────────────────────────────────────────

bool runPipeline(const PipelineParams& params, LogCallback log) {
    try {
        // Load config
        log("Loading config: " + baseName(params.configPath));
        Config config = loadConfig(params.configPath);

        // Detect KiCad files
        log("Scanning directory: " + baseName(params.kicadDir));
        KicadFiles kf = detectKicadFiles(params.kicadDir);

        log("  Edge_Cuts: " + baseName(kf.edgeCuts));
        log("  Copper:    " + baseName(kf.copper) + (kf.isBack ? " (B_Cu)" : " (F_Cu)"));
        for (auto& d : kf.drills)
            log("  Drill:     " + baseName(d));

        double xOff = params.xOffset;
        double yOff = params.yOffset;
        if (xOff == 0 && yOff == 0) {
            xOff = config.machine.x_offset;
            yOff = config.machine.y_offset;
        }

        // Parse board outline
        log("\xF0\x9F\x94\x8D Parsing Edge_Cuts...");
        geo::Path outline = parseBoardOutline(kf.edgeCuts);

        // Compute board dimensions from outline bounds
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for (auto& pt : outline) {
            minX = std::min(minX, pt.x); minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x); maxY = std::max(maxY, pt.y);
        }
        double boardW = maxX - minX;
        double boardH = maxY - minY;
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "\xF0\x9F\x94\x8D Board size: %.1f x %.1f mm", boardW, boardH);
            log(buf);
        }

        if (boardW + xOff > config.machine.x_size || boardH + yOff > config.machine.y_size) {
            char buf[256];
            _snprintf(buf, sizeof(buf),
                "Board (%.1fx%.1fmm) + offset (%.1f,%.1f) exceeds machine size (%.0fx%.0fmm)",
                boardW, boardH, xOff, yOff, config.machine.x_size, config.machine.y_size);
            throw std::runtime_error(buf);
        }

        // Parse copper
        log("\xF0\x9F\x94\x8D Parsing " + baseName(kf.copper) + "...");
        geo::Paths copper = parseGerber(kf.copper);
        log("  " + std::to_string(copper.size()) + " copper regions parsed");

        // Parse drills
        std::vector<DrillHole> allHoles;
        for (auto& drlPath : kf.drills) {
            auto holes = parseDrill(drlPath);
            {
                char buf[128];
                _snprintf(buf, sizeof(buf), "\xF0\x9F\x94\x8D Parsing %s... %d holes",
                         baseName(drlPath).c_str(), (int)holes.size());
                log(buf);
            }
            allHoles.insert(allHoles.end(), holes.begin(), holes.end());
        }

        // Filter vias
        if (params.ignoreVia) {
            size_t before = allHoles.size();
            allHoles.erase(
                std::remove_if(allHoles.begin(), allHoles.end(),
                    [](const DrillHole& h) { return h.fileFunction.find("ViaDrill") != std::string::npos; }),
                allHoles.end());
            size_t filtered = before - allHoles.size();
            if (filtered > 0)
                log("  Filtered " + std::to_string(filtered) + " via holes");
        }

        // Warn about undersized holes
        double toolD = config.machine.spindle_tool_diameter;
        int undersized = 0;
        for (auto& h : allHoles)
            if (h.diameter < toolD) undersized++;
        if (undersized > 0) {
            char buf[128];
            _snprintf(buf, sizeof(buf),
                "\xE2\x9A\xA0 %d hole(s) have diameter smaller than tool (%.1fmm)",
                undersized, toolD);
            log(buf);
        }

        // Flip for B_Cu
        bool doFlip = params.flip || kf.isBack;
        if (doFlip) {
            log("  Flipping board (B_Cu)");
            copper = geo::flipX(copper, boardW);
            for (auto& h : allHoles)
                h.x = boardW - h.x;
        }

        // Clip copper to board outline and compute clearance
        geo::Paths outlinePaths = {outline};
        copper = geo::intersect(copper, outlinePaths);

        if (geo::isEmpty(copper))
            throw std::runtime_error("No copper geometry within board outline after clipping");

        geo::Paths clearance = geo::difference(outlinePaths, copper);

        // Generate toolpaths
        log("\xF0\x9F\x94\xA5 Generating laser toolpath...");
        auto contours = generateLaserToolpath(clearance, config);
        {
            char buf[64];
            _snprintf(buf, sizeof(buf), "\xF0\x9F\x94\xA5 %d contours generated", (int)contours.size());
            log(buf);
        }

        if (!allHoles.empty()) {
            char buf[64];
            _snprintf(buf, sizeof(buf), "\xF0\x9F\x94\xA9 Optimizing drill order... %d holes", (int)allHoles.size());
            log(buf);
            allHoles = orderDrillHoles(allHoles);
        }

        // Generate GCode
        std::string gcode = generateGCode(contours, allHoles, config, xOff, yOff);
        int nLines = 0;
        for (char c : gcode) if (c == '\n') nLines++;
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "\xF0\x9F\x93\x9D Writing %s... %d lines",
                     baseName(params.outputPath).c_str(), nLines);
            log(buf);
        }

        std::ofstream outFile(params.outputPath);
        if (!outFile.is_open())
            throw std::runtime_error("Cannot write output file: " + params.outputPath);
        outFile << gcode;
        outFile.close();

        // Time estimate
        double est = estimateJobTime(contours, allHoles, config);
        int hours = (int)(est / 3600);
        int mins  = (int)((int)est % 3600) / 60;
        int secs  = (int)est % 60;
        {
            char buf[64];
            if (hours > 0)
                _snprintf(buf, sizeof(buf), "\xE2\x8F\xB1 Estimated job time: %dh %dm %ds", hours, mins, secs);
            else
                _snprintf(buf, sizeof(buf), "\xE2\x8F\xB1 Estimated job time: %dm %ds", mins, secs);
            log(buf);
        }

        // Debug image
        if (!params.debugPath.empty()) {
            log("\xF0\x9F\x96\xBC Generating debug image...");
            generateDebugBMP(params.outputPath, params.debugPath, config, allHoles);
            log("\xF0\x9F\x96\xBC Saved " + baseName(params.debugPath));
        }

        log("\xE2\x9C\x85 Done!");
        return true;

    } catch (const std::exception& e) {
        log(std::string("\xE2\x9D\x8C Error: ") + e.what());
        return false;
    }
}
