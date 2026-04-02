// ============================================================================
// AppState.cpp — Global state, settings, tool presets, actions
// ============================================================================

#include "AppState.h"
#include "AppUI.h"
#include "Canvas/PCBCanvas.h"
#include "Pipeline/Pipeline.h"

#include <Core.h>
#include <Util/StringUtils.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <algorithm>

// ════════════════════════════════════════════════════════════════════════════
// Global State — definitions
// ════════════════════════════════════════════════════════════════════════════

SimpleWindow*  g_window       = nullptr;
ConfigManager* g_settings     = nullptr;
ConfigManager* g_toolConfig   = nullptr;
PCBCanvas*     g_canvas       = nullptr;
TextArea*      g_logArea      = nullptr;
ProgressBar*   g_progressBar  = nullptr;

std::vector<ToolPreset> g_toolPresets;
int                     g_activeToolIndex = 0;

InputField*    g_fldKicadDir   = nullptr;
InputField*    g_fldOutputFile = nullptr;
InputField*    g_fldToolDia    = nullptr;
InputField*    g_fldCutDepth   = nullptr;
InputField*    g_fldSafeHeight = nullptr;
InputField*    g_fldFeedXY     = nullptr;
InputField*    g_fldFeedZ      = nullptr;
InputField*    g_fldOverlap    = nullptr;
InputField*    g_fldOffset     = nullptr;
InputField*    g_fldXOffset    = nullptr;
InputField*    g_fldYOffset    = nullptr;
InputField*    g_fldMaterial   = nullptr;
InputField*    g_fldZDrill     = nullptr;
InputField*    g_fldDrillDia   = nullptr;
InputField*    g_fldDrillFeed  = nullptr;

CheckBox*      g_chkFlip       = nullptr;
CheckBox*      g_chkIgnoreVia  = nullptr;
CheckBox*      g_chkDebug      = nullptr;
CheckBox*      g_chkFluidNC    = nullptr;
CheckBox*      g_chkEngraverSpindle = nullptr;

InputField*    g_fldDrillDwell = nullptr;

Button*        g_btnTool       = nullptr;
Button*        g_btnGenerate   = nullptr;

PipelineResult g_pipelineData;
volatile bool  g_isRunning     = false;
std::string    g_lastDebugPath;

HWND           g_hLayerPanel   = nullptr;
std::vector<LayerPanelItem> g_layerItems;

// ════════════════════════════════════════════════════════════════════════════
// Auto-refresh debounce
// ════════════════════════════════════════════════════════════════════════════

static const UINT_PTR TIMER_AUTO_REFRESH = 9601;
static const DWORD    AUTO_REFRESH_DELAY = 400;   // ms debounce
bool                  g_needsReparse     = false;

// ════════════════════════════════════════════════════════════════════════════
// String helpers
// ════════════════════════════════════════════════════════════════════════════

static std::string dblStr(double val, int dec = 2) {
    char buf[32];
    if (dec == 1) std::snprintf(buf, sizeof(buf), "%.1f", val);
    else if (dec == 3) std::snprintf(buf, sizeof(buf), "%.3f", val);
    else std::snprintf(buf, sizeof(buf), "%.2f", val);
    return buf;
}

static std::string intStr(int val) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", val);
    return buf;
}

static double parseD(const std::string& s) {
    if (s.empty()) return 0.0;
    std::string tmp = s;
    std::replace(tmp.begin(), tmp.end(), ',', '.');
    return std::strtod(tmp.c_str(), nullptr);
}

static std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char ch) { return (char)std::tolower(ch); });
    return s;
}

static double defaultSpindleDiameterForPreset(const std::string& name, double toolDiameter) {
    std::string low = lowerAscii(name);
    if (low.find("drill") != std::string::npos) return toolDiameter;
    if (low.find("end mill") != std::string::npos) return toolDiameter;
    if (low.find("cutout") != std::string::npos) return toolDiameter;
    return 0.8;
}

static ToolPresetKind inferPresetKindFromName(const std::string& name) {
    std::string low = lowerAscii(name);
    if (low.find("drill") != std::string::npos) return ToolPresetKind::Drill;
    if (low.find("cutout") != std::string::npos) return ToolPresetKind::Cutout;
    if (low.find("v-bit") != std::string::npos || low.find("engrav") != std::string::npos)
        return ToolPresetKind::Isolation;
    if (low.find("end mill") != std::string::npos) return ToolPresetKind::Combo;
    return ToolPresetKind::Isolation;
}

static std::string presetKindToKey(ToolPresetKind kind) {
    switch (kind) {
        case ToolPresetKind::Isolation: return "isolation";
        case ToolPresetKind::Combo:     return "combo";
        case ToolPresetKind::Drill:     return "drill";
        case ToolPresetKind::Cutout:    return "cutout";
    }
    return "isolation";
}

static ToolPresetKind presetKindFromKey(const std::string& key) {
    std::string low = lowerAscii(key);
    if (low == "combo")  return ToolPresetKind::Combo;
    if (low == "drill")  return ToolPresetKind::Drill;
    if (low == "cutout") return ToolPresetKind::Cutout;
    return ToolPresetKind::Isolation;
}

static std::wstring presetKindTag(ToolPresetKind kind) {
    switch (kind) {
        case ToolPresetKind::Isolation: return L"ISO";
        case ToolPresetKind::Combo:     return L"COMBO";
        case ToolPresetKind::Drill:     return L"DRILL";
        case ToolPresetKind::Cutout:    return L"CUTOUT";
    }
    return L"ISO";
}

static std::wstring presetKindHeader(ToolPresetKind kind) {
    switch (kind) {
        case ToolPresetKind::Isolation: return L"Isolation";
        case ToolPresetKind::Combo:     return L"Combo (Isolation + Drill)";
        case ToolPresetKind::Drill:     return L"Drilling";
        case ToolPresetKind::Cutout:    return L"Cutout";
    }
    return L"Isolation";
}

static double defaultOverlapForKind(ToolPresetKind kind) {
    switch (kind) {
        case ToolPresetKind::Isolation: return 0.40;
        case ToolPresetKind::Combo:     return 0.30;
        case ToolPresetKind::Drill:
        case ToolPresetKind::Cutout:    return 0.00;
    }
    return 0.40;
}

static double defaultOffsetForKind(ToolPresetKind kind) {
    switch (kind) {
        case ToolPresetKind::Isolation:
        case ToolPresetKind::Combo:     return 0.02;
        case ToolPresetKind::Drill:
        case ToolPresetKind::Cutout:    return 0.00;
    }
    return 0.02;
}

static void applyPresetJobMode(ToolPresetKind kind) {
    if (!g_canvas) return;
    auto& lay = g_canvas->layers();

    switch (kind) {
        case ToolPresetKind::Isolation:
            lay.isolation = true;
            lay.drillsPTH = false;
            lay.drillsNPTH = false;
            lay.cutout = false;
            break;
        case ToolPresetKind::Combo:
            lay.isolation = true;
            lay.drillsPTH = true;
            lay.drillsNPTH = true;
            lay.cutout = false;
            break;
        case ToolPresetKind::Drill:
            lay.isolation = false;
            lay.drillsPTH = true;
            lay.drillsNPTH = true;
            lay.cutout = false;
            break;
        case ToolPresetKind::Cutout:
            lay.isolation = false;
            lay.drillsPTH = false;
            lay.drillsNPTH = false;
            lay.cutout = true;
            break;
    }
}

