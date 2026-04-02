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

static const COLORREF CLR_BG            = RGB(34, 37, 46);
static const COLORREF CLR_TEXT          = RGB(226, 230, 239);
static const COLORREF CLR_LABEL         = RGB(159, 168, 189);
static const COLORREF CLR_SECTION       = RGB(108, 194, 255);

static const COLORREF CLR_BTN_BG        = RGB(63, 68, 84);
static const COLORREF CLR_BTN_TEXT      = RGB(221, 226, 239);
static const COLORREF CLR_BTN_HOVER     = RGB(80, 86, 106);

static const COLORREF CLR_ACTION_BG     = RGB(9, 147, 101);
static const COLORREF CLR_ACTION_TEXT   = RGB(240, 255, 248);
static const COLORREF CLR_ACTION_HOVER  = RGB(14, 176, 121);

static const COLORREF CLR_TOOL_BG       = RGB(57, 102, 173);
static const COLORREF CLR_TOOL_TEXT     = RGB(225, 238, 255);
static const COLORREF CLR_TOOL_HOVER    = RGB(72, 120, 198);

static const COLORREF CLR_EXPORT_BG     = RGB(197, 127, 33);
static const COLORREF CLR_EXPORT_TEXT   = RGB(255, 248, 236);
static const COLORREF CLR_EXPORT_HOVER  = RGB(222, 150, 53);

static const COLORREF CLR_FIELD_BG      = RGB(26, 30, 40);
static const COLORREF CLR_FIELD_TEXT    = RGB(230, 233, 242);
static const COLORREF CLR_LIST_BG       = RGB(26, 30, 40);
static const COLORREF CLR_LIST_TEXT     = RGB(220, 224, 236);
static const COLORREF CLR_LOG_BG        = RGB(20, 24, 33);
static const COLORREF CLR_LOG_TEXT      = RGB(181, 201, 220);

static const COLORREF CLR_PROGRESS      = RGB(0, 198, 118);
static const COLORREF CLR_PROGRESS_BG   = RGB(50, 58, 78);

static HBRUSH g_brField = NULL;
static HBRUSH g_brList  = NULL;

static HFONT getUiFieldFont() {
    static HFONT s_font = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    return s_font;
}

static HFONT getUiCheckFont() {
    static HFONT s_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    return s_font;
}

// ════════════════════════════════════════════════════════════════════════════
// Styling helpers
// ════════════════════════════════════════════════════════════════════════════

static void styleBtn(SimpleWindow* win, Button* btn,
                     COLORREF bg, COLORREF text, COLORREF hover) {
    btn->setBackColor(bg);
    btn->setTextColor(text);
    btn->setHoverColor(hover);
    btn->setFont(L"Segoe UI", 11, true);
    win->add(btn);
}

static void styleCheck(CheckBox* chk) {
    if (!chk || !chk->getHandle()) return;
    SendMessageW(chk->getHandle(), WM_SETFONT, (WPARAM)getUiCheckFont(), TRUE);
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
    InputField* fld = new InputField(x, y, w, 24, defVal, cb);
    win->add(fld);
    if (fld->getHandle())
        SendMessageW(fld->getHandle(), WM_SETFONT, (WPARAM)getUiFieldFont(), TRUE);
    return fld;
}

static Label* addSectionLabel(SimpleWindow* win, int x, int y, int w, const wchar_t* text) {
    Label* lbl = new Label(x, y, w, 20, text);
    win->add(lbl);
    lbl->setFont(L"Segoe UI", 10, true);
    lbl->setTextColor(CLR_SECTION);
    lbl->setBackColor(CLR_BG);
    return lbl;
}

static bool isThemedEdit(HWND hCtrl) {
    InputField* fields[] = {
        g_fldKicadDir, g_fldOutputFile, g_fldToolDia, g_fldCutDepth, g_fldSafeHeight,
        g_fldFeedXY, g_fldFeedZ, g_fldOverlap, g_fldOffset, g_fldXOffset,
        g_fldYOffset, g_fldMaterial, g_fldZDrill, g_fldDrillDia, g_fldDrillFeed
    };
    for (InputField* f : fields) {
        if (f && f->getHandle() == hCtrl) return true;
    }
    return false;
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
        (WPARAM)CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"), TRUE);
    SendMessageW(g_hLayerPanel, LB_SETITEMHEIGHT, 0, 20);
}

