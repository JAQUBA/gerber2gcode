#pragma once
#include "Geometry/Geometry.h"
#include <string>

// ── Categorized Gerber parse result ──────────────────────────────────────────

struct GerberComponents {
    geo::Paths traces;      // D01 draws (buffered lines/arcs)
    geo::Paths pads;        // D03 flashes (aperture shapes)
    geo::Paths regions;     // G36/G37 filled polygons

    // Combined result (traces ∪ pads ∪ regions − clear polarity)
    geo::Paths combined() const;
};

// Parse a copper layer Gerber file → categorized components
GerberComponents parseGerberComponents(const std::string& filepath);

// Parse a copper layer Gerber file → merged polygon paths (backward compat)
geo::Paths parseGerber(const std::string& filepath);

// Parse Edge_Cuts Gerber → closed board outline path
geo::Path parseBoardOutline(const std::string& filepath);