static void applyPresetManagedValues(const ToolPreset& tp) {
    if (g_fldOverlap)   g_fldOverlap->setText(dblStr(tp.overlap, 2).c_str());
    if (g_fldOffset)    g_fldOffset->setText(dblStr(tp.offset, 2).c_str());
    if (g_fldXOffset)   g_fldXOffset->setText(dblStr(tp.xOffset, 2).c_str());
    if (g_fldYOffset)   g_fldYOffset->setText(dblStr(tp.yOffset, 2).c_str());
    if (g_chkFlip)      g_chkFlip->setChecked(tp.flip);
    if (g_chkIgnoreVia) g_chkIgnoreVia->setChecked(tp.ignoreVia);
    if (g_chkDebug)     g_chkDebug->setChecked(tp.debugImage);
    if (g_chkEngraverSpindle) g_chkEngraverSpindle->setChecked(tp.engraverSpindle);
    if (g_fldDrillDwell) g_fldDrillDwell->setText(dblStr(tp.drillDwell, 3).c_str());
}

// ════════════════════════════════════════════════════════════════════════════
// Logging
// ════════════════════════════════════════════════════════════════════════════

void logMsg(const std::string& msg) {
    if (g_logArea) g_logArea->append(msg + "\r\n");
}

void logMsg(const wchar_t* msg) {
    if (g_logArea) {
        std::string utf8 = StringUtils::wideToUtf8(msg);
        g_logArea->append(utf8 + "\r\n");
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Settings
// ════════════════════════════════════════════════════════════════════════════

void loadSettings() {
    if (!g_settings) return;
    auto& s = *g_settings;

    if (g_fldKicadDir)   g_fldKicadDir->setText(s.getValue("kicad_dir", "").c_str());
    if (g_fldOutputFile) g_fldOutputFile->setText(s.getValue("output_file", "").c_str());
    if (g_fldToolDia)    g_fldToolDia->setText(s.getValue("tip_width", "0.1").c_str());
    if (g_fldCutDepth)   g_fldCutDepth->setText(s.getValue("z_cut", "0.05").c_str());
    if (g_fldSafeHeight) g_fldSafeHeight->setText(s.getValue("z_travel", "5").c_str());
    if (g_fldFeedXY)     g_fldFeedXY->setText(s.getValue("feed_xy", "300").c_str());
    if (g_fldFeedZ)      g_fldFeedZ->setText(s.getValue("feed_z", "100").c_str());
    if (g_fldOverlap)    g_fldOverlap->setText(s.getValue("overlap", "0.4").c_str());
    if (g_fldOffset)     g_fldOffset->setText(s.getValue("offset", "0.02").c_str());
    if (g_fldMaterial)   g_fldMaterial->setText(s.getValue("material", "1.5").c_str());
    if (g_fldZDrill)     g_fldZDrill->setText(s.getValue("z_drill", "2").c_str());
    if (g_fldDrillDia)   g_fldDrillDia->setText(s.getValue("drill_dia", "0.8").c_str());
    if (g_fldDrillFeed)  g_fldDrillFeed->setText(s.getValue("drill_feed", "60").c_str());
    if (g_fldXOffset)    g_fldXOffset->setText(s.getValue("x_offset", "0").c_str());
    if (g_fldYOffset)    g_fldYOffset->setText(s.getValue("y_offset", "0").c_str());

    if (g_chkFlip)       g_chkFlip->setChecked(s.getValue("flip", "0") == "1");
    if (g_chkIgnoreVia)  g_chkIgnoreVia->setChecked(s.getValue("ignore_via", "0") == "1");
    if (g_chkDebug)      g_chkDebug->setChecked(s.getValue("debug_image", "1") == "1");
    if (g_chkFluidNC)    g_chkFluidNC->setChecked(s.getValue("post_profile", "mach3") == "fluidnc");
    if (g_chkEngraverSpindle) g_chkEngraverSpindle->setChecked(s.getValue("engraver_spindle", "0") == "1");
    if (g_fldDrillDwell) g_fldDrillDwell->setText(s.getValue("drill_dwell", "0").c_str());

    // Load layer visibility from settings
    if (g_canvas) {
        auto& lay = g_canvas->layers();
        lay.isolation = s.getValue("gen_isolation", "1") == "1";
        lay.drillsPTH = s.getValue("gen_drilling", "1") == "1";
        lay.drillsNPTH = s.getValue("gen_drilling", "1") == "1";
        lay.cutout = s.getValue("gen_cutout", "0") == "1";
    }

    // Load tool presets
    loadToolPresets();
}

void saveSettings() {
    if (!g_settings) return;
    auto& s = *g_settings;

    if (g_fldKicadDir)   s.setValue("kicad_dir",    g_fldKicadDir->getText());
    if (g_fldOutputFile) s.setValue("output_file",   g_fldOutputFile->getText());
    if (g_fldToolDia)    s.setValue("tip_width",     g_fldToolDia->getText());
    if (g_fldCutDepth)   s.setValue("z_cut",         g_fldCutDepth->getText());
    if (g_fldSafeHeight) s.setValue("z_travel",      g_fldSafeHeight->getText());
    if (g_fldFeedXY)     s.setValue("feed_xy",       g_fldFeedXY->getText());
    if (g_fldFeedZ)      s.setValue("feed_z",        g_fldFeedZ->getText());
    if (g_fldOverlap)    s.setValue("overlap",       g_fldOverlap->getText());
    if (g_fldOffset)     s.setValue("offset",        g_fldOffset->getText());
    if (g_fldMaterial)   s.setValue("material",      g_fldMaterial->getText());
    if (g_fldZDrill)     s.setValue("z_drill",       g_fldZDrill->getText());
    if (g_fldDrillDia)   s.setValue("drill_dia",     g_fldDrillDia->getText());
    if (g_fldDrillFeed)  s.setValue("drill_feed",    g_fldDrillFeed->getText());
    if (g_fldXOffset)    s.setValue("x_offset",      g_fldXOffset->getText());
    if (g_fldYOffset)    s.setValue("y_offset",      g_fldYOffset->getText());

    if (g_chkFlip)       s.setValue("flip",          g_chkFlip->isChecked() ? "1" : "0");
    if (g_chkIgnoreVia)  s.setValue("ignore_via",    g_chkIgnoreVia->isChecked() ? "1" : "0");
    if (g_chkDebug)      s.setValue("debug_image",   g_chkDebug->isChecked() ? "1" : "0");
    if (g_chkFluidNC)    s.setValue("post_profile",  g_chkFluidNC->isChecked() ? "fluidnc" : "mach3");
    if (g_chkEngraverSpindle) s.setValue("engraver_spindle", g_chkEngraverSpindle->isChecked() ? "1" : "0");
    if (g_fldDrillDwell) s.setValue("drill_dwell",   g_fldDrillDwell->getText());

    // Save layer visibility from canvas
    if (g_canvas) {
        auto& lay = g_canvas->layers();
        s.setValue("gen_isolation", lay.isolation ? "1" : "0");
        s.setValue("gen_drilling",  (lay.drillsPTH || lay.drillsNPTH) ? "1" : "0");
        s.setValue("gen_cutout",    lay.cutout ? "1" : "0");
    }

    saveToolPresets();
}

// ════════════════════════════════════════════════════════════════════════════
// Tool Presets
// ════════════════════════════════════════════════════════════════════════════

void createDefaultToolPresets() {
    g_toolPresets.clear();
    auto add = [&](ToolPresetKind kind, const char* name,
                   double dia, double depth, double safe,
                   double fxy, double fz,
                   double zDrill, double spDia, double dFeed) {
        ToolPreset tp;
        tp.name = name;
        tp.toolDiameter = dia;
        tp.cutDepth = depth;
        tp.safeHeight = safe;
        tp.feedRateXY = fxy;
        tp.feedRateZ = fz;
        tp.zDrill = zDrill;
        tp.drillDiameter = spDia;
        tp.drillFeed = dFeed;
        tp.overlap = defaultOverlapForKind(kind);
        tp.offset = defaultOffsetForKind(kind);
        tp.xOffset = 0.0;
        tp.yOffset = 0.0;
        tp.flip = false;
        tp.ignoreVia = false;
        tp.debugImage = true;
        tp.engraverSpindle = false;
        tp.drillDwell = 0.0;
        tp.kind = kind;
        g_toolPresets.push_back(tp);
    };

    // Mach3 + FluidNC conservative defaults
    // Isolation V-bits
    add(ToolPresetKind::Isolation, "V-bit 10deg 0.05mm",           0.05, 0.03, 5.0, 120.0, 20.0, 2.0, 0.8, 50.0);
    add(ToolPresetKind::Isolation, "V-bit 20deg 0.10mm",           0.10, 0.05, 5.0, 160.0, 25.0, 2.0, 0.8, 60.0);
    add(ToolPresetKind::Isolation, "V-bit 30deg 0.08mm (0.003in)", 0.08, 0.05, 5.0, 140.0, 22.0, 2.0, 0.8, 60.0);
    add(ToolPresetKind::Isolation, "V-bit 30deg 0.10mm",           0.10, 0.06, 5.0, 180.0, 28.0, 2.0, 0.8, 60.0);
    add(ToolPresetKind::Isolation, "V-bit 30deg 0.13mm (0.005in)", 0.13, 0.08, 5.0, 220.0, 35.0, 2.0, 0.8, 70.0);
    add(ToolPresetKind::Isolation, "V-bit 30deg 0.20mm",           0.20, 0.10, 5.0, 260.0, 45.0, 2.0, 0.8, 80.0);
    add(ToolPresetKind::Isolation, "V-bit 45deg 0.20mm",           0.20, 0.10, 5.0, 300.0, 55.0, 2.0, 0.8, 90.0);
    add(ToolPresetKind::Isolation, "V-bit 60deg 0.30mm",           0.30, 0.15, 5.0, 340.0, 65.0, 2.0, 0.8, 100.0);

    // Combo (isolation + drilling with end mills)
    add(ToolPresetKind::Combo, "End mill 0.40mm (1/64in)",    0.40, 0.10, 5.0, 180.0, 35.0, 2.0, 0.4, 90.0);
    add(ToolPresetKind::Combo, "End mill 0.60mm",             0.60, 0.12, 5.0, 220.0, 45.0, 2.0, 0.6, 110.0);
    add(ToolPresetKind::Combo, "End mill 0.80mm (1/32in)",    0.80, 0.15, 5.0, 260.0, 55.0, 2.0, 0.8, 130.0);
    add(ToolPresetKind::Combo, "End mill 1.00mm",             1.00, 0.20, 5.0, 300.0, 65.0, 2.0, 1.0, 150.0);
    add(ToolPresetKind::Combo, "End mill 1.20mm",             1.20, 0.25, 5.0, 340.0, 75.0, 2.0, 1.2, 170.0);

    // Cutout (spindle driven)
    add(ToolPresetKind::Cutout, "Cutout mill 1.60mm (1/16in)", 1.60, 0.35, 5.0, 420.0, 90.0, 2.0, 1.6, 220.0);
    add(ToolPresetKind::Cutout, "End mill 2.00mm",             2.00, 0.40, 5.0, 480.0, 100.0, 2.0, 2.0, 260.0);
    add(ToolPresetKind::Cutout, "End mill 3.20mm (1/8in)",     3.20, 0.60, 5.0, 650.0, 140.0, 2.0, 3.2, 320.0);

    // Drills (common PCB hole sizes)
    add(ToolPresetKind::Drill, "Drill 0.30mm", 0.30, 2.00, 5.0, 160.0, 20.0, 2.0, 0.3, 25.0);
    add(ToolPresetKind::Drill, "Drill 0.40mm", 0.40, 2.00, 5.0, 180.0, 25.0, 2.0, 0.4, 30.0);
    add(ToolPresetKind::Drill, "Drill 0.50mm", 0.50, 2.00, 5.0, 200.0, 30.0, 2.0, 0.5, 35.0);
    add(ToolPresetKind::Drill, "Drill 0.60mm", 0.60, 2.00, 5.0, 220.0, 35.0, 2.0, 0.6, 40.0);
    add(ToolPresetKind::Drill, "Drill 0.80mm", 0.80, 2.00, 5.0, 240.0, 40.0, 2.0, 0.8, 50.0);
    add(ToolPresetKind::Drill, "Drill 0.90mm", 0.90, 2.00, 5.0, 260.0, 45.0, 2.0, 0.9, 55.0);
    add(ToolPresetKind::Drill, "Drill 1.00mm", 1.00, 2.00, 5.0, 280.0, 50.0, 2.0, 1.0, 60.0);
    add(ToolPresetKind::Drill, "Drill 1.20mm", 1.20, 2.00, 5.0, 300.0, 55.0, 2.0, 1.2, 70.0);
    add(ToolPresetKind::Drill, "Drill 1.50mm", 1.50, 2.00, 5.0, 320.0, 60.0, 2.0, 1.5, 85.0);
    add(ToolPresetKind::Drill, "Drill 2.00mm", 2.00, 2.00, 5.0, 360.0, 70.0, 2.0, 2.0, 100.0);
    add(ToolPresetKind::Drill, "Drill 3.00mm", 3.00, 2.00, 5.0, 420.0, 90.0, 2.0, 3.0, 120.0);
    add(ToolPresetKind::Drill, "Drill 3.20mm", 3.20, 2.00, 5.0, 450.0, 95.0, 2.0, 3.2, 130.0);

    g_activeToolIndex = 0;
}

void loadToolPresets() {
    if (!g_toolConfig) return;
    auto& tc = *g_toolConfig;

    int count = 0;
    try { count = std::stoi(tc.getValue("tool_count", "0")); } catch (...) {}

    g_toolPresets.clear();
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            std::string p = "tool_" + intStr(i) + "_";
            ToolPreset tp;
            tp.name         = tc.getValue(p + "name", "Tool " + intStr(i));
            tp.kind         = presetKindFromKey(tc.getValue(p + "kind",
                presetKindToKey(inferPresetKindFromName(tp.name))));
            tp.toolDiameter = parseD(tc.getValue(p + "toolDiameter", "0.2"));
            tp.cutDepth     = std::abs(parseD(tc.getValue(p + "cutDepth", "0.05")));
            tp.safeHeight   = parseD(tc.getValue(p + "safeHeight", "5.0"));
            tp.feedRateXY   = parseD(tc.getValue(p + "feedXY", "300"));
            tp.feedRateZ    = parseD(tc.getValue(p + "feedZ", "100"));
            tp.zDrill       = std::abs(parseD(tc.getValue(p + "zDrill", "2")));
            tp.drillDiameter = parseD(tc.getValue(p + "drillDiameter",
                dblStr(defaultSpindleDiameterForPreset(tp.name, tp.toolDiameter), 3)));
            tp.drillFeed    = parseD(tc.getValue(p + "drillFeed", "60"));
            tp.overlap      = parseD(tc.getValue(p + "overlap", dblStr(defaultOverlapForKind(tp.kind), 2)));
            tp.offset       = parseD(tc.getValue(p + "offset",  dblStr(defaultOffsetForKind(tp.kind), 2)));
            tp.xOffset      = parseD(tc.getValue(p + "xOffset", "0"));
            tp.yOffset      = parseD(tc.getValue(p + "yOffset", "0"));
            tp.flip         = tc.getValue(p + "flip", "0") == "1";
            tp.ignoreVia    = tc.getValue(p + "ignoreVia", "0") == "1";
            tp.debugImage   = tc.getValue(p + "debugImage", "1") == "1";
            tp.engraverSpindle = tc.getValue(p + "engraverSpindle", "0") == "1";
            tp.drillDwell   = parseD(tc.getValue(p + "drillDwell", "0"));
            g_toolPresets.push_back(tp);
        }
    } else {
        createDefaultToolPresets();
    }

    try { g_activeToolIndex = std::stoi(tc.getValue("active_tool", "0")); } catch (...) { g_activeToolIndex = 0; }
    if (g_activeToolIndex < 0 || g_activeToolIndex >= (int)g_toolPresets.size())
        g_activeToolIndex = 0;

    applyActiveToolPreset();
}

