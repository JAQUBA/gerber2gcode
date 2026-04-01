// gerber2gcode — Native Laser PCB GCode Generator
// Converts KiCad Gerber/drill files to Klipper GCode for laser etching and drilling.
// All processing done natively in C++ (Gerber parser, Clipper2 geometry, toolpath gen).

#include <Core.h>
#include <UI/SimpleWindow/SimpleWindow.h>
#include <UI/Label/Label.h>
#include <UI/Button/Button.h>
#include <UI/InputField/InputField.h>
#include <UI/CheckBox/CheckBox.h>
#include <UI/TextArea/TextArea.h>
#include <UI/ProgressBar/ProgressBar.h>
#include <Util/ConfigManager.h>
#include <Util/StringUtils.h>

#include "Pipeline/Pipeline.h"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <cstdlib>

// ════════════════════════════════════════════════════════════════════════════
// Layout constants
// ════════════════════════════════════════════════════════════════════════════

static const int LX = 15;              // left margin
static const int IX = 175;             // file input x
static const int IW = 585;             // file input width
static const int BX = 770;             // browse button x
static const int BW = 175;             // browse button width
static const int CH = 24;              // control height
static const int CFH = 22;             // config field height

// Config field column layout (5-column grid)
static const int CFG_LBL_W   = 82;     // label width
static const int CFG_INP_W   = 62;     // input width
static const int CFG_GAP     = 4;      // gap label→input
static const int CFG_SPACING = 182;    // column spacing

static int cfgLabelX(int col)  { return LX + col * CFG_SPACING; }
static int cfgInputX(int col)  { return LX + col * CFG_SPACING + CFG_LBL_W + CFG_GAP; }

// ════════════════════════════════════════════════════════════════════════════
// Global state
// ════════════════════════════════════════════════════════════════════════════

static SimpleWindow*  window         = nullptr;
static ConfigManager* settings       = nullptr;

// File paths
static InputField*    fldKicadDir    = nullptr;
static InputField*    fldOutputFile  = nullptr;

// Machine config fields
static InputField*    cfgXSize       = nullptr;
static InputField*    cfgYSize       = nullptr;
static InputField*    cfgMoveFeed    = nullptr;
// Engraver
static InputField*    cfgEngZTravel  = nullptr;
static InputField*    cfgEngZCut     = nullptr;
static InputField*    cfgEngTipW     = nullptr;
// Drill
static InputField*    cfgZHome       = nullptr;
static InputField*    cfgZPreDrill   = nullptr;
static InputField*    cfgZDrill      = nullptr;
static InputField*    cfgToolDia     = nullptr;

// CAM config fields
static InputField*    cfgOverlap     = nullptr;
static InputField*    cfgOffset      = nullptr;

// Job config fields
static InputField*    cfgEngFeed     = nullptr;
static InputField*    cfgSpindlePower = nullptr;
static InputField*    cfgSpindleFeed = nullptr;

// Options
static InputField*    fldXOffset     = nullptr;
static InputField*    fldYOffset     = nullptr;
static CheckBox*      chkFlip        = nullptr;
static CheckBox*      chkIgnoreVia   = nullptr;
static CheckBox*      chkDebug       = nullptr;

// Output
static TextArea*      logArea        = nullptr;
static ProgressBar*   progressBar    = nullptr;
static Button*        btnGenerate    = nullptr;
static Button*        btnOpenFolder  = nullptr;
static Button*        btnPreview     = nullptr;

static volatile bool  g_isRunning    = false;
static std::string    g_lastDebugPath;

// ════════════════════════════════════════════════════════════════════════════
// File / folder dialog helpers
// ════════════════════════════════════════════════════════════════════════════

static std::string openFileDialogUTF8(
    const wchar_t* filter, const wchar_t* title)
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = window ? window->getHandle() : NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        return StringUtils::wideToUtf8(file);
    return "";
}

static std::string saveFileDialogUTF8(
    const wchar_t* filter, const wchar_t* title, const wchar_t* defExt)
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = window ? window->getHandle() : NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn))
        return StringUtils::wideToUtf8(file);
    return "";
}

static std::string browseFolderUTF8(const wchar_t* title) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = window ? window->getHandle() : NULL;
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return StringUtils::wideToUtf8(path);
        }
        CoTaskMemFree(pidl);
    }
    return "";
}

