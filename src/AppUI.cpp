// ============================================================================
// AppUI.cpp — UI layout, toolbar, layer panel, resize handler
// ============================================================================

#include "AppUI.h"
#include "AppState.h"
#include "Canvas/PCBCanvas.h"

#include <Core.h>
#include <UI/SimpleWindow/SimpleWindow.h>
#include <UI/Label/Label.h>
#include <UI/Button/Button.h>
#include <UI/InputField/InputField.h>
#include <UI/CheckBox/CheckBox.h>
#include <UI/TextArea/TextArea.h>
#include <UI/ProgressBar/ProgressBar.h>
#include <Util/StringUtils.h>

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <functional>

// ════════════════════════════════════════════════════════════════════════════
// Theme colors
// ════════════════════════════════════════════════════════════════════════════

static const COLORREF CLR_BG        = RGB(45, 45, 54);
static const COLORREF CLR_TEXT      = RGB(220, 220, 230);
static const COLORREF CLR_LABEL     = RGB(155, 160, 172);
static const COLORREF CLR_SECTION   = RGB(90, 165, 235);

static const COLORREF CLR_BTN_BG    = RGB(58, 58, 70);
static const COLORREF CLR_BTN_TEXT  = RGB(200, 200, 210);
static const COLORREF CLR_BTN_HOVER = RGB(72, 72, 88);

static const COLORREF CLR_ACTION_BG    = RGB(0, 120, 60);
static const COLORREF CLR_ACTION_TEXT  = RGB(255, 255, 255);
static const COLORREF CLR_ACTION_HOVER = RGB(0, 155, 78);

static const COLORREF CLR_TOOL_BG     = RGB(60, 80, 120);
static const COLORREF CLR_TOOL_TEXT   = RGB(200, 220, 255);
static const COLORREF CLR_TOOL_HOVER  = RGB(70, 95, 140);

static const COLORREF CLR_EXPORT_BG    = RGB(130, 80, 20);
static const COLORREF CLR_EXPORT_TEXT  = RGB(255, 230, 180);
static const COLORREF CLR_EXPORT_HOVER = RGB(160, 100, 30);

// ════════════════════════════════════════════════════════════════════════════
// Styling helpers
// ════════════════════════════════════════════════════════════════════════════

static void styleBtn(SimpleWindow* win, Button* btn,
                     COLORREF bg, COLORREF text, COLORREF hover) {
    btn->setBackColor(bg);
    btn->setTextColor(text);
    btn->setHoverColor(hover);
    win->add(btn);
}

static Label* addLabel(SimpleWindow* win, int x, int y, int w, const wchar_t* text,
                       int fontSize = 10, bool bold = false) {
    Label* lbl = new Label(x, y, w, 18, text);
    win->add(lbl);
    lbl->setFont(L"Segoe UI", fontSize, bold);
    lbl->setTextColor(CLR_LABEL);
    lbl->setBackColor(CLR_BG);
    return lbl;
}

static InputField* addField(SimpleWindow* win, int x, int y, int w,
                             const char* defVal = "",
                             std::function<void(InputField*, const char*)> cb = nullptr) {
    InputField* fld = new InputField(x, y, w, 22, defVal, cb);
    win->add(fld);
    return fld;
}

// ════════════════════════════════════════════════════════════════════════════
// File / folder dialog helpers
// ════════════════════════════════════════════════════════════════════════════