void saveToolPresets() {
    if (!g_toolConfig) return;
    auto& tc = *g_toolConfig;

    tc.setValue("tool_count", intStr((int)g_toolPresets.size()));
    for (int i = 0; i < (int)g_toolPresets.size(); i++) {
        std::string p = "tool_" + intStr(i) + "_";
        auto& tp = g_toolPresets[i];
        tc.setValue(p + "name",         tp.name);
        tc.setValue(p + "toolDiameter", dblStr(tp.toolDiameter, 3));
        tc.setValue(p + "cutDepth",     dblStr(tp.cutDepth, 3));
        tc.setValue(p + "safeHeight",   dblStr(tp.safeHeight));
        tc.setValue(p + "feedXY",       dblStr(tp.feedRateXY, 1));
        tc.setValue(p + "feedZ",        dblStr(tp.feedRateZ, 1));
        tc.setValue(p + "zDrill",       dblStr(tp.zDrill));
        tc.setValue(p + "drillDiameter", dblStr(tp.drillDiameter, 3));
        tc.setValue(p + "drillFeed",    dblStr(tp.drillFeed, 1));
        tc.setValue(p + "overlap",      dblStr(tp.overlap, 2));
        tc.setValue(p + "offset",       dblStr(tp.offset, 2));
        tc.setValue(p + "xOffset",      dblStr(tp.xOffset, 2));
        tc.setValue(p + "yOffset",      dblStr(tp.yOffset, 2));
        tc.setValue(p + "flip",         tp.flip ? "1" : "0");
        tc.setValue(p + "ignoreVia",    tp.ignoreVia ? "1" : "0");
        tc.setValue(p + "debugImage",   tp.debugImage ? "1" : "0");
        tc.setValue(p + "engraverSpindle", tp.engraverSpindle ? "1" : "0");
        tc.setValue(p + "drillDwell",   dblStr(tp.drillDwell, 3));
        tc.setValue(p + "kind",         presetKindToKey(tp.kind));
    }
    tc.setValue("active_tool", intStr(g_activeToolIndex));
}