// ════════════════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════════════════

static void logMsg(const std::string& msg) {
    logArea->append(msg + "\r\n");
}

static Config buildConfigFromGUI() {
    Config cfg;
    auto d = [](const std::string& s) -> double {
        return s.empty() ? 0.0 : std::strtod(s.c_str(), nullptr);
    };
    cfg.machine.x_size              = d(cfgXSize->getText());
    cfg.machine.y_size              = d(cfgYSize->getText());
    cfg.machine.move_feedrate       = d(cfgMoveFeed->getText());
    cfg.machine.engraver_z_travel   = d(cfgEngZTravel->getText());
    cfg.machine.engraver_z_cut      = d(cfgEngZCut->getText());
    cfg.machine.engraver_tip_width  = d(cfgEngTipW->getText());
    cfg.machine.spindle_z_home      = d(cfgZHome->getText());
    cfg.machine.spindle_z_pre_drill = d(cfgZPreDrill->getText());
    cfg.machine.spindle_z_drill     = d(cfgZDrill->getText());
    cfg.machine.spindle_tool_diameter = d(cfgToolDia->getText());
    cfg.machine.x_offset            = 0;
    cfg.machine.y_offset            = 0;
    cfg.cam.overlap  = d(cfgOverlap->getText());
    cfg.cam.offset   = d(cfgOffset->getText());
    cfg.job.engraver_feedrate = d(cfgEngFeed->getText());
    cfg.job.spindle_power    = d(cfgSpindlePower->getText());
    cfg.job.spindle_feedrate = d(cfgSpindleFeed->getText());
    return cfg;
}

static void saveSettings() {
    settings->setValue("kicad_dir",    fldKicadDir->getText());
    settings->setValue("output_file",  fldOutputFile->getText());
    // Machine
    settings->setValue("cfg_x_size",       cfgXSize->getText());
    settings->setValue("cfg_y_size",       cfgYSize->getText());
    settings->setValue("cfg_move_feed",    cfgMoveFeed->getText());
    // Engraver
    settings->setValue("cfg_eng_z_travel", cfgEngZTravel->getText());
    settings->setValue("cfg_eng_z_cut",    cfgEngZCut->getText());
    settings->setValue("cfg_eng_tip_w",    cfgEngTipW->getText());
    // Drill
    settings->setValue("cfg_z_home",       cfgZHome->getText());
    settings->setValue("cfg_z_pre_drill",  cfgZPreDrill->getText());
    settings->setValue("cfg_z_drill",      cfgZDrill->getText());
    settings->setValue("cfg_tool_dia",     cfgToolDia->getText());
    // CAM
    settings->setValue("cfg_overlap",      cfgOverlap->getText());
    settings->setValue("cfg_offset",       cfgOffset->getText());
    // Job
    settings->setValue("cfg_eng_feed",     cfgEngFeed->getText());
    settings->setValue("cfg_spindle_power", cfgSpindlePower->getText());
    settings->setValue("cfg_spindle_feed", cfgSpindleFeed->getText());
    // Options
    settings->setValue("x_offset",     fldXOffset->getText());
    settings->setValue("y_offset",     fldYOffset->getText());
    settings->setValue("flip",         chkFlip->isChecked()      ? "1" : "0");
    settings->setValue("ignore_via",   chkIgnoreVia->isChecked() ? "1" : "0");
    settings->setValue("debug_image",  chkDebug->isChecked()     ? "1" : "0");
}

