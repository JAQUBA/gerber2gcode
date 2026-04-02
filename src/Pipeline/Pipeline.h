#pragma once
#include "Config/Config.h"
#include "Geometry/Geometry.h"
#include "Toolpath/Toolpath.h"
#include "Drill/DrillParser.h"
#include <string>
#include <vector>
#include <functional>
#include <set>

using LogCallback = std::function<void(const std::string&)>;

struct PipelineParams {
    Config config;
    std::string kicadDir;
    std::string outputPath;
    double xOffset  = 0.0;
    double yOffset  = 0.0;
    bool flip       = false;
    bool ignoreVia  = false;
    std::string debugPath;
    // Generation flags (derived from layer visibility)
    bool generateIsolation = true;
    bool generateDrilling  = true;
    bool generateCutout    = false;
    // Drill diameter filter — diameters listed here are excluded from G-Code
    // Key format: "%.3f" of diameter in mm (e.g. "0.800", "3.200")
    std::set<std::string> disabledDrillDiameters;
};

struct KicadFiles {
    std::string edgeCuts;
    std::string copperTop;          // F_Cu
    std::string copperBottom;       // B_Cu
    std::string maskTop;            // F_Mask
    std::string maskBottom;         // B_Mask
    std::string silkTop;            // F_Silkscreen
    std::string silkBottom;         // B_Silkscreen
    std::string pasteTop;           // F_Paste
    std::string pasteBottom;        // B_Paste
    std::vector<std::string> drillsPTH;
    std::vector<std::string> drillsNPTH;
    // Computed: active copper path (copperTop or copperBottom, depending on flip)
    std::string activeCopperPath() const { return !copperTop.empty() ? copperTop : copperBottom; }
    bool isBack() const { return copperTop.empty() && !copperBottom.empty(); }
};

// Intermediate data for canvas preview
struct PipelineResult {
    // Board
    geo::Path   outline;
    double      boardW = 0, boardH = 0;

    // Input layers (parsed from Gerber files)
    geo::Paths  copperTop;
    geo::Paths  copperBottom;
    geo::Paths  maskTop;
    geo::Paths  maskBottom;
    geo::Paths  silkTop;
    geo::Paths  silkBottom;
    geo::Paths  pasteTop;
    geo::Paths  pasteBottom;

    // Drills (separate PTH/NPTH)
    std::vector<DrillHole>  drillsPTH;
    std::vector<DrillHole>  drillsNPTH;

    // Generated layers
    geo::Paths                      clearance;
    std::vector<ToolpathContour>    contours;       // isolation
    // geo::Path cutoutPath;  // future: cutout

    // Output
    std::string gcode;
    bool        valid = false;

    // Helpers — combined holes for backward compat
    std::vector<DrillHole> allDrills() const {
        std::vector<DrillHole> all;
        all.insert(all.end(), drillsPTH.begin(), drillsPTH.end());
        all.insert(all.end(), drillsNPTH.begin(), drillsNPTH.end());
        return all;
    }

    // Active copper (the layer being machined, after flip)
    const geo::Paths& activeCopperRef() const {
        return !copperTop.empty() ? copperTop : copperBottom;
    }
};

KicadFiles detectKicadFiles(const std::string& directory);

// Full pipeline: parse + generate + write
bool runPipeline(const PipelineParams& params, LogCallback log,
                 PipelineResult* resultOut = nullptr);

// Parse-only: parse files and return intermediate data for preview
PipelineResult parsePipelineData(const PipelineParams& params, LogCallback log);