void applyActiveToolPreset() {
    if (g_activeToolIndex < 0 || g_activeToolIndex >= (int)g_toolPresets.size()) return;
    const auto& tp = g_toolPresets[g_activeToolIndex];

    if (g_fldToolDia)    g_fldToolDia->setText(dblStr(tp.toolDiameter, 3).c_str());
    if (g_fldCutDepth)   g_fldCutDepth->setText(dblStr(tp.cutDepth, 3).c_str());
    if (g_fldSafeHeight) g_fldSafeHeight->setText(dblStr(tp.safeHeight).c_str());
    if (g_fldFeedXY)     g_fldFeedXY->setText(dblStr(tp.feedRateXY, 1).c_str());
    if (g_fldFeedZ)      g_fldFeedZ->setText(dblStr(tp.feedRateZ, 1).c_str());
    if (g_fldZDrill)     g_fldZDrill->setText(dblStr(tp.zDrill).c_str());
    if (g_fldDrillDia)   g_fldDrillDia->setText(dblStr(tp.drillDiameter, 3).c_str());
    if (g_fldDrillFeed)  g_fldDrillFeed->setText(dblStr(tp.drillFeed, 1).c_str());

    applyPresetManagedValues(tp);
    applyPresetJobMode(tp.kind);

    if (g_canvas) {
        rebuildLayerPanel();
        g_canvas->redraw();
    }

    if (g_pipelineData.valid) {
        scheduleAutoRefresh(true);
    }
}

void doSelectTool(int index) {
    if (index < 0 || index >= (int)g_toolPresets.size()) return;
    g_activeToolIndex = index;
    applyActiveToolPreset();
    updateToolButtonText();
    saveSettings();
}

void updateToolButtonText() {
    if (!g_btnTool) return;
    std::wstring text = L"\u25BC ";
    if (g_activeToolIndex >= 0 && g_activeToolIndex < (int)g_toolPresets.size()) {
        text += L"[" + presetKindTag(g_toolPresets[g_activeToolIndex].kind) + L"] ";
        text += StringUtils::utf8ToWide(g_toolPresets[g_activeToolIndex].name);
    }
    SetWindowTextW(g_btnTool->getHandle(), text.c_str());
}

// ════════════════════════════════════════════════════════════════════════════
// Tool popup (context menu)
// ════════════════════════════════════════════════════════════════════════════

