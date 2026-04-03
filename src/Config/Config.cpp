#include "Config/Config.h"
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>

// ── Minimal JSON parser for flatcum-compatible config ────────────────────────

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static size_t skipWhitespace(const std::string& s, size_t pos) {
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) pos++;
    return pos;
}

static std::string parseString(const std::string& s, size_t& pos) {
    pos = skipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '"')
        throw std::runtime_error("Expected '\"' in JSON");
    pos++;
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) { pos++; }
        result += s[pos++];
    }
    if (pos < s.size()) pos++; // skip closing "
    return result;
}

static double parseNumber(const std::string& s, size_t& pos) {
    pos = skipWhitespace(s, pos);
    size_t start = pos;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
    while (pos < s.size() && (std::isdigit((unsigned char)s[pos])
           || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E'
           || s[pos] == '+' || s[pos] == '-')) pos++;
    if (pos == start)
        throw std::runtime_error("Expected number in JSON");
    return std::stod(s.substr(start, pos - start));
}

using Section = std::map<std::string, double>;

static Section parseObject(const std::string& s, size_t& pos) {
    Section sec;
    pos = skipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '{')
        throw std::runtime_error("Expected '{' in JSON");
    pos++;
    while (true) {
        pos = skipWhitespace(s, pos);
        if (pos >= s.size()) break;
        if (s[pos] == '}') { pos++; break; }
        if (s[pos] == ',') { pos++; continue; }
        std::string key = parseString(s, pos);
        pos = skipWhitespace(s, pos);
        if (pos < s.size() && s[pos] == ':') pos++;
        double val = parseNumber(s, pos);
        sec[key] = val;
    }
    return sec;
}

static double getVal(const Section& sec, const std::string& key, double def = 0.0) {
    auto it = sec.find(key);
    return (it != sec.end()) ? it->second : def;
}

Config loadConfig(const std::string& path) {
    std::string json = readFile(path);
    std::map<std::string, Section> sections;

    size_t pos = skipWhitespace(json, 0);
    if (pos >= json.size() || json[pos] != '{')
        throw std::runtime_error("Invalid JSON config (expected top-level object)");
    pos++;

    while (true) {
        pos = skipWhitespace(json, pos);
        if (pos >= json.size()) break;
        if (json[pos] == '}') break;
        if (json[pos] == ',') { pos++; continue; }

        std::string key = parseString(json, pos);
        pos = skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ':') pos++;
        pos = skipWhitespace(json, pos);

        if (pos < json.size() && json[pos] == '{') {
            sections[key] = parseObject(json, pos);
        } else {
            // skip unknown value
            while (pos < json.size() && json[pos] != ',' && json[pos] != '}') pos++;
        }
    }

    if (sections.find("machine") == sections.end())
        throw std::runtime_error("Config missing 'machine' section");
    if (sections.find("cam") == sections.end())
        throw std::runtime_error("Config missing 'cam' section");
    if (sections.find("job") == sections.end())
        throw std::runtime_error("Config missing 'job' section");

    auto& m = sections["machine"];
    auto& c = sections["cam"];
    auto& j = sections["job"];

    Config cfg;
    cfg.machine.x_size              = getVal(m, "x_size");
    cfg.machine.y_size              = getVal(m, "y_size");
    cfg.machine.laser_z             = getVal(m, "laser_z");
    cfg.machine.spindle_z_home      = getVal(m, "spindle_z_home");
    cfg.machine.spindle_z_pre_drill = getVal(m, "spindle_z_pre_drill");
    cfg.machine.spindle_z_drill     = getVal(m, "spindle_z_drill");
    cfg.machine.move_feedrate       = getVal(m, "move_feedrate");
    cfg.machine.laser_beam_diameter = getVal(m, "laser_beam_diameter");
    cfg.machine.spindle_tool_diameter = getVal(m, "spindle_tool_diameter");
    cfg.machine.x_offset            = getVal(m, "x_offset", 0.0);
    cfg.machine.y_offset            = getVal(m, "y_offset", 0.0);

    cfg.cam.overlap = getVal(c, "overlap");
    cfg.cam.offset  = getVal(c, "offset");

    cfg.job.laser_power      = getVal(j, "laser_power");
    cfg.job.spindle_power    = getVal(j, "spindle_power");
    cfg.job.laser_feedrate   = getVal(j, "laser_feedrate");
    cfg.job.engraver_feedrate = getVal(j, "engraver_feedrate");
    cfg.job.engraver_plunge_feedrate = getVal(j, "engraver_plunge_feedrate", 100.0);
    cfg.job.spindle_feedrate = getVal(j, "spindle_feedrate");

    return cfg;
}
