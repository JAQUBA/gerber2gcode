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

Button*        g_btnTool       = nullptr;
Button*        g_btnGenerate   = nullptr;

PipelineResult g_pipelineData;
volatile bool  g_isRunning     = false;
std::string    g_lastDebugPath;

HWND           g_hLayerPanel   = nullptr;

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
    if (g_fldCutDepth)   g_fldCutDepth->setText(s.getValue("z_cut", "-0.05").c_str());
    if (g_fldSafeHeight) g_fldSafeHeight->setText(s.getValue("z_travel", "5").c_str());
    if (g_fldFeedXY)     g_fldFeedXY->setText(s.getValue("feed_xy", "300").c_str());
    if (g_fldFeedZ)      g_fldFeedZ->setText(s.getValue("feed_z", "100").c_str());
    if (g_fldOverlap)    g_fldOverlap->setText(s.getValue("overlap", "0.4").c_str());
    if (g_fldOffset)     g_fldOffset->setText(s.getValue("offset", "0.02").c_str());
    if (g_fldMaterial)   g_fldMaterial->setText(s.getValue("material", "1.5").c_str());
    if (g_fldZDrill)     g_fldZDrill->setText(s.getValue("z_drill", "-2").c_str());
    if (g_fldDrillDia)   g_fldDrillDia->setText(s.getValue("drill_dia", "0.8").c_str());
    if (g_fldDrillFeed)  g_fldDrillFeed->setText(s.getValue("drill_feed", "60").c_str());
    if (g_fldXOffset)    g_fldXOffset->setText(s.getValue("x_offset", "0").c_str());
    if (g_fldYOffset)    g_fldYOffset->setText(s.getValue("y_offset", "0").c_str());

    if (g_chkFlip)       g_chkFlip->setChecked(s.getValue("flip", "0") == "1");
    if (g_chkIgnoreVia)  g_chkIgnoreVia->setChecked(s.getValue("ignore_via", "0") == "1");
    if (g_chkDebug)      g_chkDebug->setChecked(s.getValue("debug_image", "1") == "1");

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
    g_toolPresets.push_back({"V-bit 20deg 0.1mm", 0.1, -0.05, 5.0, 200.0, 50.0, -2.0, 60.0});
    g_toolPresets.push_back({"V-bit 30deg 0.2mm", 0.2, -0.05, 5.0, 300.0, 80.0, -2.0, 60.0});
    g_toolPresets.push_back({"End mill 0.8mm",    0.8, -0.15, 5.0, 400.0, 100.0, -2.0, 60.0});
    g_toolPresets.push_back({"End mill 1.0mm",    1.0, -0.20, 5.0, 400.0, 100.0, -2.0, 60.0});
    g_toolPresets.push_back({"Drill 0.8mm",       0.8, -2.00, 5.0, 300.0, 50.0, -2.0, 60.0});
    g_toolPresets.push_back({"Drill 1.0mm",       1.0, -2.00, 5.0, 300.0, 50.0, -2.0, 60.0});
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
            tp.toolDiameter = parseD(tc.getValue(p + "toolDiameter", "0.2"));
            tp.cutDepth     = parseD(tc.getValue(p + "cutDepth", "-0.05"));
            tp.safeHeight   = parseD(tc.getValue(p + "safeHeight", "5.0"));
            tp.feedRateXY   = parseD(tc.getValue(p + "feedXY", "300"));
            tp.feedRateZ    = parseD(tc.getValue(p + "feedZ", "100"));
            tp.zDrill       = parseD(tc.getValue(p + "zDrill", "-2"));
            tp.drillFeed    = parseD(tc.getValue(p + "drillFeed", "60"));
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
        tc.setValue(p + "drillFeed",    dblStr(tp.drillFeed, 1));
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
    if (g_fldDrillFeed)  g_fldDrillFeed->setText(dblStr(tp.drillFeed, 1).c_str());
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
    for (int i = 0; i < (int)g_toolPresets.size(); i++) {
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
static HWND s_hToolDrF   = nullptr;
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
        SetWindowTextW(s_hToolDrF, L"");
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
    SetWindowTextW(s_hToolDrF,   StringUtils::utf8ToWide(dblStr(tp.drillFeed, 1)).c_str());
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
    GetWindowTextW(s_hToolDrF, buf, 128);   tp.drillFeed    = parseD(StringUtils::wideToUtf8(buf));
}

static void toolDlgRefreshList(int sel = -1) {
    SendMessageW(s_hToolList, LB_RESETCONTENT, 0, 0);
    for (auto& tp : g_toolPresets) {
        std::wstring name = StringUtils::utf8ToWide(tp.name);
        SendMessageW(s_hToolList, LB_ADDSTRING, 0, (LPARAM)name.c_str());
    }
    if (sel >= 0 && sel < (int)g_toolPresets.size())
        SendMessageW(s_hToolList, LB_SETCURSEL, sel, 0);
}

