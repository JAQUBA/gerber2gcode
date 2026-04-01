// gerber2gcode — Laser PCB GCode Generator
// GUI wrapper for flatcum using JQB_WindowsLib
//
// Calls the Python flatcum CLI as a subprocess and displays output in real-time.

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

static InputField*    fldFlatcumCmd  = nullptr;
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

static std::string stripAnsiCodes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool esc = false;
    for (char c : text) {
        if (c == '\033')  { esc = true; }
        else if (esc)     { if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) esc = false; }
        else              { out += c; }
    }
    return out;
}

static void logMsg(const std::string& msg) {
    logArea->append(msg + "\r\n");
}

static void saveSettings() {
    settings->setValue("flatcum_cmd",  fldFlatcumCmd->getText());
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
    fldFlatcumCmd->setText(
        settings->getValue("flatcum_cmd", "python -m flatcum").c_str());
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
// Subprocess execution (runs on background thread)
// ════════════════════════════════════════════════════════════════════════════

static void doGenerate() {
    // Snapshot UI values (thread-safe via SendMessage inside getText)
    std::string cmd        = fldFlatcumCmd->getText();
    std::string kicadDir   = fldKicadDir->getText();
    std::string configFile = fldConfigFile->getText();
    std::string outputFile = fldOutputFile->getText();
    std::string xOffset    = fldXOffset->getText();
    std::string yOffset    = fldYOffset->getText();
    bool flip              = chkFlip->isChecked();
    bool ignoreVia         = chkIgnoreVia->isChecked();
    bool debugImg          = chkDebug->isChecked();

    // ── Validate ──
    if (cmd.empty())        { logMsg("Error: Flatcum command not set.");        return; }
    if (kicadDir.empty())   { logMsg("Error: KiCad directory not selected.");   return; }
    if (configFile.empty()) { logMsg("Error: Config JSON not selected.");       return; }
    if (outputFile.empty()) { logMsg("Error: Output file not selected.");       return; }

    // ── Build command line ──
    std::string cmdLine = cmd + " convert-kicad"
        " \"" + configFile + "\""
        " \"" + kicadDir   + "\""
        " -o \"" + outputFile + "\""
        " -v";

    if (flip)      cmdLine += " --flip";
    if (ignoreVia) cmdLine += " --ignore-via";

    g_lastDebugPath.clear();
    if (debugImg) {
        std::string dbg = outputFile;
        size_t dot = dbg.rfind('.');
        if (dot != std::string::npos)
            dbg = dbg.substr(0, dot);
        dbg += "_debug.png";
        cmdLine += " --debug \"" + dbg + "\"";
        g_lastDebugPath = dbg;
    }

    if (!xOffset.empty()) cmdLine += " --x-offset " + xOffset;
    if (!yOffset.empty()) cmdLine += " --y-offset " + yOffset;

    logMsg("$ " + cmdLine);
    logMsg("");

    // ── Create pipe for stdout+stderr capture ──
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        logMsg("Error: CreatePipe failed.");
        return;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    // ── Launch process ──
    STARTUPINFOW si = {};
    si.cb         = sizeof(si);
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};
    std::wstring wCmd = StringUtils::utf8ToWide(cmdLine);
    std::vector<wchar_t> cmdBuf(wCmd.begin(), wCmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        NULL, cmdBuf.data(), NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) {
        DWORD err = GetLastError();
        logMsg("Error: Could not start process (code "
               + std::to_string(err) + ").");
        logMsg("Check that the Flatcum command is correct and Python is on PATH.");
        CloseHandle(hRead);
        return;
    }

    // ── Stream output to log ──
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL)
           && bytesRead > 0)
    {
        buf[bytesRead] = '\0';
        std::string text = stripAnsiCodes(std::string(buf, bytesRead));
        logArea->append(text);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    logMsg("");
    if (exitCode == 0)
        logMsg("=== Done! ===");
    else
        logMsg("=== Failed (exit code "
               + std::to_string(exitCode) + ") ===");
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
// Application lifecycle
// ════════════════════════════════════════════════════════════════════════════

void init() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

void setup() {
    window = new SimpleWindow(960, 720, "gerber2gcode", 0);
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

    // ── Row 1: Flatcum Command ───────────────────────────────────────────

    Label* lblCmd = new Label(LX, 52, 155, 20, L"Flatcum Command:");
    window->add(lblCmd);
    lblCmd->setFont(L"Segoe UI", 11);
    lblCmd->setTextColor(RGB(170, 175, 185));
    lblCmd->setBackColor(RGB(45, 45, 54));

    fldFlatcumCmd = new InputField(IX, 50, IW + BW + 10, CH,
        "python -m flatcum");
    window->add(fldFlatcumCmd);

    // ── Row 2: KiCad Directory ───────────────────────────────────────────

    Label* lblKicad = new Label(LX, 82, 155, 20, L"KiCad Directory:");
    window->add(lblKicad);
    lblKicad->setFont(L"Segoe UI", 11);
    lblKicad->setTextColor(RGB(170, 175, 185));
    lblKicad->setBackColor(RGB(45, 45, 54));

    fldKicadDir = new InputField(IX, 80, IW, CH);
    window->add(fldKicadDir);

    Button* btnBrowseKicad = new Button(BX, 80, BW, CH,
        "Browse...", [](Button*) {
            std::string p = browseFolderUTF8(
                L"Select KiCad Gerber output directory");
            if (!p.empty()) fldKicadDir->setText(p.c_str());
        });
    window->add(btnBrowseKicad);
    btnBrowseKicad->setBackColor(RGB(58, 58, 70));
    btnBrowseKicad->setTextColor(RGB(200, 200, 210));
    btnBrowseKicad->setHoverColor(RGB(72, 72, 88));

    // ── Row 3: Config JSON ───────────────────────────────────────────────

    Label* lblConfig = new Label(LX, 112, 155, 20, L"Config JSON:");
    window->add(lblConfig);
    lblConfig->setFont(L"Segoe UI", 11);
    lblConfig->setTextColor(RGB(170, 175, 185));
    lblConfig->setBackColor(RGB(45, 45, 54));

    fldConfigFile = new InputField(IX, 110, IW, CH);
    window->add(fldConfigFile);

    Button* btnBrowseConfig = new Button(BX, 110, BW, CH,
        "Browse...", [](Button*) {
            std::string p = openFileDialogUTF8(
                L"JSON (*.json)\0*.json\0All (*.*)\0*.*\0",
                L"Select config.json");
            if (!p.empty()) fldConfigFile->setText(p.c_str());
        });
    window->add(btnBrowseConfig);
    btnBrowseConfig->setBackColor(RGB(58, 58, 70));
    btnBrowseConfig->setTextColor(RGB(200, 200, 210));
    btnBrowseConfig->setHoverColor(RGB(72, 72, 88));

    // ── Row 4: Output GCode ──────────────────────────────────────────────

    Label* lblOutput = new Label(LX, 142, 155, 20, L"Output GCode:");
    window->add(lblOutput);
    lblOutput->setFont(L"Segoe UI", 11);
    lblOutput->setTextColor(RGB(170, 175, 185));
    lblOutput->setBackColor(RGB(45, 45, 54));

    fldOutputFile = new InputField(IX, 140, IW, CH);
    window->add(fldOutputFile);

    Button* btnBrowseOutput = new Button(BX, 140, BW, CH,
        "Save As...", [](Button*) {
            std::string p = saveFileDialogUTF8(
                L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                L"Save GCode output", L"gcode");
            if (!p.empty()) fldOutputFile->setText(p.c_str());
        });
    window->add(btnBrowseOutput);
    btnBrowseOutput->setBackColor(RGB(58, 58, 70));
    btnBrowseOutput->setTextColor(RGB(200, 200, 210));
    btnBrowseOutput->setHoverColor(RGB(72, 72, 88));

    // ── Row 5: Options ───────────────────────────────────────────────────

    int oy = 178;

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

    btnGenerate = new Button(IX, 215, IW, 38,
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

    progressBar = new ProgressBar(IX, 258, IW, 5);
    window->add(progressBar);
    progressBar->setColor(RGB(0, 180, 80));

    // ── Log area ─────────────────────────────────────────────────────────

    logArea = new TextArea(LX, 272, 930, 383);
    window->add(logArea);
    logArea->setFont(L"Consolas", 10, false);
    logArea->setTextColor(RGB(185, 195, 205));
    logArea->setBackColor(RGB(22, 22, 28));

    // ── Bottom buttons ───────────────────────────────────────────────────

    btnOpenFolder = new Button(LX, 664, 175, 30,
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
    btnOpenFolder->setBackColor(RGB(58, 58, 70));
    btnOpenFolder->setTextColor(RGB(200, 200, 210));
    btnOpenFolder->setHoverColor(RGB(72, 72, 88));

    btnPreview = new Button(200, 664, 185, 30,
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
    btnPreview->setBackColor(RGB(58, 58, 70));
    btnPreview->setTextColor(RGB(200, 200, 210));
    btnPreview->setHoverColor(RGB(72, 72, 88));

    // ── Load saved settings ──────────────────────────────────────────────

    loadSettings();

    logMsg("Ready. Configure paths above, then click Generate GCode.");
    logMsg("");
    logMsg("Flatcum command examples:");
    logMsg("  python -m flatcum          (if flatcum installed via pip)");
    logMsg("  uv run flatcum             (if using uv in flatcum project)");
    logMsg("  uv run --project C:\\path\\to\\flatcum flatcum");
}

void loop() {
    // No periodic work needed
}
