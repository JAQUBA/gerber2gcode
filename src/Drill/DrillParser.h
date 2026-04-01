#pragma once
#include <string>
#include <vector>

struct DrillHole {
    double x;
    double y;
    double diameter;
    std::string fileFunction;
};

std::vector<DrillHole> parseDrill(const std::string& filepath);
