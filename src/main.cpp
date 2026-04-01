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

// ════════════════════════════════════════════════════════════════════════════
// Layout constants
// ════════════════════════════════════════════════════════════════════════════

static const int LX = 15;              // label x
static const int IX = 175;             // input x
static const int IW = 585;             // input width
static const int BX = 770;             // browse button x
static const int BW = 175;             // browse button width
static const int CH = 24;              // control height

// ════════════════════════════════════════════════════════════════════════════
// Global state
// ════════════════════════════════════════════════════════════════════════════

static SimpleWindow*  window         = nullptr;
static ConfigManager* settings       = nullptr;

static InputField*    fldKicadDir    = nullptr;
static InputField*    fldConfigFile  = nullptr;
static InputField*    fldOutputFile  = nullptr;
static InputField*    fldXOffset     = nullptr;
static InputField*    fldYOffset     = nullptr;

static CheckBox*      chkFlip        = nullptr;
static CheckBox*      chkIgnoreVia   = nullptr;
static CheckBox*      chkDebug       = nullptr;

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

static void saveSettings() {
    settings->setValue("kicad_dir",    fldKicadDir->getText());
    settings->setValue("config_file",  fldConfigFile->getText());
    settings->setValue("output_file",  fldOutputFile->getText());
    settings->setValue("x_offset",     fldXOffset->getText());
    settings->setValue("y_offset",     fldYOffset->getText());
    settings->setValue("flip",         chkFlip->isChecked()      ? "1" : "0");
    settings->setValue("ignore_via",   chkIgnoreVia->isChecked() ? "1" : "0");
    settings->setValue("debug_image",  chkDebug->isChecked()     ? "1" : "0");
}

static void loadSettings() {
    fldKicadDir->setText(
        settings->getValue("kicad_dir", "").c_str());
    fldConfigFile->setText(
        settings->getValue("config_file", "").c_str());
    fldOutputFile->setText(
        settings->getValue("output_file", "").c_str());
    fldXOffset->setText(
        settings->getValue("x_offset", "").c_str());
    fldYOffset->setText(
        settings->getValue("y_offset", "").c_str());
    chkFlip->setChecked(
        settings->getValue("flip", "0") == "1");
    chkIgnoreVia->setChecked(
        settings->getValue("ignore_via", "0") == "1");
    chkDebug->setChecked(
        settings->getValue("debug_image", "1") == "1");
}

// ════════════════════════════════════════════════════════════════════════════
// Native pipeline execution (runs on background thread)
// ════════════════════════════════════════════════════════════════════════════