static std::string browseFolderUTF8(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
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

static std::string saveFileDialogUTF8(HWND owner,
    const wchar_t* filter, const wchar_t* title, const wchar_t* defExt)
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
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

// ════════════════════════════════════════════════════════════════════════════
// Layer panel LISTBOX
// ════════════════════════════════════════════════════════════════════════════

static const int LP_ID = 9500;

static void createLayerPanel(HWND parent, int x, int y, int w, int h) {
    g_hLayerPanel = CreateWindowExW(0, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        x, y, w, h,
        parent, (HMENU)(intptr_t)LP_ID, _core.hInstance, NULL);

    SendMessageW(g_hLayerPanel, WM_SETFONT,
        (WPARAM)CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"), TRUE);
}

// ════════════════════════════════════════════════════════════════════════════
// Resize subclass proc
// ════════════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK ResizeProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_SIZE && wParam != SIZE_MINIMIZED) {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        doResize(w, h);
    }
    // Auto-refresh debounce timer
    if (msg == WM_TIMER && wParam == 9601) {
        KillTimer(hwnd, 9601);
        if (g_needsReparse) {
            g_needsReparse = false;
            doLoadKicadDir();
        } else {
            doRefreshIsolation();
        }
        return 0;
    }
    // Layer panel click handler — uses g_layerItems mapping
    if (msg == WM_COMMAND && LOWORD(wParam) == LP_ID && HIWORD(wParam) == LBN_SELCHANGE) {
        int idx = (int)SendMessageW(g_hLayerPanel, LB_GETCURSEL, 0, 0);
        if (idx >= 0 && idx < (int)g_layerItems.size() && g_canvas) {
            auto& item = g_layerItems[idx];
            if (!item.isSection && item.flag) {
                *item.flag = !(*item.flag);
                rebuildLayerPanel();
                g_canvas->redraw();
            }
        }
        SendMessageW(g_hLayerPanel, LB_SETCURSEL, (WPARAM)-1, 0);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void installResizeHandler() {
    SetWindowSubclass(g_window->getHandle(), ResizeProc, 1, 0);
}

void doResize(int w, int h) {
    int canvasW = w - LAYER_PANEL_W - 10;
    int canvasH = h - TOOLBAR_HEIGHT - LOG_AREA_H - STATUS_BAR_H - 8;
    if (canvasW < 50) canvasW = 50;
    if (canvasH < 50) canvasH = 50;

    // Canvas
    if (g_canvas)
        SetWindowPos(g_canvas->getHandle(), NULL,
            5, TOOLBAR_HEIGHT, canvasW, canvasH, SWP_NOZORDER);

    // Layer panel
    if (g_hLayerPanel)
        SetWindowPos(g_hLayerPanel, NULL,
            w - LAYER_PANEL_W - 5, TOOLBAR_HEIGHT,
            LAYER_PANEL_W, canvasH, SWP_NOZORDER);

    // Log area
    if (g_logArea)
        SetWindowPos(g_logArea->getHandle(), NULL,
            5, TOOLBAR_HEIGHT + canvasH + 4,
            w - 10, LOG_AREA_H, SWP_NOZORDER);

    // Progress bar
    if (g_progressBar)
        SetWindowPos(g_progressBar->getHandle(), NULL,
            5, h - STATUS_BAR_H,
            w - 10, 5, SWP_NOZORDER);
}

// ════════════════════════════════════════════════════════════════════════════
// createUI — Main UI construction
// ════════════════════════════════════════════════════════════════════════════

void createUI(SimpleWindow* win) {
    int m = 8;    // margin
    int y = 4;

    // Callbacks for auto-refresh on parameter change
    auto onIsoParam = [](InputField*, const char*) { scheduleAutoRefresh(false); };
    auto onPosParam = [](InputField*, const char*) { scheduleAutoRefresh(true); };

    // ── Row 1: File actions + tool selector + key engraver fields ────────
    styleBtn(win, new Button(m, y, 115, 26, "Open KiCad...",
        [](Button*) {
            auto p = browseFolderUTF8(g_window->getHandle(),
                L"Select KiCad Gerber directory");
            if (!p.empty()) {
                g_fldKicadDir->setText(p.c_str());
                doLoadKicadDir();
            }
        }),
        CLR_ACTION_BG, CLR_ACTION_TEXT, CLR_ACTION_HOVER);

    // Tool preset dropdown
    g_btnTool = new Button(m + 120, y, 150, 26, "Tool",
        [](Button*) { showToolPopup(); });
    styleBtn(win, g_btnTool, CLR_TOOL_BG, CLR_TOOL_TEXT, CLR_TOOL_HOVER);
    updateToolButtonText();

    // Key fields — tool dia triggers isolation refresh
    int fx = m + 278;
    addLabel(win, fx, y + 3, 23, L"Tip:");
    g_fldToolDia = addField(win, fx + 25, y, 42, "0.1", onIsoParam);

    fx += 72;
    addLabel(win, fx, y + 3, 30, L"Z Cut:");
    g_fldCutDepth = addField(win, fx + 32, y, 48, "-0.05");

    fx += 85;
    addLabel(win, fx, y + 3, 36, L"Z Trav:");
    g_fldSafeHeight = addField(win, fx + 38, y, 36, "5");

    fx += 80;
    addLabel(win, fx, y + 3, 25, L"Feed:");
    g_fldFeedXY = addField(win, fx + 28, y, 42, "300");

    fx += 76;
    addLabel(win, fx, y + 3, 32, L"F Plg:");
    g_fldFeedZ = addField(win, fx + 34, y, 40, "100");

    fx += 80;
    addLabel(win, fx, y + 3, 23, L"Mat:");
    g_fldMaterial = addField(win, fx + 25, y, 36, "1.5");

    // ── Row 2: Generate button + CAM params + drill params ─────────
    y += 30;

    // Generate & Export button
    g_btnGenerate = new Button(m, y, 100, 24, "Generate",
        [](Button*) { doGenerate(); });
    styleBtn(win, g_btnGenerate, CLR_EXPORT_BG, CLR_EXPORT_TEXT, CLR_EXPORT_HOVER);
    g_btnGenerate->setFont(L"Segoe UI", 11, true);

    fx = m + 108;
    addLabel(win, fx, y + 3, 48, L"Overlap:");
    g_fldOverlap = addField(win, fx + 50, y, 36, "0.4", onIsoParam);

    fx += 92;
    addLabel(win, fx, y + 3, 40, L"Offset:");
    g_fldOffset = addField(win, fx + 42, y, 36, "0.02", onIsoParam);

    // Drill fields
    fx += 85;
    addLabel(win, fx, y + 3, 36, L"Z Drill:");
    g_fldZDrill = addField(win, fx + 38, y, 40, "-2");

    fx += 84;
    addLabel(win, fx, y + 3, 40, L"Dr.Dia:");
    g_fldDrillDia = addField(win, fx + 42, y, 36, "0.8");

    fx += 84;
    addLabel(win, fx, y + 3, 42, L"Dr.Feed:");
    g_fldDrillFeed = addField(win, fx + 44, y, 38, "60");

    // ── Row 3: Options + file paths ──────────────────────────────────────
    y += 28;

    addLabel(win, m, y + 3, 40, L"KiCad:");
    g_fldKicadDir = addField(win, m + 42, y, 280, "");
    g_fldKicadDir->setMaxLength(512);

    Button* btnBrowse = new Button(m + 326, y, 26, 22, "...",
        [](Button*) {
            auto p = browseFolderUTF8(g_window->getHandle(),
                L"Select KiCad Gerber directory");
            if (!p.empty()) {
                g_fldKicadDir->setText(p.c_str());
                doLoadKicadDir();
            }
        });
    styleBtn(win, btnBrowse, CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    addLabel(win, m + 360, y + 3, 40, L"Output:");
    g_fldOutputFile = addField(win, m + 402, y, 230, "");
    g_fldOutputFile->setMaxLength(512);

    Button* btnBrowseOut = new Button(m + 636, y, 26, 22, "...",
        [](Button*) {
            auto p = saveFileDialogUTF8(g_window->getHandle(),
                L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                L"Save GCode", L"gcode");
            if (!p.empty()) g_fldOutputFile->setText(p.c_str());
        });
    styleBtn(win, btnBrowseOut, CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    // X/Y offsets — trigger full reparse (position changes)
    fx = m + 670;
    addLabel(win, fx, y + 3, 17, L"X:");
    g_fldXOffset = addField(win, fx + 18, y, 36, "0", onPosParam);
    fx += 58;
    addLabel(win, fx, y + 3, 17, L"Y:");
    g_fldYOffset = addField(win, fx + 18, y, 36, "0", onPosParam);

    fx += 60;
    g_chkFlip = new CheckBox(fx, y, 55, 22, "Flip", false,
        [](CheckBox*, bool) { scheduleAutoRefresh(true); });
    win->add(g_chkFlip);
    g_chkIgnoreVia = new CheckBox(fx + 58, y, 78, 22, "No Vias", false,
        [](CheckBox*, bool) { scheduleAutoRefresh(true); });
    win->add(g_chkIgnoreVia);
    g_chkDebug = new CheckBox(fx + 138, y, 70, 22, "Debug", true);
    win->add(g_chkDebug);

    // ── Canvas (will be repositioned by doResize) ────────────────────────

    g_canvas = new PCBCanvas();
    g_canvas->create(win->getHandle(), 5, TOOLBAR_HEIGHT, 600, 400);
    g_canvas->setBackgroundColor(RGB(22, 22, 28));
    g_canvas->setGridColor(RGB(40, 40, 52));
    g_canvas->setGridSpacing(10.0);
    g_canvas->setGridVisible(true);

    // ── Layer panel ──────────────────────────────────────────────────────

    RECT rc;
    GetClientRect(win->getHandle(), &rc);
    createLayerPanel(win->getHandle(),
        rc.right - LAYER_PANEL_W - 5, TOOLBAR_HEIGHT,
        LAYER_PANEL_W, rc.bottom - TOOLBAR_HEIGHT - LOG_AREA_H - STATUS_BAR_H - 8);
    rebuildLayerPanel();

    // ── Log area ─────────────────────────────────────────────────────────

    int logY = rc.bottom - LOG_AREA_H - STATUS_BAR_H - 4;
    g_logArea = new TextArea(5, logY, rc.right - 10, LOG_AREA_H);
    win->add(g_logArea);
    g_logArea->setFont(L"Consolas", 10, false);
    g_logArea->setTextColor(RGB(185, 195, 205));
    g_logArea->setBackColor(RGB(22, 22, 28));

    // ── Progress bar ─────────────────────────────────────────────────────

    g_progressBar = new ProgressBar(5, rc.bottom - STATUS_BAR_H, rc.right - 10, 5);
    win->add(g_progressBar);
    g_progressBar->setColor(RGB(0, 180, 80));

    // ── Install resize handler & initial layout ──────────────────────────

    installResizeHandler();
    doResize(rc.right, rc.bottom);
}
