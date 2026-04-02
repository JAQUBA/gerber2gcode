#pragma once
#include <string>
#include <stdexcept>

struct MachineConfig {
    double x_size = 0;              // 0 = no limit
    double y_size = 0;              // 0 = no limit
    // Engraver (V-bit isolation milling)
    double engraver_z_travel = 5.0;       // safe Z for rapid moves
    double engraver_z_cut = -0.05;        // cutting depth Z
    double engraver_tip_width = 0.2;      // V-bit tip diameter (mm)
    // Spindle (drilling)
    double spindle_z_home = 5.0;
    double spindle_z_pre_drill = 1.0;
    double spindle_z_drill = -2.0;
    double spindle_tool_diameter = 0.8;
    // General
    double move_feedrate = 2400;
    // Laser (reserved for future use)
    double laser_z = 0;
    double laser_beam_diameter = 0;
    double x_offset = 0.0;
    double y_offset = 0.0;
};

struct CamConfig {
    double overlap;
    double offset;
};

struct JobConfig {
    double engraver_feedrate;       // cutting feedrate (mm/min)
    double spindle_power;
    double spindle_feedrate;
    // Laser (reserved for future use)
    double laser_power = 0;
    double laser_feedrate = 0;
};

struct Config {
    MachineConfig machine;
    CamConfig cam;
    JobConfig job;
};

Config loadConfig(const std::string& path);