// ════════════════════════════════════════════════════════════════════════════
// Resize subclass proc
// ════════════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK ResizeProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_CTLCOLOREDIT) {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (isThemedEdit(hCtrl)) {
            if (!g_brField) g_brField = CreateSolidBrush(CLR_FIELD_BG);
            SetTextColor(hdc, CLR_FIELD_TEXT);
            SetBkColor(hdc, CLR_FIELD_BG);
            return (LRESULT)g_brField;
        }
    }

    if (msg == WM_CTLCOLORLISTBOX) {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hLayerPanel) {
            if (!g_brList) g_brList = CreateSolidBrush(CLR_LIST_BG);
            SetTextColor(hdc, CLR_LIST_TEXT);
            SetBkColor(hdc, CLR_LIST_BG);
            return (LRESULT)g_brList;
        }
    }

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

                // Check if toggled flag belongs to copper sub-visibility or pad group
                bool isCopperSub = false;
                {
                    auto& topSub = g_canvas->copperTopSubVis();
                    auto& botSub = g_canvas->copperBottomSubVis();
                    if (item.flag == &topSub.traces || item.flag == &topSub.pads || item.flag == &topSub.regions ||
                        item.flag == &botSub.traces || item.flag == &botSub.pads || item.flag == &botSub.regions) {
                        isCopperSub = true;
                    }
                    // Also check per-aperture pad group visibility
                    if (!isCopperSub) {
                        auto* pgTop = g_canvas->copperTopPadGroups();
                        auto* pgBot = g_canvas->copperBottomPadGroups();
                        if (pgTop) for (auto& pg : *pgTop) { if (item.flag == &pg.visible) { isCopperSub = true; break; } }
                        if (!isCopperSub && pgBot) for (auto& pg : *pgBot) { if (item.flag == &pg.visible) { isCopperSub = true; break; } }
                    }
                }

                rebuildLayerPanel();
                g_canvas->redraw();

                // Copper sub-vis change → recompute clearance + isolation
                if (isCopperSub)
                    doRecomputeClearance();
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
    const int outer = 8;
    int canvasW = w - LAYER_PANEL_W - (outer * 3);
    int canvasH = h - TOOLBAR_HEIGHT - LOG_AREA_H - STATUS_BAR_H - (outer * 2);
    if (canvasW < 50) canvasW = 50;
    if (canvasH < 50) canvasH = 50;

    // Canvas
    if (g_canvas)
        SetWindowPos(g_canvas->getHandle(), NULL,
            outer, TOOLBAR_HEIGHT, canvasW, canvasH, SWP_NOZORDER);

    // Layer panel
    if (g_hLayerPanel)
        SetWindowPos(g_hLayerPanel, NULL,
            w - LAYER_PANEL_W - outer, TOOLBAR_HEIGHT,
            LAYER_PANEL_W, canvasH, SWP_NOZORDER);

    // Log area
    if (g_logArea)
        SetWindowPos(g_logArea->getHandle(), NULL,
            outer, TOOLBAR_HEIGHT + canvasH + outer,
            w - (outer * 2), LOG_AREA_H, SWP_NOZORDER);

    // Progress bar
    if (g_progressBar)
        SetWindowPos(g_progressBar->getHandle(), NULL,
            outer, h - STATUS_BAR_H + 2,
            w - (outer * 2), 6, SWP_NOZORDER);
}

// ════════════════════════════════════════════════════════════════════════════
// createUI — Main UI construction
// ════════════════════════════════════════════════════════════════════════════

