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

enum class ToolPresetKind {
    Isolation,
    Combo,
    Drill,
    Cutout
};

struct ToolPreset {
    std::string name        = "New Tool";
    double toolDiameter     = 0.2;      // mm (engraver tip width)
    double cutDepth         = 0.05;     // mm (engraving depth, positive)
    double safeHeight       = 5.0;      // mm (clearance above material)
    double feedRateXY       = 300.0;    // mm/min
    double feedRateZ        = 100.0;    // mm/min (plunge rate)
    double zDrill           = 2.0;      // mm (drill depth from top, positive)
    double drillDiameter    = 0.8;      // mm (spindle tool diameter for drilling/cutout)
    double drillFeed        = 60.0;     // mm/min (spindle feedrate)
    double overlap          = 0.40;     // isolation overlap ratio
    double offset           = 0.02;     // isolation safety offset [mm]
    double xOffset          = 0.0;      // board X offset [mm]
    double yOffset          = 0.0;      // board Y offset [mm]
    bool   flip             = false;    // mirror board
    bool   ignoreVia        = false;    // skip via holes
    bool   debugImage       = true;     // emit debug BMP
    bool   engraverSpindle  = false;    // M3 before isolation (motorized engraver)
    double drillDwell       = 0.0;      // dwell at drill bottom (seconds, 0=off)
    ToolPresetKind kind     = ToolPresetKind::Isolation;
};

// ════════════════════════════════════════════════════════════════════════════
// Layer Panel Item — maps listbox index to toggle action
// ════════════════════════════════════════════════════════════════════════════

struct LayerPanelItem {
    bool  isSection;        // true = non-clickable header
    bool* flag = nullptr;   // pointer to bool to toggle (null for sections)
};

extern std::vector<LayerPanelItem> g_layerItems;

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
extern CheckBox*      g_chkEngraverSpindle;
extern CheckBox*      g_chkUseArcs;

// Drill dwell field
extern InputField*    g_fldDrillDwell;

// Tool dropdown button
extern Button*        g_btnTool;
extern Button*        g_btnGenerate;

// Pipeline intermediate data (for canvas preview)
extern PipelineResult g_pipelineData;
extern volatile bool  g_isRunning;
extern std::string    g_lastDebugPath;

// Layer panel
extern HWND           g_hLayerPanel;

// Option flags (formerly checkboxes — now controlled via Options menu)
extern bool  g_optFlip;
extern bool  g_optIgnoreVia;
extern bool  g_optDebugImage;
extern bool  g_optEngraverSpindle;

// Menu bar handle (for CheckMenuItem sync)
extern HMENU g_hMenuBar;

// Menu command IDs (9000+ range — no collision with auto-assigned control IDs 1000-8999)
#define IDM_FILE_OPEN     9001
#define IDM_FILE_SAVEAS   9002
#define IDM_FILE_EXIT     9003
#define IDM_VIEW_RELOAD   9010
#define IDM_VIEW_FIT      9011
#define IDM_VIEW_RESET    9012
#define IDM_VIEW_GRID     9013
#define IDM_VIEW_ALLON    9014
#define IDM_VIEW_FOCUS    9015
#define IDM_OPT_FLIP      9020
#define IDM_OPT_NOVIAS    9021
#define IDM_OPT_DEBUG     9022
#define IDM_OPT_SPINDLE   9023
#define IDM_TOOLS_MANAGE  9030
#define IDM_HELP_ABOUT    9040

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

// Auto-refresh
void scheduleAutoRefresh(bool fullReparse = false);
void doRefreshIsolation();
void doRecomputeClearance();
extern bool g_needsReparse;

// Config builder
Config buildConfigFromGUI();

// Layer panel
void rebuildLayerPanel();

// Menu option checkmarks sync
void syncMenuOptionCheckmarks();

// Resize
void installResizeHandler();
void doResize(int w, int h);