static void loadSettings() {
    fldKicadDir->setText(settings->getValue("kicad_dir", "").c_str());
    fldOutputFile->setText(settings->getValue("output_file", "").c_str());
    // Machine
    cfgXSize->setText(settings->getValue("cfg_x_size", "250").c_str());
    cfgYSize->setText(settings->getValue("cfg_y_size", "200").c_str());
    cfgMoveFeed->setText(settings->getValue("cfg_move_feed", "2400").c_str());
    // Engraver
    cfgEngZTravel->setText(settings->getValue("cfg_eng_z_travel", "5").c_str());
    cfgEngZCut->setText(settings->getValue("cfg_eng_z_cut", "-0.05").c_str());
    cfgEngTipW->setText(settings->getValue("cfg_eng_tip_w", "0.1").c_str());
    // Drill
    cfgZHome->setText(settings->getValue("cfg_z_home", "30").c_str());
    cfgZPreDrill->setText(settings->getValue("cfg_z_pre_drill", "3").c_str());
    cfgZDrill->setText(settings->getValue("cfg_z_drill", "-2").c_str());
    cfgToolDia->setText(settings->getValue("cfg_tool_dia", "0.8").c_str());
    // CAM
    cfgOverlap->setText(settings->getValue("cfg_overlap", "0.4").c_str());
    cfgOffset->setText(settings->getValue("cfg_offset", "0.02").c_str());
    // Job
    cfgEngFeed->setText(settings->getValue("cfg_eng_feed", "300").c_str());
    cfgSpindlePower->setText(settings->getValue("cfg_spindle_power", "255").c_str());
    cfgSpindleFeed->setText(settings->getValue("cfg_spindle_feed", "60").c_str());
    // Options
    fldXOffset->setText(settings->getValue("x_offset", "0").c_str());
    fldYOffset->setText(settings->getValue("y_offset", "0").c_str());
    chkFlip->setChecked(settings->getValue("flip", "0") == "1");
    chkIgnoreVia->setChecked(settings->getValue("ignore_via", "0") == "1");
    chkDebug->setChecked(
        settings->getValue("debug_image", "1") == "1");
}

// ════════════════════════════════════════════════════════════════════════════
// Native pipeline execution (runs on background thread)
// ════════════════════════════════════════════════════════════════════════════

static void doGenerate() {
    std::string kicadDir   = fldKicadDir->getText();
    std::string outputFile = fldOutputFile->getText();
    bool flip              = chkFlip->isChecked();
    bool ignoreVia         = chkIgnoreVia->isChecked();
    bool debugImg          = chkDebug->isChecked();

    if (kicadDir.empty())   { logMsg("Error: KiCad directory not selected.");   return; }
    if (outputFile.empty()) { logMsg("Error: Output file not selected.");       return; }

    PipelineParams params;
    params.config    = buildConfigFromGUI();
    params.kicadDir  = kicadDir;
    params.outputPath = outputFile;
    params.flip      = flip;
    params.ignoreVia = ignoreVia;
    params.xOffset   = std::strtod(fldXOffset->getText().c_str(), nullptr);
    params.yOffset   = std::strtod(fldYOffset->getText().c_str(), nullptr);

    g_lastDebugPath.clear();
    if (debugImg) {
        std::string dbg = outputFile;
        size_t dot = dbg.rfind('.');
        if (dot != std::string::npos)
            dbg = dbg.substr(0, dot);
        dbg += "_debug.bmp";
        params.debugPath = dbg;
        g_lastDebugPath = dbg;
    }

    logMsg("=== Starting native GCode generation ===");
    logMsg("");

    bool ok = runPipeline(params, [](const std::string& msg) {
        logMsg(msg);
    });

    logMsg("");
    if (ok)
        logMsg("=== Generation complete ===");
    else
        logMsg("=== Generation failed ===");
}

static DWORD WINAPI generateThread(LPVOID) {
    doGenerate();
    progressBar->setMarquee(false);
    progressBar->setProgress(0);
    EnableWindow(btnGenerate->getHandle(), TRUE);
    g_isRunning = false;
    return 0;
}

// ════════════════════════════════════════════════════════════════════════════
// Button styling helper
// ════════════════════════════════════════════════════════════════════════════

static void styleBrowseButton(Button* btn) {
    btn->setBackColor(RGB(58, 58, 70));
    btn->setTextColor(RGB(200, 200, 210));
    btn->setHoverColor(RGB(72, 72, 88));
}

static void styleLabel(Label* lbl) {
    lbl->setFont(L"Segoe UI", 11);
    lbl->setTextColor(RGB(170, 175, 185));
    lbl->setBackColor(RGB(45, 45, 54));
}

// Helper: create a labeled config input field
static InputField* addCfgField(int col, int y, const wchar_t* label) {
    Label* lbl = new Label(cfgLabelX(col), y + 3, CFG_LBL_W, 16, label);
    window->add(lbl);
    lbl->setFont(L"Segoe UI", 9);
    lbl->setTextColor(RGB(140, 148, 160));
    lbl->setBackColor(RGB(45, 45, 54));
    InputField* fld = new InputField(cfgInputX(col), y, CFG_INP_W, CFH);
    window->add(fld);
    return fld;
}