void createUI(SimpleWindow* win) {
    const int m = 10;
    const int rowH = 26;
    const int rowGap = 8;
    int y = 8;

    // Callbacks for auto-refresh on parameter change
    auto onIsoParam = [](InputField*, const char*) { scheduleAutoRefresh(false); };
    auto onPosParam = [](InputField*, const char*) { scheduleAutoRefresh(true); };

    // Section labels
    addSectionLabel(win, m, y, 180, L"PROJECT");
    addSectionLabel(win, m + 370, y, 240, L"MACHINING");
    addSectionLabel(win, m + 930, y, 220, L"POSITION / FILTERS");

    y += 16;

    // ── Row 1: File actions + tool selector + key engraver fields ────────
    styleBtn(win, new Button(m, y, 130, rowH, "Open KiCad...",
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
    g_btnTool = new Button(m + 138, y, 220, rowH, "Tool",
        [](Button*) { showToolPopup(); });
    styleBtn(win, g_btnTool, CLR_TOOL_BG, CLR_TOOL_TEXT, CLR_TOOL_HOVER);
    updateToolButtonText();

    // Main action
    g_btnGenerate = new Button(m + 366, y, 120, rowH, "Generate",
        [](Button*) { doGenerate(); });
    styleBtn(win, g_btnGenerate, CLR_EXPORT_BG, CLR_EXPORT_TEXT, CLR_EXPORT_HOVER);
    g_btnGenerate->setFont(L"Segoe UI", 11, true);

    // Key fields — tool dia triggers isolation refresh
    int fx = m + 498;
    addLabel(win, fx, y + 3, 28, L"Tip:");
    g_fldToolDia = addField(win, fx + 30, y, 54, "0.1", onIsoParam);

    fx += 90;
    addLabel(win, fx, y + 3, 42, L"Z Cut:");
    g_fldCutDepth = addField(win, fx + 44, y, 54, "0.05");

    fx += 106;
    addLabel(win, fx, y + 3, 44, L"Z Safe:");
    g_fldSafeHeight = addField(win, fx + 46, y, 54, "5");

    fx += 108;
    addLabel(win, fx, y + 3, 38, L"Feed:");
    g_fldFeedXY = addField(win, fx + 40, y, 58, "300");

    fx += 104;
    addLabel(win, fx, y + 3, 40, L"Plunge:");
    g_fldFeedZ = addField(win, fx + 42, y, 58, "100");

    fx += 104;
    addLabel(win, fx, y + 3, 30, L"Mat:");
    g_fldMaterial = addField(win, fx + 32, y, 52, "1.5");

    // ── Row 2: CAM + drill/spindle + machine options ─────────
    y += rowH + rowGap;

    addLabel(win, m, y + 3, 56, L"Overlap:");
    g_fldOverlap = addField(win, m + 58, y, 58, "0.4", onIsoParam);

    addLabel(win, m + 126, y + 3, 48, L"Offset:");
    g_fldOffset = addField(win, m + 176, y, 58, "0.02", onIsoParam);

    addLabel(win, m + 248, y + 3, 52, L"Z Drill:");
    g_fldZDrill = addField(win, m + 302, y, 58, "2");

    addLabel(win, m + 374, y + 3, 48, L"Sp Dia:");
    g_fldDrillDia = addField(win, m + 424, y, 58, "0.8");

    addLabel(win, m + 496, y + 3, 56, L"Sp Feed:");
    g_fldDrillFeed = addField(win, m + 554, y, 58, "60");

    addLabel(win, m + 630, y + 3, 36, L"X Off:");
    g_fldXOffset = addField(win, m + 668, y, 50, "0", onPosParam);

    addLabel(win, m + 724, y + 3, 36, L"Y Off:");
    g_fldYOffset = addField(win, m + 762, y, 50, "0", onPosParam);

    g_chkFlip = new CheckBox(m + 826, y + 1, 64, 24, "Flip", false,
        [](CheckBox*, bool) { scheduleAutoRefresh(true); });
    win->add(g_chkFlip);
    styleCheck(g_chkFlip);

    g_chkIgnoreVia = new CheckBox(m + 896, y + 1, 92, 24, "No Vias", false,
        [](CheckBox*, bool) { scheduleAutoRefresh(true); });
    win->add(g_chkIgnoreVia);
    styleCheck(g_chkIgnoreVia);

    g_chkDebug = new CheckBox(m + 996, y + 1, 76, 24, "Debug", true);
    win->add(g_chkDebug);
    styleCheck(g_chkDebug);

    g_chkFluidNC = new CheckBox(m + 1078, y + 1, 94, 24, "FluidNC", false);
    win->add(g_chkFluidNC);
    styleCheck(g_chkFluidNC);

    // Auto-managed workflow: these options are derived from selected tool preset.
    // User should mainly provide material thickness (Mat).
    if (g_fldToolDia)    g_fldToolDia->setReadOnly(true);
    if (g_fldCutDepth)   g_fldCutDepth->setReadOnly(true);
    if (g_fldSafeHeight) g_fldSafeHeight->setReadOnly(true);
    if (g_fldFeedXY)     g_fldFeedXY->setReadOnly(true);
    if (g_fldFeedZ)      g_fldFeedZ->setReadOnly(true);
    if (g_fldOverlap)    g_fldOverlap->setReadOnly(true);
    if (g_fldOffset)     g_fldOffset->setReadOnly(true);
    if (g_fldZDrill)     g_fldZDrill->setReadOnly(true);
    if (g_fldDrillDia)   g_fldDrillDia->setReadOnly(true);
    if (g_fldDrillFeed)  g_fldDrillFeed->setReadOnly(true);
    if (g_fldXOffset)    g_fldXOffset->setReadOnly(true);
    if (g_fldYOffset)    g_fldYOffset->setReadOnly(true);

    if (g_chkFlip && g_chkFlip->getHandle())
        EnableWindow(g_chkFlip->getHandle(), FALSE);
    if (g_chkIgnoreVia && g_chkIgnoreVia->getHandle())
        EnableWindow(g_chkIgnoreVia->getHandle(), FALSE);
    if (g_chkDebug && g_chkDebug->getHandle())
        EnableWindow(g_chkDebug->getHandle(), FALSE);

    // ── Row 3: Paths and browse actions ─────────────────────────────────
    y += rowH + rowGap;

    addLabel(win, m, y + 3, 40, L"KiCad:");
    g_fldKicadDir = addField(win, m + 42, y, 430, "");
    g_fldKicadDir->setMaxLength(512);

    Button* btnBrowse = new Button(m + 476, y, 30, 24, "...",
        [](Button*) {
            auto p = browseFolderUTF8(g_window->getHandle(),
                L"Select KiCad Gerber directory");
            if (!p.empty()) {
                g_fldKicadDir->setText(p.c_str());
                doLoadKicadDir();
            }
        });
    styleBtn(win, btnBrowse, CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    addLabel(win, m + 516, y + 3, 46, L"Output:");
    g_fldOutputFile = addField(win, m + 564, y, 500, "");
    g_fldOutputFile->setMaxLength(512);

    Button* btnBrowseOut = new Button(m + 1068, y, 30, 24, "...",
        [](Button*) {
            auto p = saveFileDialogUTF8(g_window->getHandle(),
                L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                L"Save GCode", L"gcode");
            if (!p.empty()) g_fldOutputFile->setText(p.c_str());
        });
    styleBtn(win, btnBrowseOut, CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    // ── Canvas (will be repositioned by doResize) ────────────────────────

    g_canvas = new PCBCanvas();
    g_canvas->create(win->getHandle(), 8, TOOLBAR_HEIGHT, 600, 400);
    g_canvas->setBackgroundColor(RGB(18, 22, 31));
    g_canvas->setGridColor(RGB(39, 47, 68));
    g_canvas->setGridSpacing(10.0);
    g_canvas->setGridVisible(true);

    // ── Layer panel ──────────────────────────────────────────────────────

    RECT rc;
    GetClientRect(win->getHandle(), &rc);
    createLayerPanel(win->getHandle(),
        rc.right - LAYER_PANEL_W - 8, TOOLBAR_HEIGHT,
        LAYER_PANEL_W, rc.bottom - TOOLBAR_HEIGHT - LOG_AREA_H - STATUS_BAR_H - 16);
    rebuildLayerPanel();

    // ── Log area ─────────────────────────────────────────────────────────

    int logY = rc.bottom - LOG_AREA_H - STATUS_BAR_H - 8;
    g_logArea = new TextArea(8, logY, rc.right - 16, LOG_AREA_H);
    win->add(g_logArea);
    g_logArea->setFont(L"Consolas", 11, false);
    g_logArea->setTextColor(CLR_LOG_TEXT);
    g_logArea->setBackColor(CLR_LOG_BG);

    // ── Progress bar ─────────────────────────────────────────────────────

    g_progressBar = new ProgressBar(8, rc.bottom - STATUS_BAR_H + 2, rc.right - 16, 6);
    win->add(g_progressBar);
    g_progressBar->setColor(CLR_PROGRESS);
    g_progressBar->setBackColor(CLR_PROGRESS_BG);

    // ── Install resize handler & initial layout ──────────────────────────

    installResizeHandler();
    doResize(rc.right, rc.bottom);
}
