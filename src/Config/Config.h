#pragma once
#include <string>
#include <stdexcept>

struct MachineConfig {
    double x_size;
    double y_size;
    // Engraver (V-bit isolation milling)
    double engraver_z_travel;       // safe Z for rapid moves (e.g. 5.0)
    double engraver_z_cut;          // cutting depth Z (e.g. -0.05)
    double engraver_tip_width;      // V-bit tip diameter (mm)
    // Spindle (drilling)
    double spindle_z_home;
    double spindle_z_pre_drill;
    double spindle_z_drill;
    double spindle_tool_diameter;
    // General
    double move_feedrate;
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