void showToolPopup() {
    if (!g_btnTool || !g_window) return;
    RECT rc;
    GetWindowRect(g_btnTool->getHandle(), &rc);

    HMENU hMenu = CreatePopupMenu();
    ToolPresetKind lastKind = ToolPresetKind::Isolation;
    bool hasLastKind = false;
    for (int i = 0; i < (int)g_toolPresets.size(); i++) {
        ToolPresetKind kind = g_toolPresets[i].kind;
        if (!hasLastKind || kind != lastKind) {
            if (hasLastKind) AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            std::wstring header = L"[ " + presetKindHeader(kind) + L" ]";
            AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, header.c_str());
            hasLastKind = true;
            lastKind = kind;
        }
        std::wstring name = StringUtils::utf8ToWide(g_toolPresets[i].name);
        UINT flags = MF_STRING;
        if (i == g_activeToolIndex) flags |= MF_CHECKED;
        AppendMenuW(hMenu, flags, 10000 + i, name.c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 10999, L"Manage tools...");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                             rc.left, rc.bottom, 0, g_window->getHandle(), NULL);
    DestroyMenu(hMenu);

    if (cmd >= 10000 && cmd < 10999) {
        doSelectTool(cmd - 10000);
    } else if (cmd == 10999) {
        doShowToolPresets();
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Tool Presets Dialog (modal)
// ════════════════════════════════════════════════════════════════════════════

static HWND s_hToolList  = nullptr;
static HWND s_hToolName  = nullptr;
static HWND s_hToolDia   = nullptr;
static HWND s_hToolDepth = nullptr;
static HWND s_hToolSafeH = nullptr;
static HWND s_hToolFXY   = nullptr;
static HWND s_hToolFZ    = nullptr;
static HWND s_hToolZDr   = nullptr;
static HWND s_hToolDrDia = nullptr;
static HWND s_hToolDrF   = nullptr;
static HWND s_hToolOverlap = nullptr;
static HWND s_hToolOffset  = nullptr;
static HWND s_hToolDrillDwell = nullptr;
static HWND s_hToolEngSpindle = nullptr;
static int  s_toolSel    = -1;

static void toolDlgPopulate(int idx) {
    if (idx < 0 || idx >= (int)g_toolPresets.size()) {
        SetWindowTextW(s_hToolName, L"");
        SetWindowTextW(s_hToolDia, L"");
        SetWindowTextW(s_hToolDepth, L"");
        SetWindowTextW(s_hToolSafeH, L"");
        SetWindowTextW(s_hToolFXY, L"");
        SetWindowTextW(s_hToolFZ, L"");
        SetWindowTextW(s_hToolZDr, L"");
        SetWindowTextW(s_hToolDrDia, L"");
        SetWindowTextW(s_hToolDrF, L"");
        SetWindowTextW(s_hToolOverlap, L"");
        SetWindowTextW(s_hToolOffset, L"");
        SetWindowTextW(s_hToolDrillDwell, L"");
        if (s_hToolEngSpindle) SendMessageW(s_hToolEngSpindle, BM_SETCHECK, BST_UNCHECKED, 0);
        return;
    }
    const auto& tp = g_toolPresets[idx];
    SetWindowTextW(s_hToolName,  StringUtils::utf8ToWide(tp.name).c_str());
    SetWindowTextW(s_hToolDia,   StringUtils::utf8ToWide(dblStr(tp.toolDiameter, 3)).c_str());
    SetWindowTextW(s_hToolDepth, StringUtils::utf8ToWide(dblStr(tp.cutDepth, 3)).c_str());
    SetWindowTextW(s_hToolSafeH, StringUtils::utf8ToWide(dblStr(tp.safeHeight)).c_str());
    SetWindowTextW(s_hToolFXY,   StringUtils::utf8ToWide(dblStr(tp.feedRateXY, 1)).c_str());
    SetWindowTextW(s_hToolFZ,    StringUtils::utf8ToWide(dblStr(tp.feedRateZ, 1)).c_str());
    SetWindowTextW(s_hToolZDr,   StringUtils::utf8ToWide(dblStr(tp.zDrill)).c_str());
    SetWindowTextW(s_hToolDrDia, StringUtils::utf8ToWide(dblStr(tp.drillDiameter, 3)).c_str());
    SetWindowTextW(s_hToolDrF,   StringUtils::utf8ToWide(dblStr(tp.drillFeed, 1)).c_str());
    SetWindowTextW(s_hToolOverlap, StringUtils::utf8ToWide(dblStr(tp.overlap, 2)).c_str());
    SetWindowTextW(s_hToolOffset,  StringUtils::utf8ToWide(dblStr(tp.offset, 2)).c_str());
    SetWindowTextW(s_hToolDrillDwell, StringUtils::utf8ToWide(dblStr(tp.drillDwell, 3)).c_str());
    if (s_hToolEngSpindle) SendMessageW(s_hToolEngSpindle, BM_SETCHECK,
        tp.engraverSpindle ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void toolDlgSave(int idx) {
    if (idx < 0 || idx >= (int)g_toolPresets.size()) return;
    auto& tp = g_toolPresets[idx];
    wchar_t buf[128];
    GetWindowTextW(s_hToolName, buf, 128);  tp.name = StringUtils::wideToUtf8(buf);
    GetWindowTextW(s_hToolDia, buf, 128);   tp.toolDiameter = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolDepth, buf, 128); tp.cutDepth     = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolSafeH, buf, 128); tp.safeHeight   = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolFXY, buf, 128);   tp.feedRateXY   = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolFZ, buf, 128);    tp.feedRateZ    = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolZDr, buf, 128);   tp.zDrill       = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolDrDia, buf, 128); tp.drillDiameter = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolDrF, buf, 128);   tp.drillFeed    = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolOverlap, buf, 128); tp.overlap    = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolOffset, buf, 128);  tp.offset     = parseD(StringUtils::wideToUtf8(buf));
    GetWindowTextW(s_hToolDrillDwell, buf, 128); tp.drillDwell = parseD(StringUtils::wideToUtf8(buf));
    tp.engraverSpindle = s_hToolEngSpindle
        ? (SendMessageW(s_hToolEngSpindle, BM_GETCHECK, 0, 0) == BST_CHECKED) : false;
    tp.kind = inferPresetKindFromName(tp.name);
}

static void toolDlgRefreshList(int sel = -1) {
    SendMessageW(s_hToolList, LB_RESETCONTENT, 0, 0);
    for (auto& tp : g_toolPresets) {
        std::wstring name = L"[" + presetKindTag(tp.kind) + L"] " + StringUtils::utf8ToWide(tp.name);
        SendMessageW(s_hToolList, LB_ADDSTRING, 0, (LPARAM)name.c_str());
    }
    if (sel >= 0 && sel < (int)g_toolPresets.size())
        SendMessageW(s_hToolList, LB_SETCURSEL, sel, 0);
}

static LRESULT CALLBACK ToolDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int lbW = 170, lbH = 256;
            int fx = 195, fw = 250, lw = 75;

            s_hToolList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                15, 15, lbW, lbH, hwnd, (HMENU)100, _core.hInstance, NULL);

            auto mkField = [&](int row, const wchar_t* label, HWND& hEdit, int id) {
                int y = 15 + row * 28;
                CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE,
                    fx, y + 2, lw, 20, hwnd, NULL, _core.hInstance, NULL);
                hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    fx + lw + 4, y, fw - lw - 4, 22,
                    hwnd, (HMENU)(UINT_PTR)id, _core.hInstance, NULL);
            };

            mkField(0, L"Name:",     s_hToolName,  101);
            mkField(1, L"Diameter:", s_hToolDia,   102);
            mkField(2, L"Cut depth:", s_hToolDepth, 103);
            mkField(3, L"Safe H:",   s_hToolSafeH, 104);
            mkField(4, L"Feed XY:",  s_hToolFXY,   105);
            mkField(5, L"Feed Z:",   s_hToolFZ,    106);
            mkField(6, L"Z Drill:",  s_hToolZDr,   107);
            mkField(7, L"Sp. Dia:",  s_hToolDrDia, 108);
            mkField(8, L"Sp. Feed:", s_hToolDrF,   109);
            mkField(9, L"Overlap:", s_hToolOverlap, 114);
            mkField(10, L"Offset:", s_hToolOffset, 115);
            mkField(11, L"Dwell (s):", s_hToolDrillDwell, 116);

            // Engraver spindle checkbox
            s_hToolEngSpindle = CreateWindowExW(0, L"BUTTON", L"Engr. M3",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                fx, 15 + 12 * 28 + 2, fw, 20,
                hwnd, (HMENU)117, _core.hInstance, NULL);

            // Buttons
            CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
                15, lbH + 20, 80, 26, hwnd, (HMENU)110, _core.hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE,
                105, lbH + 20, 80, 26, hwnd, (HMENU)111, _core.hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE,
                fx, 15 + 13 * 28, 80, 26, hwnd, (HMENU)112, _core.hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                fx + 90, 15 + 13 * 28, 80, 26, hwnd, (HMENU)113, _core.hInstance, NULL);

            toolDlgRefreshList(g_activeToolIndex);
            s_toolSel = g_activeToolIndex;
            toolDlgPopulate(s_toolSel);
            return 0;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            // Listbox selection change
            if (wmId == 100 && wmEvent == LBN_SELCHANGE) {
                if (s_toolSel >= 0) toolDlgSave(s_toolSel);
                s_toolSel = (int)SendMessageW(s_hToolList, LB_GETCURSEL, 0, 0);
                toolDlgPopulate(s_toolSel);
                return 0;
            }
            // Double-click activates
            if (wmId == 100 && wmEvent == LBN_DBLCLK) {
                int sel = (int)SendMessageW(s_hToolList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)g_toolPresets.size()) {
                    toolDlgSave(sel);
                    g_activeToolIndex = sel;
                    applyActiveToolPreset();
                    updateToolButtonText();
                    saveToolPresets();
                }
                return 0;
            }

            switch (wmId) {
                case 110: { // Add
                    if (s_toolSel >= 0) toolDlgSave(s_toolSel);
                    g_toolPresets.push_back(ToolPreset());
                    int ni = (int)g_toolPresets.size() - 1;
                    toolDlgRefreshList(ni);
                    s_toolSel = ni;
                    toolDlgPopulate(s_toolSel);
                    break;
                }
                case 111: { // Remove
                    if (s_toolSel >= 0 && (int)g_toolPresets.size() > 1) {
                        g_toolPresets.erase(g_toolPresets.begin() + s_toolSel);
                        if (g_activeToolIndex >= (int)g_toolPresets.size())
                            g_activeToolIndex = (int)g_toolPresets.size() - 1;
                        if (s_toolSel >= (int)g_toolPresets.size())
                            s_toolSel = (int)g_toolPresets.size() - 1;
                        toolDlgRefreshList(s_toolSel);
                        toolDlgPopulate(s_toolSel);
                    }
                    break;
                }
                case 112: { // Save
                    if (s_toolSel >= 0) {
                        toolDlgSave(s_toolSel);
                        toolDlgRefreshList(s_toolSel);
                        if (s_toolSel == g_activeToolIndex) {
                            applyActiveToolPreset();
                            updateToolButtonText();
                        }
                        saveToolPresets();
                    }
                    break;
                }
                case 113: // Close
                    if (s_toolSel >= 0) toolDlgSave(s_toolSel);
                    applyActiveToolPreset();
                    updateToolButtonText();
                    saveToolPresets();
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }
        case WM_DESTROY:
            EnableWindow(g_window->getHandle(), TRUE);
            SetForegroundWindow(g_window->getHandle());
            return 0;
        case WM_CLOSE:
            if (s_toolSel >= 0) toolDlgSave(s_toolSel);
            applyActiveToolPreset();
            updateToolButtonText();
            saveToolPresets();
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void doShowToolPresets() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ToolDlgProc;
        wc.hInstance      = _core.hInstance;
        wc.hCursor        = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName  = L"G2G_ToolPresets";
        RegisterClassExW(&wc);
        registered = true;
    }

    EnableWindow(g_window->getHandle(), FALSE);

    RECT pr;
    GetWindowRect(g_window->getHandle(), &pr);
    int cx = (pr.left + pr.right) / 2 - 240;
    int cy = (pr.top + pr.bottom) / 2 - 210;

    CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"G2G_ToolPresets", L"Tool Presets",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, 480, 450,
        g_window->getHandle(), NULL, _core.hInstance, NULL);
}

