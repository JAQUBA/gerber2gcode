#pragma once
#include "Geometry/Geometry.h"
#include <string>
#include <vector>

// ── Pad group — pads sharing the same aperture ──────────────────────────────

struct PadGroup {
    std::string name;       // e.g. "Circle Ø0.800mm", "Rect 1.27×0.64mm"
    int         apNum;      // aperture D-code number
    geo::Paths  paths;      // geometry for this group
    int         count;      // number of flashes
    bool        visible = true;  // UI toggle (like DrillFilter)
    bool        isCircular = false;  // true for Circle/Obround apertures (arc-eligible)
    double      apertureRadius = 0;  // outer radius for circular apertures
    std::vector<geo::Point> centers;  // D03 flash positions (pad centers)
};

// ── Categorized Gerber parse result ──────────────────────────────────────────

struct GerberComponents {
    geo::Paths traces;      // D01 draws (buffered lines/arcs)
    geo::Paths pads;        // D03 flashes (aperture shapes) — all combined
    geo::Paths regions;     // G36/G37 filled polygons

    std::vector<PadGroup> padGroups;  // pads grouped by aperture

    // Combined result (traces ∪ pads ∪ regions − clear polarity)
    geo::Paths combined() const;

    // Combined pads from visible groups only
    geo::Paths visiblePads() const;
};

// Parse a copper layer Gerber file → categorized components
GerberComponents parseGerberComponents(const std::string& filepath);

// Parse a copper layer Gerber file → merged polygon paths (backward compat)
geo::Paths parseGerber(const std::string& filepath);

// Parse Edge_Cuts Gerber → closed board outline path
geo::Path parseBoardOutline(const std::string& filepath);
