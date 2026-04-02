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

    std::wstring pattern = StringUtils::utf8ToWide(directory + "\\*");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open directory: " + directory);

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = StringUtils::wideToUtf8(fd.cFileName);
        std::string fullPath = directory + "\\" + name;

        if      (endsWith(name, "-Edge_Cuts.gbr"))    result.edgeCuts      = fullPath;
        else if (endsWith(name, "-F_Cu.gbr"))         result.copperTop     = fullPath;
        else if (endsWith(name, "-B_Cu.gbr"))         result.copperBottom  = fullPath;
        else if (endsWith(name, "-F_Mask.gbr"))       result.maskTop       = fullPath;
        else if (endsWith(name, "-B_Mask.gbr"))       result.maskBottom    = fullPath;
        else if (endsWith(name, "-F_Silkscreen.gbr")) result.silkTop       = fullPath;
        else if (endsWith(name, "-B_Silkscreen.gbr")) result.silkBottom    = fullPath;
        else if (endsWith(name, "-F_Paste.gbr"))      result.pasteTop      = fullPath;
        else if (endsWith(name, "-B_Paste.gbr"))      result.pasteBottom   = fullPath;
        else if (endsWith(name, "-PTH.drl"))          result.drillsPTH.push_back(fullPath);
        else if (endsWith(name, "-NPTH.drl"))         result.drillsNPTH.push_back(fullPath);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (result.edgeCuts.empty())
        throw std::runtime_error("No Edge_Cuts.gbr file found in " + directory);

    if (result.copperTop.empty() && result.copperBottom.empty())
        throw std::runtime_error("No F_Cu.gbr or B_Cu.gbr found in " + directory);

    return result;
}

// ── Main pipeline ────────────────────────────────────────────────────────────

// ── Helper: parse optional Gerber layer, normalize coordinates ───────────────

static geo::Paths parseOptionalLayer(const std::string& filepath,
                                      double dx, double dy, LogCallback log) {
    if (filepath.empty()) return {};
    log("Parsing " + baseName(filepath) + "...");
    geo::Paths p = parseGerber(filepath);
    p = geo::translateAll(p, dx, dy);
    log("  " + std::to_string(p.size()) + " regions");
    return p;
}

// ── Helper: parse drill files (PTH or NPTH) ─────────────────────────────────

static std::vector<DrillHole> parseDrillFiles(const std::vector<std::string>& files,
                                               double dx, double dy,
                                               bool ignoreVia, LogCallback log) {
    std::vector<DrillHole> allHoles;
    for (auto& drlPath : files) {
        auto holes = parseDrill(drlPath);
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "Parsing %s... %d holes",
                     baseName(drlPath).c_str(), (int)holes.size());
            log(buf);
        }
        for (auto& h : holes) { h.x += dx; h.y += dy; }
        allHoles.insert(allHoles.end(), holes.begin(), holes.end());
    }
    if (ignoreVia) {
        allHoles.erase(
            std::remove_if(allHoles.begin(), allHoles.end(),
                [](const DrillHole& h) { return h.fileFunction.find("ViaDrill") != std::string::npos; }),
            allHoles.end());
    }
    return allHoles;
}

// ── Main pipeline ────────────────────────────────────────────────────────────