// ════════════════════════════════════════════════════════════════════════════
// Config builder
// ════════════════════════════════════════════════════════════════════════════

Config buildConfigFromGUI() {
    Config cfg;
    auto d = [](InputField* f) -> double {
        return f ? parseD(f->getText()) : 0.0;
    };

    cfg.machine.materialThickness = d(g_fldMaterial);
    cfg.machine.engraver_tip_width  = d(g_fldToolDia);
    cfg.machine.engraver_z_cut      = std::abs(d(g_fldCutDepth));
    cfg.machine.engraver_z_travel   = d(g_fldSafeHeight);
    cfg.machine.spindle_z_drill     = std::abs(d(g_fldZDrill));
    cfg.machine.spindle_tool_diameter = d(g_fldDrillDia);
    cfg.machine.move_feedrate       = 2400;

    cfg.cam.overlap = d(g_fldOverlap);
    cfg.cam.offset  = d(g_fldOffset);

    cfg.job.engraver_feedrate  = d(g_fldFeedXY);
    cfg.job.engraver_plunge_feedrate = d(g_fldFeedZ);
    cfg.job.engraver_spindle_on = g_chkEngraverSpindle && g_chkEngraverSpindle->isChecked();
    cfg.job.spindle_feedrate   = d(g_fldDrillFeed);
    cfg.job.spindle_power      = 255;
    cfg.job.drill_dwell        = d(g_fldDrillDwell);
    cfg.job.postProfile        = (g_chkFluidNC && g_chkFluidNC->isChecked())
        ? PostProfile::FluidNC
        : PostProfile::Mach3;

    return cfg;
}

// ════════════════════════════════════════════════════════════════════════════
// Layer panel
// ════════════════════════════════════════════════════════════════════════════