// Helper: section header label
static void addSectionLabel(int y, const wchar_t* text) {
    Label* lbl = new Label(LX, y, 930, 18, text);
    window->add(lbl);
    lbl->setFont(L"Segoe UI", 10, true);
    lbl->setTextColor(RGB(90, 165, 235));
    lbl->setBackColor(RGB(45, 45, 54));
}

// ════════════════════════════════════════════════════════════════════════════
// Application lifecycle
// ════════════════════════════════════════════════════════════════════════════

void init() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

void setup() {
    window = new SimpleWindow(960, 760, "gerber2gcode", 0);
    window->init();
    window->setBackgroundColor(RGB(45, 45, 54));
    window->setTextColor(RGB(220, 220, 230));

    settings = new ConfigManager("gerber2gcode.ini");

    // ── Title ────────────────────────────────────────────────────────────

    Label* title = new Label(LX, 10, 930, 28,
        L"gerber2gcode \u2014 CNC PCB Isolation Router");
    window->add(title);
    title->setFont(L"Segoe UI", 16, true);
    title->setTextColor(RGB(230, 235, 243));
    title->setBackColor(RGB(45, 45, 54));

    // ── Row 1: KiCad Directory ───────────────────────────────────────────

    Label* lblKicad = new Label(LX, 50, 155, 20, L"KiCad Directory:");
    window->add(lblKicad);
    styleLabel(lblKicad);

    fldKicadDir = new InputField(IX, 48, IW, CH);
    window->add(fldKicadDir);

    Button* btnBrowseKicad = new Button(BX, 48, BW, CH,
        "Browse...", [](Button*) {
            std::string p = browseFolderUTF8(
                L"Select KiCad Gerber output directory");
            if (!p.empty()) fldKicadDir->setText(p.c_str());
        });
    window->add(btnBrowseKicad);
    styleBrowseButton(btnBrowseKicad);

    // ── Row 2: Output GCode ──────────────────────────────────────────────

    Label* lblOutput = new Label(LX, 80, 155, 20, L"Output GCode:");
    window->add(lblOutput);
    styleLabel(lblOutput);

    fldOutputFile = new InputField(IX, 78, IW, CH);
    window->add(fldOutputFile);

    Button* btnBrowseOutput = new Button(BX, 78, BW, CH,
        "Save As...", [](Button*) {
            std::string p = saveFileDialogUTF8(
                L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                L"Save GCode output", L"gcode");
            if (!p.empty()) fldOutputFile->setText(p.c_str());
        });
    window->add(btnBrowseOutput);
    styleBrowseButton(btnBrowseOutput);

    // ── Machine Configuration ────────────────────────────────────────────

    addSectionLabel(112, L"Machine");

    cfgXSize    = addCfgField(0, 132, L"X Size");
    cfgYSize    = addCfgField(1, 132, L"Y Size");
    cfgMoveFeed = addCfgField(2, 132, L"Move Feed");

    // ── Engraver ─────────────────────────────────────────────────────────

    addSectionLabel(160, L"Engraver (V-bit)");

    cfgEngZTravel = addCfgField(0, 180, L"Z Travel");
    cfgEngZCut    = addCfgField(1, 180, L"Z Cut");
    cfgEngTipW    = addCfgField(2, 180, L"Tip Width");
    cfgEngFeed    = addCfgField(3, 180, L"Eng Feed");

    // ── Drill ────────────────────────────────────────────────────────────

    addSectionLabel(208, L"Drill");

    cfgZHome     = addCfgField(0, 228, L"Z Home");
    cfgZPreDrill = addCfgField(1, 228, L"Z Pre-drill");
    cfgZDrill    = addCfgField(2, 228, L"Z Drill");
    cfgToolDia   = addCfgField(3, 228, L"Tool Dia");

    // ── CAM ──────────────────────────────────────────────────────────────

    addSectionLabel(256, L"CAM");

    cfgOverlap      = addCfgField(0, 276, L"Overlap");
    cfgOffset       = addCfgField(1, 276, L"Offset");
    cfgSpindlePower = addCfgField(2, 276, L"Spindle Pwr");
    cfgSpindleFeed  = addCfgField(3, 276, L"Spindle Feed");

    // ── Options ──────────────────────────────────────────────────────────

    int oy = 310;

    Label* lblXOff = new Label(LX, oy + 2, 95, 20, L"X Offset (mm):");
    window->add(lblXOff);
    lblXOff->setFont(L"Segoe UI", 10);
    lblXOff->setTextColor(RGB(170, 175, 185));
    lblXOff->setBackColor(RGB(45, 45, 54));

    fldXOffset = new InputField(115, oy, 55, CH);
    window->add(fldXOffset);

    Label* lblYOff = new Label(185, oy + 2, 95, 20, L"Y Offset (mm):");
    window->add(lblYOff);
    lblYOff->setFont(L"Segoe UI", 10);
    lblYOff->setTextColor(RGB(170, 175, 185));
    lblYOff->setBackColor(RGB(45, 45, 54));

    fldYOffset = new InputField(285, oy, 55, CH);
    window->add(fldYOffset);

    chkFlip      = new CheckBox(375, oy, 120, CH, "Flip (B_Cu)",  false);
    chkIgnoreVia = new CheckBox(505, oy, 120, CH, "Ignore Vias",  false);
    chkDebug     = new CheckBox(640, oy, 145, CH, "Debug Image",  true);
    window->add(chkFlip);
    window->add(chkIgnoreVia);
    window->add(chkDebug);

    // ── Generate button ──────────────────────────────────────────────────

    btnGenerate = new Button(IX, 348, IW, 38,
        "Generate GCode", [](Button*) {
            if (g_isRunning) return;
            g_isRunning = true;
            logArea->clear();
            saveSettings();
            EnableWindow(btnGenerate->getHandle(), FALSE);
            progressBar->setMarquee(true);
            CreateThread(NULL, 0, generateThread, NULL, 0, NULL);
        });
    window->add(btnGenerate);
    btnGenerate->setBackColor(RGB(0, 120, 60));
    btnGenerate->setTextColor(RGB(255, 255, 255));
    btnGenerate->setHoverColor(RGB(0, 155, 78));
    btnGenerate->setFont(L"Segoe UI", 12, true);

    // ── Progress bar ─────────────────────────────────────────────────────

    progressBar = new ProgressBar(IX, 390, IW, 5);
    window->add(progressBar);
    progressBar->setColor(RGB(0, 180, 80));

    // ── Log area ─────────────────────────────────────────────────────────

    logArea = new TextArea(LX, 404, 930, 310);
    window->add(logArea);
    logArea->setFont(L"Consolas", 10, false);
    logArea->setTextColor(RGB(185, 195, 205));
    logArea->setBackColor(RGB(22, 22, 28));

    // ── Bottom buttons ───────────────────────────────────────────────────

    btnOpenFolder = new Button(LX, 724, 175, 30,
        "Open Output Folder", [](Button*) {
            std::string path = fldOutputFile->getText();
            if (path.empty()) return;
            size_t sep = path.find_last_of("\\/");
            std::string folder =
                (sep != std::string::npos) ? path.substr(0, sep) : ".";
            ShellExecuteW(NULL, L"explore",
                StringUtils::utf8ToWide(folder).c_str(),
                NULL, NULL, SW_SHOW);
        });
    window->add(btnOpenFolder);
    styleBrowseButton(btnOpenFolder);

    btnPreview = new Button(200, 724, 185, 30,
        "Preview Debug Image", [](Button*) {
            if (g_lastDebugPath.empty()) {
                MessageBoxW(window->getHandle(),
                    L"No debug image generated yet.\n"
                    L"Enable 'Debug Image' and run Generate first.",
                    L"Preview", MB_ICONINFORMATION);
                return;
            }
            ShellExecuteW(NULL, L"open",
                StringUtils::utf8ToWide(g_lastDebugPath).c_str(),
                NULL, NULL, SW_SHOW);
        });
    window->add(btnPreview);
    styleBrowseButton(btnPreview);

    // ── Load saved settings ──────────────────────────────────────────────

    loadSettings();

    logMsg("gerber2gcode - CNC PCB Isolation Router");
    logMsg("Converts KiCad Gerber/drill files to GCode for V-bit engraving and drilling.");
    logMsg("");
    logMsg("1. Select KiCad Gerber output directory (with .gbr/.drl files)");
    logMsg("2. Configure engraver/drill/CAM parameters above");
    logMsg("3. Choose output .gcode file path");
    logMsg("4. Click Generate GCode");
}

void loop() {
    // No periodic work needed
}
