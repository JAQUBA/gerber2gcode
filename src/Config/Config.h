#pragma once
#include <string>
#include <stdexcept>

struct MachineConfig {
    double x_size;
    double y_size;
    double laser_z;
    double spindle_z_home;
    double spindle_z_pre_drill;
    double spindle_z_drill;
    double move_feedrate;
    double laser_beam_diameter;
    double spindle_tool_diameter;
    double x_offset = 0.0;
    double y_offset = 0.0;
};

struct CamConfig {
    double overlap;
    double offset;
};

struct JobConfig {
    double laser_power;
    double spindle_power;
    double laser_feedrate;
    double spindle_feedrate;
};

struct Config {
    MachineConfig machine;
    CamConfig cam;
    JobConfig job;
};

Config loadConfig(const std::string& path);