bool runPipeline(const PipelineParams& params, LogCallback log,
                 PipelineResult* resultOut) {
    try {
        Config config = params.config;

        log("Scanning directory: " + baseName(params.kicadDir));
        KicadFiles kf = detectKicadFiles(params.kicadDir);

        // Log detected files
        log("  Edge_Cuts: " + baseName(kf.edgeCuts));
        if (!kf.copperTop.empty())     log("  F_Cu:      " + baseName(kf.copperTop));
        if (!kf.copperBottom.empty())  log("  B_Cu:      " + baseName(kf.copperBottom));
        if (!kf.maskTop.empty())       log("  F_Mask:    " + baseName(kf.maskTop));
        if (!kf.maskBottom.empty())    log("  B_Mask:    " + baseName(kf.maskBottom));
        if (!kf.silkTop.empty())       log("  F_Silk:    " + baseName(kf.silkTop));
        if (!kf.silkBottom.empty())    log("  B_Silk:    " + baseName(kf.silkBottom));
        for (auto& d : kf.drillsPTH)  log("  PTH:       " + baseName(d));
        for (auto& d : kf.drillsNPTH) log("  NPTH:      " + baseName(d));

        double xOff = params.xOffset;
        double yOff = params.yOffset;
        if (xOff == 0 && yOff == 0) {
            xOff = config.machine.x_offset;
            yOff = config.machine.y_offset;
        }

        // Parse board outline
        log("Parsing Edge_Cuts...");
        geo::Path outline = parseBoardOutline(kf.edgeCuts);

        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for (auto& pt : outline) {
            minX = std::min(minX, pt.x); minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x); maxY = std::max(maxY, pt.y);
        }
        double boardW = maxX - minX;
        double boardH = maxY - minY;
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "Board size: %.2f x %.2f mm", boardW, boardH);
            log(buf);
        }

        outline = geo::translate(outline, -minX, -minY);
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "Board origin normalized from (%.2f, %.2f) to (0, 0)", minX, minY);
            log(buf);
        }

        // Machine size check
        if (config.machine.x_size > 0 && config.machine.y_size > 0) {
            if (boardW + xOff > config.machine.x_size || boardH + yOff > config.machine.y_size) {
                char buf[256];
                _snprintf(buf, sizeof(buf),
                    "Board (%.1fx%.1fmm) + offset (%.1f,%.1f) exceeds machine size (%.0fx%.0fmm)",
                    boardW, boardH, xOff, yOff, config.machine.x_size, config.machine.y_size);
                throw std::runtime_error(buf);
            }
        }

        // Parse all layers
        double dx = -minX, dy = -minY;
        PipelineResult result;
        result.outline = outline;
        result.boardW  = boardW;
        result.boardH  = boardH;

        // Copper layers
        result.copperTop    = parseOptionalLayer(kf.copperTop,    dx, dy, log);
        result.copperBottom = parseOptionalLayer(kf.copperBottom, dx, dy, log);

        // Reference layers
        result.maskTop      = parseOptionalLayer(kf.maskTop,      dx, dy, log);
        result.maskBottom   = parseOptionalLayer(kf.maskBottom,   dx, dy, log);
        result.silkTop      = parseOptionalLayer(kf.silkTop,      dx, dy, log);
        result.silkBottom   = parseOptionalLayer(kf.silkBottom,   dx, dy, log);
        result.pasteTop     = parseOptionalLayer(kf.pasteTop,     dx, dy, log);
        result.pasteBottom  = parseOptionalLayer(kf.pasteBottom,  dx, dy, log);

        // Drills
        result.drillsPTH  = parseDrillFiles(kf.drillsPTH,  dx, dy, params.ignoreVia, log);
        result.drillsNPTH = parseDrillFiles(kf.drillsNPTH, dx, dy, false, log);

        // Determine active copper (for isolation/clearance)
        bool doFlip = params.flip || kf.isBack();
        geo::Paths& activeCu = doFlip ? result.copperBottom : result.copperTop;

        if (doFlip) {
            log("Flipping board (B_Cu)");
            activeCu = geo::flipX(activeCu, boardW);
            for (auto& h : result.drillsPTH)  h.x = boardW - h.x;
            for (auto& h : result.drillsNPTH) h.x = boardW - h.x;
        }

        // Clip active copper and compute clearance
        geo::Paths outlinePaths = {outline};
        activeCu = geo::intersect(activeCu, outlinePaths);

        if (geo::isEmpty(activeCu))
            throw std::runtime_error("No copper geometry within board outline after clipping");

        result.clearance = geo::difference(outlinePaths, activeCu);

        // Generate isolation toolpaths
        std::vector<ToolpathContour> contours;
        if (params.generateIsolation) {
            log("Generating engraver toolpath...");
            contours = generateToolpath(result.clearance, config);
            {
                char buf[64];
                _snprintf(buf, sizeof(buf), "%d contours generated", (int)contours.size());
                log(buf);
            }
            result.contours = contours;
        } else {
            log("Isolation: skipped (disabled)");
        }

        // Collect holes for GCode generation
        std::vector<DrillHole> allHoles;
        if (params.generateDrilling) {
            allHoles = result.allDrills();

            // Filter out disabled drill diameters
            if (!params.disabledDrillDiameters.empty()) {
                int beforeCount = (int)allHoles.size();
                allHoles.erase(
                    std::remove_if(allHoles.begin(), allHoles.end(),
                        [&](const DrillHole& h) {
                            char key[16];
                            _snprintf(key, sizeof(key), "%.3f", h.diameter);
                            return params.disabledDrillDiameters.count(key) > 0;
                        }),
                    allHoles.end());
                int removed = beforeCount - (int)allHoles.size();
                if (removed > 0) {
                    char buf[80];
                    _snprintf(buf, sizeof(buf), "Filtered out %d holes (disabled diameters)", removed);
                    log(buf);
                }
            }

            // Warn about undersized holes
            double toolD = config.machine.spindle_tool_diameter;
            int undersized = 0;
            for (auto& h : allHoles)
                if (h.diameter < toolD) undersized++;
            if (undersized > 0) {
                char buf[128];
                _snprintf(buf, sizeof(buf),
                    "Warning: %d hole(s) smaller than drill tool (%.1fmm)",
                    undersized, toolD);
                log(buf);
            }

            if (!allHoles.empty()) {
                char buf[64];
                _snprintf(buf, sizeof(buf), "Optimizing drill order... %d holes", (int)allHoles.size());
                log(buf);
                allHoles = orderDrillHoles(allHoles);
            }
        } else {
            log("Drilling: skipped (disabled)");
        }

        // Generate GCode
        std::vector<ToolpathContour> gContours = params.generateIsolation ? contours : std::vector<ToolpathContour>{};

        std::string gcode = generateGCode(gContours, allHoles, config, xOff, yOff);
        result.gcode = gcode;
        result.valid = true;
        int nLines = 0;
        for (char c : gcode) if (c == '\n') nLines++;
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "Writing %s... %d lines",
                     baseName(params.outputPath).c_str(), nLines);
            log(buf);
        }

        std::ofstream outFile(params.outputPath);
        if (!outFile.is_open())
            throw std::runtime_error("Cannot write output file: " + params.outputPath);
        outFile << gcode;
        outFile.close();

        // Time estimate
        double est = estimateJobTime(gContours, allHoles, config);
        int hours = (int)(est / 3600);
        int mins  = (int)((int)est % 3600) / 60;
        int secs  = (int)est % 60;
        {
            char buf[64];
            if (hours > 0)
                _snprintf(buf, sizeof(buf), "Estimated job time: %dh %dm %ds", hours, mins, secs);
            else
                _snprintf(buf, sizeof(buf), "Estimated job time: %dm %ds", mins, secs);
            log(buf);
        }

        // Debug image
        if (!params.debugPath.empty()) {
            log("Generating debug image...");
            generateDebugBMP(params.outputPath, params.debugPath, config, allHoles);
            log("Saved " + baseName(params.debugPath));
        }

        if (resultOut)
            *resultOut = std::move(result);

        log("Done!");
        return true;

    } catch (const std::exception& e) {
        log(std::string("Error: ") + e.what());
        return false;
    }
}