static LRESULT CALLBACK ToolDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int lbW = 170, lbH = 200;
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
            mkField(7, L"Drill Feed:", s_hToolDrF,  108);

            // Buttons
            CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
                15, lbH + 20, 80, 26, hwnd, (HMENU)110, _core.hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE,
                105, lbH + 20, 80, 26, hwnd, (HMENU)111, _core.hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE,
                fx, 15 + 8 * 28, 80, 26, hwnd, (HMENU)112, _core.hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                fx + 90, 15 + 8 * 28, 80, 26, hwnd, (HMENU)113, _core.hInstance, NULL);

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
    int cy = (pr.top + pr.bottom) / 2 - 170;

    CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"G2G_ToolPresets", L"Tool Presets",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, 480, 310,
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

    cfg.machine.engraver_tip_width  = d(g_fldToolDia);
    cfg.machine.engraver_z_cut      = d(g_fldCutDepth);
    cfg.machine.engraver_z_travel   = d(g_fldSafeHeight);
    cfg.machine.spindle_z_drill     = d(g_fldZDrill);
    cfg.machine.spindle_tool_diameter = d(g_fldDrillDia);
    cfg.machine.move_feedrate       = 2400;

    cfg.cam.overlap = d(g_fldOverlap);
    cfg.cam.offset  = d(g_fldOffset);

    cfg.job.engraver_feedrate  = d(g_fldFeedXY);
    cfg.job.spindle_feedrate   = d(g_fldDrillFeed);
    cfg.job.spindle_power      = 255;

    return cfg;
}

// ════════════════════════════════════════════════════════════════════════════
// Layer panel
// ════════════════════════════════════════════════════════════════════════════

void rebuildLayerPanel() {
    if (!g_hLayerPanel || !g_canvas) return;
    SendMessageW(g_hLayerPanel, LB_RESETCONTENT, 0, 0);

    auto& lay = g_canvas->layers();
    auto& pres = g_canvas->presence();

    // Each item is either a section header (non-clickable) or a toggleable layer.
    // We store a mapping from listbox index → layer flag pointer so toggle works.
    // Section headers are indicated by starting with "── ".

    auto addSection = [](const wchar_t* name) {
        std::wstring text = L"\u2500\u2500 ";
        text += name;
        text += L" \u2500\u2500";
        SendMessageW(g_hLayerPanel, LB_ADDSTRING, 0, (LPARAM)text.c_str());
    };

    auto addItem = [](const wchar_t* name, bool visible) {
        std::wstring text = visible ? L"  \u2611 " : L"  \u2610 ";
        text += name;
        SendMessageW(g_hLayerPanel, LB_ADDSTRING, 0, (LPARAM)text.c_str());
    };

    // ── Board ──
    addSection(L"Board");
    addItem(L"Board Outline", lay.outline);             // index 1

    // ── Copper ──
    if (pres.copperTop || pres.copperBottom) {
        addSection(L"Copper");
        if (pres.copperTop)    addItem(L"Copper Top (F_Cu)",    lay.copperTop);
        if (pres.copperBottom) addItem(L"Copper Bottom (B_Cu)", lay.copperBottom);
    }

    // ── Layers ──
    if (pres.maskTop || pres.maskBottom || pres.silkTop || pres.silkBottom ||
        pres.pasteTop || pres.pasteBottom) {
        addSection(L"Layers");
        if (pres.maskTop)      addItem(L"Mask Top",       lay.maskTop);
        if (pres.maskBottom)   addItem(L"Mask Bottom",    lay.maskBottom);
        if (pres.silkTop)      addItem(L"Silkscreen Top", lay.silkTop);
        if (pres.silkBottom)   addItem(L"Silkscreen Bot", lay.silkBottom);
        if (pres.pasteTop)     addItem(L"Paste Top",      lay.pasteTop);
        if (pres.pasteBottom)  addItem(L"Paste Bottom",   lay.pasteBottom);
    }

    // ── Drills ──
    if (pres.drillsPTH || pres.drillsNPTH) {
        addSection(L"Drills");
        if (pres.drillsPTH)    addItem(L"PTH Drills",     lay.drillsPTH);
        if (pres.drillsNPTH)   addItem(L"NPTH Drills",    lay.drillsNPTH);
    }

    // ── Generated ──
    if (pres.clearance || pres.isolation) {
        addSection(L"Generated");
        if (pres.clearance)    addItem(L"Clearance",       lay.clearance);
        if (pres.isolation)    addItem(L"Isolation Paths", lay.isolation);
    }
    // Cutout always available as a toggle (even without data yet)
    // addItem(L"Cutout Path",    lay.cutout);
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