void rebuildLayerPanel() {
    if (!g_hLayerPanel || !g_canvas) return;
    SendMessageW(g_hLayerPanel, LB_RESETCONTENT, 0, 0);
    g_layerItems.clear();

    auto& lay = g_canvas->layers();
    auto& pres = g_canvas->presence();

    // Each item is either a section header (non-clickable) or a toggleable layer.
    // g_layerItems maps listbox indices to bool* toggle targets.

    auto addSection = [](const wchar_t* name) {
        std::wstring text = L"\u2500\u2500 ";
        text += name;
        text += L" \u2500\u2500";
        SendMessageW(g_hLayerPanel, LB_ADDSTRING, 0, (LPARAM)text.c_str());
        g_layerItems.push_back({true, nullptr});
    };

    auto addItem = [](const wchar_t* name, bool visible, bool* flag) {
        std::wstring text = visible ? L"  \u2611 " : L"  \u2610 ";
        text += name;
        SendMessageW(g_hLayerPanel, LB_ADDSTRING, 0, (LPARAM)text.c_str());
        g_layerItems.push_back({false, flag});
    };

    auto addSubItem = [](const wchar_t* name, bool visible, bool* flag) {
        std::wstring text = visible ? L"      \u2611 " : L"      \u2610 ";
        text += name;
        SendMessageW(g_hLayerPanel, LB_ADDSTRING, 0, (LPARAM)text.c_str());
        g_layerItems.push_back({false, flag});
    };

    auto addSubSubItem = [](const wchar_t* name, bool visible, bool* flag) {
        std::wstring text = visible ? L"          \u2611 " : L"          \u2610 ";
        text += name;
        SendMessageW(g_hLayerPanel, LB_ADDSTRING, 0, (LPARAM)text.c_str());
        g_layerItems.push_back({false, flag});
    };

    // ── Board ──
    addSection(L"Board");
    addItem(L"Board Outline", lay.outline, &lay.outline);

    // ── Copper ──
    if (pres.copperTop || pres.copperBottom) {
        addSection(L"Copper");
        if (pres.copperTop) {
            addItem(L"Copper Top (F_Cu)", lay.copperTop, &lay.copperTop);
            if (pres.copperTopSub.traces)  addSubItem(L"Traces",  lay.copperTopSub.traces,  &lay.copperTopSub.traces);
            if (pres.copperTopSub.pads) {
                addSubItem(L"Pads", lay.copperTopSub.pads, &lay.copperTopSub.pads);
                auto* pgTop = g_canvas->copperTopPadGroups();
                if (pgTop) {
                    for (auto& pg : *pgTop) {
                        std::wstring wname = StringUtils::utf8ToWide(pg.name);
                        wchar_t buf[128];
                        _snwprintf(buf, 128, L"%ls (%d)", wname.c_str(), pg.count);
                        addSubSubItem(buf, pg.visible, &pg.visible);
                    }
                }
            }
            if (pres.copperTopSub.regions) addSubItem(L"Regions", lay.copperTopSub.regions, &lay.copperTopSub.regions);
        }
        if (pres.copperBottom) {
            addItem(L"Copper Bottom (B_Cu)", lay.copperBottom, &lay.copperBottom);
            if (pres.copperBottomSub.traces)  addSubItem(L"Traces",  lay.copperBottomSub.traces,  &lay.copperBottomSub.traces);
            if (pres.copperBottomSub.pads) {
                addSubItem(L"Pads", lay.copperBottomSub.pads, &lay.copperBottomSub.pads);
                auto* pgBot = g_canvas->copperBottomPadGroups();
                if (pgBot) {
                    for (auto& pg : *pgBot) {
                        std::wstring wname = StringUtils::utf8ToWide(pg.name);
                        wchar_t buf[128];
                        _snwprintf(buf, 128, L"%ls (%d)", wname.c_str(), pg.count);
                        addSubSubItem(buf, pg.visible, &pg.visible);
                    }
                }
            }
            if (pres.copperBottomSub.regions) addSubItem(L"Regions", lay.copperBottomSub.regions, &lay.copperBottomSub.regions);
        }
    }

    // ── Layers ──
    if (pres.maskTop || pres.maskBottom || pres.silkTop || pres.silkBottom ||
        pres.pasteTop || pres.pasteBottom) {
        addSection(L"Layers");
        if (pres.maskTop)      addItem(L"Mask Top",       lay.maskTop,      &lay.maskTop);
        if (pres.maskBottom)   addItem(L"Mask Bottom",    lay.maskBottom,   &lay.maskBottom);
        if (pres.silkTop)      addItem(L"Silkscreen Top", lay.silkTop,      &lay.silkTop);
        if (pres.silkBottom)   addItem(L"Silkscreen Bot", lay.silkBottom,   &lay.silkBottom);
        if (pres.pasteTop)     addItem(L"Paste Top",      lay.pasteTop,     &lay.pasteTop);
        if (pres.pasteBottom)  addItem(L"Paste Bottom",   lay.pasteBottom,  &lay.pasteBottom);
    }

    // ── Drills with per-diameter sub-items ──
    if (pres.drillsPTH || pres.drillsNPTH) {
        addSection(L"Drills");
        if (pres.drillsPTH) {
            addItem(L"PTH Drills", lay.drillsPTH, &lay.drillsPTH);
            for (auto& f : g_canvas->drillFilterPTH()) {
                wchar_t buf[64];
                _snwprintf(buf, 64, L"\u00D8%.3fmm (%d)", f.diameter, f.count);
                addSubItem(buf, f.visible, &f.visible);
            }
        }
        if (pres.drillsNPTH) {
            addItem(L"NPTH Drills", lay.drillsNPTH, &lay.drillsNPTH);
            for (auto& f : g_canvas->drillFilterNPTH()) {
                wchar_t buf[64];
                _snwprintf(buf, 64, L"\u00D8%.3fmm (%d)", f.diameter, f.count);
                addSubItem(buf, f.visible, &f.visible);
            }
        }
    }

    // ── Generated ──
    if (pres.clearance || pres.isolation || pres.cutout) {
        addSection(L"Generated");
        if (pres.clearance)    addItem(L"Clearance",       lay.clearance,  &lay.clearance);
        if (pres.isolation)    addItem(L"Isolation Paths", lay.isolation,  &lay.isolation);
        if (pres.cutout)       addItem(L"Cutout",          lay.cutout,     &lay.cutout);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Auto-refresh
// ════════════════════════════════════════════════════════════════════════════

void scheduleAutoRefresh(bool reparse) {
    if (reparse) g_needsReparse = true;
    if (!g_window) return;
    KillTimer(g_window->getHandle(), TIMER_AUTO_REFRESH);
    SetTimer(g_window->getHandle(), TIMER_AUTO_REFRESH, AUTO_REFRESH_DELAY, NULL);
}

void doRefreshIsolation() {
    if (!g_pipelineData.valid) return;
    if (g_pipelineData.clearance.empty()) return;
    Config cfg = buildConfigFromGUI();
    g_pipelineData.contours = generateToolpath(g_pipelineData.clearance, cfg);
    if (g_canvas) {
        g_canvas->setContours(g_pipelineData.contours.empty() ? nullptr : &g_pipelineData.contours);
        g_canvas->redraw();
        rebuildLayerPanel();
    }
}

// Recompute clearance from cached copper components (lightweight — no file re-parse)
void doRecomputeClearance() {
    if (!g_pipelineData.valid) return;
    if (!g_canvas) return;

    auto& d = g_pipelineData;
    GerberComponents& comp = d.flipped ? d.copperBottomComp : d.copperTopComp;
    CopperSubVis& subVis = d.flipped ? g_canvas->copperBottomSubVis() : g_canvas->copperTopSubVis();

    // Build filtered copper from components using current sub-vis
    geo::Paths filtered;
    if (subVis.traces  && !comp.traces.empty())  { auto u = geo::unionAll(comp.traces);  filtered.insert(filtered.end(), u.begin(), u.end()); }
    if (subVis.pads) { auto vp = comp.visiblePads(); if (!vp.empty()) { auto u = geo::unionAll(vp); filtered.insert(filtered.end(), u.begin(), u.end()); } }
    if (subVis.regions && !comp.regions.empty()) { auto u = geo::unionAll(comp.regions); filtered.insert(filtered.end(), u.begin(), u.end()); }
    if (!filtered.empty()) filtered = geo::unionAll(filtered);

    // Clip to board outline and compute clearance
    geo::Paths outlinePaths = {d.outline};
    if (!filtered.empty())
        filtered = geo::intersect(filtered, outlinePaths);
    d.clearance = geo::difference(outlinePaths, filtered);

    // Regenerate isolation from new clearance
    Config cfg = buildConfigFromGUI();
    d.contours = generateToolpath(d.clearance, cfg);

    g_canvas->setClearance(d.clearance.empty() ? nullptr : &d.clearance);
    g_canvas->setContours(d.contours.empty() ? nullptr : &d.contours);
    g_canvas->redraw();
    rebuildLayerPanel();
}

// ════════════════════════════════════════════════════════════════════════════
// Canvas update helper
// ════════════════════════════════════════════════════════════════════════════

static void updateCanvasFromPipelineData() {
    if (!g_canvas) return;
    auto& d = g_pipelineData;
    if (!d.valid) { g_canvas->clearData(); return; }

    g_canvas->setOutline(&d.outline);
    g_canvas->setCopperTop(d.copperTop.empty() ? nullptr : &d.copperTop);
    g_canvas->setCopperBottom(d.copperBottom.empty() ? nullptr : &d.copperBottom);

    // Copper sub-components
    g_canvas->setCopperTopTraces(d.copperTopComp.traces.empty() ? nullptr : &d.copperTopComp.traces);
    g_canvas->setCopperTopPads(d.copperTopComp.pads.empty() ? nullptr : &d.copperTopComp.pads);
    g_canvas->setCopperTopRegions(d.copperTopComp.regions.empty() ? nullptr : &d.copperTopComp.regions);
    g_canvas->setCopperBottomTraces(d.copperBottomComp.traces.empty() ? nullptr : &d.copperBottomComp.traces);
    g_canvas->setCopperBottomPads(d.copperBottomComp.pads.empty() ? nullptr : &d.copperBottomComp.pads);
    g_canvas->setCopperBottomRegions(d.copperBottomComp.regions.empty() ? nullptr : &d.copperBottomComp.regions);

    // Pad groups (per-aperture sub-items)
    g_canvas->setCopperTopPadGroups(d.copperTopComp.padGroups.empty() ? nullptr : &d.copperTopComp.padGroups);
    g_canvas->setCopperBottomPadGroups(d.copperBottomComp.padGroups.empty() ? nullptr : &d.copperBottomComp.padGroups);

    g_canvas->setMaskTop(d.maskTop.empty() ? nullptr : &d.maskTop);
    g_canvas->setMaskBottom(d.maskBottom.empty() ? nullptr : &d.maskBottom);
    g_canvas->setSilkTop(d.silkTop.empty() ? nullptr : &d.silkTop);
    g_canvas->setSilkBottom(d.silkBottom.empty() ? nullptr : &d.silkBottom);
    g_canvas->setPasteTop(d.pasteTop.empty() ? nullptr : &d.pasteTop);
    g_canvas->setPasteBottom(d.pasteBottom.empty() ? nullptr : &d.pasteBottom);
    g_canvas->setDrillsPTH(d.drillsPTH.empty() ? nullptr : &d.drillsPTH);
    g_canvas->setDrillsNPTH(d.drillsNPTH.empty() ? nullptr : &d.drillsNPTH);
    g_canvas->setClearance(d.clearance.empty() ? nullptr : &d.clearance);
    g_canvas->setContours(d.contours.empty() ? nullptr : &d.contours);
    g_canvas->setCutout(d.cutoutPath.empty() ? nullptr : &d.cutoutPath);
    g_canvas->setGridExtent(d.boardW + 10, d.boardH + 10);
    g_canvas->zoomToFit(d.boardW, d.boardH);
    rebuildLayerPanel();
}

// ════════════════════════════════════════════════════════════════════════════
// Actions
// ════════════════════════════════════════════════════════════════════════════

void doLoadKicadDir() {
    std::string dir = g_fldKicadDir ? g_fldKicadDir->getText() : "";

    // If field is empty, don't need to re-parse, just return
    if (dir.empty()) {
        logMsg("No KiCad directory specified. Use the KiCad field to set it.");
        return;
    }

    logMsg("Parsing KiCad files from: " + dir);

    PipelineParams params;
    params.config   = buildConfigFromGUI();
    params.kicadDir = dir;
    params.flip     = g_chkFlip ? g_chkFlip->isChecked() : false;
    params.ignoreVia = g_chkIgnoreVia ? g_chkIgnoreVia->isChecked() : false;
    params.xOffset  = g_fldXOffset ? parseD(g_fldXOffset->getText()) : 0.0;
    params.yOffset  = g_fldYOffset ? parseD(g_fldYOffset->getText()) : 0.0;

    // Copper sub-layer visibility
    if (g_canvas) {
        auto& lay = g_canvas->layers();
        bool isFlipped = g_pipelineData.flipped || params.flip;
        auto& sub = isFlipped ? lay.copperBottomSub : lay.copperTopSub;
        params.copperVis.traces  = sub.traces;
        params.copperVis.pads    = sub.pads;
        params.copperVis.regions = sub.regions;
    }

    g_pipelineData = parsePipelineData(params, [](const std::string& msg) {
        logMsg(msg);
    });

    if (g_pipelineData.valid) {
        logMsg("Parsed OK. Board: " + dblStr(g_pipelineData.boardW, 1)
             + " x " + dblStr(g_pipelineData.boardH, 1) + " mm");
        // Generate isolation contours for preview
        if (!g_pipelineData.clearance.empty()) {
            Config cfg = buildConfigFromGUI();
            g_pipelineData.contours = generateToolpath(g_pipelineData.clearance, cfg);
            logMsg("Isolation preview: " + intStr((int)g_pipelineData.contours.size()) + " contours");
        }
        updateCanvasFromPipelineData();
    } else {
        logMsg("Failed to parse KiCad files.");
    }
}

static DWORD WINAPI generateThread(LPVOID) {
    std::string kicadDir   = g_fldKicadDir   ? g_fldKicadDir->getText()   : "";
    std::string outputFile = g_fldOutputFile  ? g_fldOutputFile->getText() : "";

    if (kicadDir.empty())   { logMsg("Error: KiCad directory not set.");  goto done; }
    if (outputFile.empty()) { logMsg("Error: Output file not set.");      goto done; }

    {
        PipelineParams params;
        params.config       = buildConfigFromGUI();
        params.kicadDir     = kicadDir;
        params.outputPath   = outputFile;
        params.flip         = g_chkFlip     ? g_chkFlip->isChecked()     : false;
        params.ignoreVia    = g_chkIgnoreVia? g_chkIgnoreVia->isChecked(): false;
        params.xOffset      = g_fldXOffset  ? parseD(g_fldXOffset->getText()) : 0.0;
        params.yOffset      = g_fldYOffset  ? parseD(g_fldYOffset->getText()) : 0.0;

        // Derive generation flags from layer visibility ("co widzimy to generujemy")
        if (g_canvas) {
            auto& lay = g_canvas->layers();
            params.generateIsolation = lay.isolation;
            params.generateDrilling  = lay.drillsPTH || lay.drillsNPTH;
            params.generateCutout    = lay.cutout;

            // Collect disabled drill diameters from canvas filters
            auto collectDisabled = [&](const std::vector<DrillFilter>& filters) {
                for (auto& f : filters) {
                    if (!f.visible) {
                        char key[16];
                        std::snprintf(key, sizeof(key), "%.3f", f.diameter);
                        params.disabledDrillDiameters.insert(key);
                    }
                }
            };
            collectDisabled(g_canvas->drillFilterPTH());
            collectDisabled(g_canvas->drillFilterNPTH());

            // Copper sub-layer visibility
            bool isFlipped = params.flip;
            auto& sub = isFlipped ? lay.copperBottomSub : lay.copperTopSub;
            params.copperVis.traces  = sub.traces;
            params.copperVis.pads    = sub.pads;
            params.copperVis.regions = sub.regions;

            // Per-aperture pad filter
            auto* pgVec = isFlipped ? g_canvas->copperBottomPadGroups() : g_canvas->copperTopPadGroups();
            if (pgVec) {
                for (auto& pg : *pgVec) {
                    if (!pg.visible)
                        params.disabledPadApertures.insert(pg.apNum);
                }
            }
        }

        g_lastDebugPath.clear();
        if (g_chkDebug && g_chkDebug->isChecked()) {
            std::string dbg = outputFile;
            size_t dot = dbg.rfind('.');
            if (dot != std::string::npos) dbg = dbg.substr(0, dot);
            dbg += "_debug.bmp";
            params.debugPath = dbg;
            g_lastDebugPath = dbg;
        }

        logMsg("=== Starting GCode generation ===");
        logMsg("");

        PipelineResult result;
        bool ok = runPipeline(params, [](const std::string& msg) {
            logMsg(msg);
        }, &result);

        logMsg("");
        if (ok) {
            logMsg("=== Generation complete ===");
            g_pipelineData = result;
            updateCanvasFromPipelineData();
        } else {
            logMsg("=== Generation failed ===");
        }
    }

done:
    if (g_progressBar) {
        g_progressBar->setMarquee(false);
        g_progressBar->setProgress(0);
    }
    if (g_btnGenerate) EnableWindow(g_btnGenerate->getHandle(), TRUE);
    g_isRunning = false;
    return 0;
}

void doGenerate() {
    if (g_isRunning) return;

    // If no output file set, prompt user
    std::string outPath = g_fldOutputFile ? g_fldOutputFile->getText() : "";
    if (outPath.empty() && g_window) {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = g_window->getHandle();
        ofn.lpstrFilter = L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0";
        ofn.lpstrFile   = file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrTitle  = L"Save GCode";
        ofn.lpstrDefExt = L"gcode";
        ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&ofn)) {
            std::string path = StringUtils::wideToUtf8(file);
            if (g_fldOutputFile) g_fldOutputFile->setText(path.c_str());
        } else {
            return; // user cancelled
        }
    }

    g_isRunning = true;
    if (g_logArea) g_logArea->clear();
    saveSettings();
    if (g_btnGenerate) EnableWindow(g_btnGenerate->getHandle(), FALSE);
    if (g_progressBar) g_progressBar->setMarquee(true);
    CreateThread(NULL, 0, generateThread, NULL, 0, NULL);
}

void doExportGCode() {
    doGenerate();
}