static void doGenerate() {
    std::string kicadDir   = fldKicadDir->getText();
    std::string configFile = fldConfigFile->getText();
    std::string outputFile = fldOutputFile->getText();
    std::string xOffStr    = fldXOffset->getText();
    std::string yOffStr    = fldYOffset->getText();
    bool flip              = chkFlip->isChecked();
    bool ignoreVia         = chkIgnoreVia->isChecked();
    bool debugImg          = chkDebug->isChecked();

    // Validate
    if (kicadDir.empty())   { logMsg("Error: KiCad directory not selected.");   return; }
    if (configFile.empty()) { logMsg("Error: Config JSON not selected.");       return; }
    if (outputFile.empty()) { logMsg("Error: Output file not selected.");       return; }

    PipelineParams params;
    params.configPath = configFile;
    params.kicadDir   = kicadDir;
    params.outputPath = outputFile;
    params.flip       = flip;
    params.ignoreVia  = ignoreVia;

    if (!xOffStr.empty()) {
        try { params.xOffset = std::stod(xOffStr); } catch (...) {}
    }
    if (!yOffStr.empty()) {
        try { params.yOffset = std::stod(yOffStr); } catch (...) {}
    }

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

// ════════════════════════════════════════════════════════════════════════════
// Application lifecycle
// ════════════════════════════════════════════════════════════════════════════

void init() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

void setup() {
    window = new SimpleWindow(960, 690, "gerber2gcode", 0);
    window->init();
    window->setBackgroundColor(RGB(45, 45, 54));
    window->setTextColor(RGB(220, 220, 230));

    settings = new ConfigManager("gerber2gcode.ini");

    // ── Title ────────────────────────────────────────────────────────────

    Label* title = new Label(LX, 10, 930, 28,
        L"gerber2gcode \u2014 Laser PCB GCode Generator");
    window->add(title);
    title->setFont(L"Segoe UI", 16, true);
    title->setTextColor(RGB(230, 235, 243));
    title->setBackColor(RGB(45, 45, 54));

    // ── Row 1: KiCad Directory ───────────────────────────────────────────

    Label* lblKicad = new Label(LX, 52, 155, 20, L"KiCad Directory:");
    window->add(lblKicad);
    styleLabel(lblKicad);

    fldKicadDir = new InputField(IX, 50, IW, CH);
    window->add(fldKicadDir);

    Button* btnBrowseKicad = new Button(BX, 50, BW, CH,
        "Browse...", [](Button*) {
            std::string p = browseFolderUTF8(
                L"Select KiCad Gerber output directory");
            if (!p.empty()) fldKicadDir->setText(p.c_str());
        });
    window->add(btnBrowseKicad);
    styleBrowseButton(btnBrowseKicad);

    // ── Row 2: Config JSON ───────────────────────────────────────────────

    Label* lblConfig = new Label(LX, 82, 155, 20, L"Config JSON:");
    window->add(lblConfig);
    styleLabel(lblConfig);

    fldConfigFile = new InputField(IX, 80, IW, CH);
    window->add(fldConfigFile);

    Button* btnBrowseConfig = new Button(BX, 80, BW, CH,
        "Browse...", [](Button*) {
            std::string p = openFileDialogUTF8(
                L"JSON (*.json)\0*.json\0All (*.*)\0*.*\0",
                L"Select config.json");
            if (!p.empty()) fldConfigFile->setText(p.c_str());
        });
    window->add(btnBrowseConfig);
    styleBrowseButton(btnBrowseConfig);

    // ── Row 3: Output GCode ──────────────────────────────────────────────

    Label* lblOutput = new Label(LX, 112, 155, 20, L"Output GCode:");
    window->add(lblOutput);
    styleLabel(lblOutput);

    fldOutputFile = new InputField(IX, 110, IW, CH);
    window->add(fldOutputFile);

    Button* btnBrowseOutput = new Button(BX, 110, BW, CH,
        "Save As...", [](Button*) {
            std::string p = saveFileDialogUTF8(
                L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                L"Save GCode output", L"gcode");
            if (!p.empty()) fldOutputFile->setText(p.c_str());
        });
    window->add(btnBrowseOutput);
    styleBrowseButton(btnBrowseOutput);

    // ── Row 4: Options ───────────────────────────────────────────────────

    int oy = 148;

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

    btnGenerate = new Button(IX, 185, IW, 38,
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

    progressBar = new ProgressBar(IX, 228, IW, 5);
    window->add(progressBar);
    progressBar->setColor(RGB(0, 180, 80));

    // ── Log area ─────────────────────────────────────────────────────────

    logArea = new TextArea(LX, 242, 930, 380);
    window->add(logArea);
    logArea->setFont(L"Consolas", 10, false);
    logArea->setTextColor(RGB(185, 195, 205));
    logArea->setBackColor(RGB(22, 22, 28));

    // ── Bottom buttons ───────────────────────────────────────────────────

    btnOpenFolder = new Button(LX, 632, 175, 30,
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

    btnPreview = new Button(200, 632, 185, 30,
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

    logMsg("gerber2gcode - Native Laser PCB GCode Generator");
    logMsg("Converts KiCad Gerber/drill files to Klipper GCode.");
    logMsg("");
    logMsg("1. Select KiCad Gerber output directory (with .gbr/.drl files)");
    logMsg("2. Select config.json (machine/cam/job parameters)");
    logMsg("3. Choose output .gcode file path");
    logMsg("4. Click Generate GCode");
}

void loop() {
    // No periodic work needed
}