// ── Parse-only pipeline for immediate preview ────────────────────────────────

PipelineResult parsePipelineData(const PipelineParams& params, LogCallback log) {
    PipelineResult result;
    try {
        log("Scanning directory: " + baseName(params.kicadDir));
        KicadFiles kf = detectKicadFiles(params.kicadDir);

        // Log detected files
        log("  Edge_Cuts: " + baseName(kf.edgeCuts));
        std::string activeName = !kf.copperTop.empty() ? baseName(kf.copperTop) : baseName(kf.copperBottom);
        log("  Active Cu: " + activeName + (kf.isBack() ? " (B_Cu)" : " (F_Cu)"));

        // Parse board outline
        log("Parsing Edge_Cuts...");
        geo::Path outline = parseBoardOutline(kf.edgeCuts);

        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for (auto& pt : outline) {
            minX = std::min(minX, pt.x); minY = std::min(minY, pt.y);
            maxX = std::max(maxX, pt.x); maxY = std::max(maxY, pt.y);
        }
        double boardW = maxX - minX;
        double boardH = maxY - minY;
        outline = geo::translate(outline, -minX, -minY);

        double dx = -minX, dy = -minY;
        result.outline = outline;
        result.boardW  = boardW;
        result.boardH  = boardH;

        // Parse all available layers
        result.copperTop    = parseOptionalLayer(kf.copperTop,    dx, dy, log);
        result.copperBottom = parseOptionalLayer(kf.copperBottom, dx, dy, log);
        result.maskTop      = parseOptionalLayer(kf.maskTop,      dx, dy, log);
        result.maskBottom   = parseOptionalLayer(kf.maskBottom,   dx, dy, log);
        result.silkTop      = parseOptionalLayer(kf.silkTop,      dx, dy, log);
        result.silkBottom   = parseOptionalLayer(kf.silkBottom,   dx, dy, log);
        result.pasteTop     = parseOptionalLayer(kf.pasteTop,     dx, dy, log);
        result.pasteBottom  = parseOptionalLayer(kf.pasteBottom,  dx, dy, log);

        // Parse drills (separate PTH/NPTH)
        result.drillsPTH  = parseDrillFiles(kf.drillsPTH,  dx, dy, params.ignoreVia, log);
        result.drillsNPTH = parseDrillFiles(kf.drillsNPTH, dx, dy, false, log);

        // Flip if needed
        bool doFlip = params.flip || kf.isBack();
        geo::Paths& activeCu = doFlip ? result.copperBottom : result.copperTop;
        if (doFlip) {
            activeCu = geo::flipX(activeCu, boardW);
            for (auto& h : result.drillsPTH)  h.x = boardW - h.x;
            for (auto& h : result.drillsNPTH) h.x = boardW - h.x;
        }

        // Clip and clearance
        geo::Paths outlinePaths = {outline};
        activeCu = geo::intersect(activeCu, outlinePaths);
        result.clearance = geo::difference(outlinePaths, activeCu);

        result.valid = true;

        int totalHoles = (int)(result.drillsPTH.size() + result.drillsNPTH.size());
        int totalLayers = (!result.copperTop.empty()) + (!result.copperBottom.empty()) +
                          (!result.maskTop.empty()) + (!result.maskBottom.empty()) +
                          (!result.silkTop.empty()) + (!result.silkBottom.empty()) +
                          (!result.pasteTop.empty()) + (!result.pasteBottom.empty());
        {
            char buf[128];
            _snprintf(buf, sizeof(buf), "Board: %.1fx%.1fmm, %d layers, %d PTH + %d NPTH holes",
                     boardW, boardH, totalLayers, (int)result.drillsPTH.size(), (int)result.drillsNPTH.size());
            log(buf);
        }

    } catch (const std::exception& e) {
        log(std::string("Error: ") + e.what());
    }
    return result;
}
