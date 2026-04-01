#pragma once
#include "Geometry/Geometry.h"
#include <string>

// Parse a copper layer Gerber file → polygon paths
geo::Paths parseGerber(const std::string& filepath);

// Parse Edge_Cuts Gerber → closed board outline path
geo::Path parseBoardOutline(const std::string& filepath);
