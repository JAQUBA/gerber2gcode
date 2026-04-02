#pragma once
// gerber2gcode — Application State & Shared Actions

#include <Core.h>
#include <UI/SimpleWindow/SimpleWindow.h>
#include <UI/Label/Label.h>
#include <UI/Button/Button.h>
#include <UI/InputField/InputField.h>
#include <UI/CheckBox/CheckBox.h>
#include <UI/TextArea/TextArea.h>
#include <UI/ProgressBar/ProgressBar.h>
#include <Util/ConfigManager.h>

#include "Config/Config.h"
#include "Pipeline/Pipeline.h"
#include "Canvas/PCBCanvas.h"

#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
// Tool Preset
// ════════════════════════════════════════════════════════════════════════════

struct ToolPreset {
    std::string name        = "New Tool";
    double toolDiameter     = 0.2;      // mm (engraver tip width)
    double cutDepth         = -0.05;    // mm (engraver Z cut)
    double safeHeight       = 5.0;      // mm (engraver Z travel)
    double feedRateXY       = 300.0;    // mm/min
    double feedRateZ        = 100.0;    // mm/min (plunge rate)
    double zDrill           = -2.0;     // mm (drill depth)
    double drillFeed        = 60.0;     // mm/min (spindle feedrate)
};

// ════════════════════════════════════════════════════════════════════════════
// Global State (extern declarations)
// ════════════════════════════════════════════════════════════════════════════

extern SimpleWindow*  g_window;
extern ConfigManager* g_settings;
extern ConfigManager* g_toolConfig;
extern PCBCanvas*     g_canvas;
extern TextArea*      g_logArea;
extern ProgressBar*   g_progressBar;

// Tool presets
extern std::vector<ToolPreset> g_toolPresets;
extern int                     g_activeToolIndex;

// Toolbar fields — set by AppUI, read by AppState
extern InputField*    g_fldKicadDir;
extern InputField*    g_fldOutputFile;
extern InputField*    g_fldToolDia;
extern InputField*    g_fldCutDepth;
extern InputField*    g_fldSafeHeight;
extern InputField*    g_fldFeedXY;
extern InputField*    g_fldFeedZ;
extern InputField*    g_fldOverlap;
extern InputField*    g_fldOffset;
extern InputField*    g_fldXOffset;
extern InputField*    g_fldYOffset;
extern InputField*    g_fldMaterial;
// Drill fields
extern InputField*    g_fldZDrill;
extern InputField*    g_fldDrillDia;
extern InputField*    g_fldDrillFeed;

// Checkboxes
extern CheckBox*      g_chkFlip;
extern CheckBox*      g_chkIgnoreVia;
extern CheckBox*      g_chkDebug;

// Tool dropdown button
extern Button*        g_btnTool;
extern Button*        g_btnGenerate;

// Pipeline intermediate data (for canvas preview)
extern PipelineResult g_pipelineData;
extern volatile bool  g_isRunning;
extern std::string    g_lastDebugPath;

// Layer panel
extern HWND           g_hLayerPanel;

// ════════════════════════════════════════════════════════════════════════════
// Functions
// ════════════════════════════════════════════════════════════════════════════

// Logging
void logMsg(const std::string& msg);
void logMsg(const wchar_t* msg);

// Settings
void loadSettings();
void saveSettings();

// Tool presets
void loadToolPresets();
void saveToolPresets();
void createDefaultToolPresets();
void applyActiveToolPreset();
void doSelectTool(int index);
void doShowToolPresets();
void showToolPopup();
void updateToolButtonText();

// Actions
void doLoadKicadDir();
void doGenerate();
void doExportGCode();

// Config builder
Config buildConfigFromGUI();

// Layer panel
void rebuildLayerPanel();

// Resize
void installResizeHandler();
void doResize(int w, int h);
