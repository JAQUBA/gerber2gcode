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
#include <cstring>

// ════════════════════════════════════════════════════════════════════════════
// Theme colors
// ════════════════════════════════════════════════════════════════════════════

static const COLORREF CLR_BG            = RGB(27, 31, 40);
static const COLORREF CLR_TEXT          = RGB(231, 236, 245);
static const COLORREF CLR_LABEL         = RGB(174, 184, 206);
static const COLORREF CLR_SECTION       = RGB(111, 218, 235);

static const COLORREF CLR_BTN_BG        = RGB(66, 74, 95);
static const COLORREF CLR_BTN_TEXT      = RGB(230, 236, 248);
static const COLORREF CLR_BTN_HOVER     = RGB(84, 94, 120);

static const COLORREF CLR_ACTION_BG     = RGB(0, 147, 117);
static const COLORREF CLR_ACTION_TEXT   = RGB(236, 255, 250);
static const COLORREF CLR_ACTION_HOVER  = RGB(0, 172, 136);

static const COLORREF CLR_TOOL_BG       = RGB(37, 107, 184);
static const COLORREF CLR_TOOL_TEXT     = RGB(231, 241, 255);
static const COLORREF CLR_TOOL_HOVER    = RGB(52, 124, 203);

static const COLORREF CLR_EXPORT_BG     = RGB(214, 132, 31);
static const COLORREF CLR_EXPORT_TEXT   = RGB(255, 250, 239);
static const COLORREF CLR_EXPORT_HOVER  = RGB(232, 151, 52);

static const COLORREF CLR_FIELD_BG      = RGB(19, 23, 31);
static const COLORREF CLR_FIELD_TEXT    = RGB(236, 241, 250);
static const COLORREF CLR_LIST_BG       = RGB(20, 24, 33);
static const COLORREF CLR_LIST_TEXT     = RGB(225, 230, 243);
static const COLORREF CLR_LOG_BG        = RGB(17, 20, 28);
static const COLORREF CLR_LOG_TEXT      = RGB(187, 206, 229);

static const COLORREF CLR_PROGRESS      = RGB(38, 205, 130);
static const COLORREF CLR_PROGRESS_BG   = RGB(53, 62, 85);

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
    InputField* fields[] = { g_fldKicadDir, g_fldOutputFile, g_fldMaterial };
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

static void doOpenKicadFolder() {
    auto p = browseFolderUTF8(g_window->getHandle(),
        L"Select KiCad Gerber directory");
    if (!p.empty() && g_fldKicadDir) {
        g_fldKicadDir->setText(p.c_str());
        doLoadKicadDir();
    }
}

static void doReloadProject() {
    if (!g_fldKicadDir) return;
    std::string dir = g_fldKicadDir->getText();
    if (dir.empty()) {
        doOpenKicadFolder();
        return;
    }
    doLoadKicadDir();
}

static void doFitCanvasToBoard() {
    if (!g_canvas || !g_pipelineData.valid) return;
    g_canvas->zoomToFit(g_pipelineData.boardW, g_pipelineData.boardH);
}

static void doResetCanvasView() {
    if (!g_canvas) return;
    g_canvas->resetView();
    g_canvas->redraw();
}

static void doToggleGrid() {
    if (!g_canvas) return;
    bool visible = !g_canvas->isGridVisible();
    g_canvas->setGridVisible(visible);
    g_canvas->redraw();
    logMsg(visible ? "Grid enabled" : "Grid disabled");
}

static void setDrillFiltersVisible(std::vector<DrillFilter>& filters, bool visible) {
    for (auto& f : filters)
        f.visible = visible;
}

static void setAllLayersVisible(bool visible) {
    if (!g_canvas) return;

    auto& lay = g_canvas->layers();
    auto& pres = g_canvas->presence();
    bool copperFilterChanged = false;

    auto setIfPresent = [&](bool present, bool& flag) {
        if (!present) return;
        flag = visible;
    };

    setIfPresent(pres.outline, lay.outline);
    setIfPresent(pres.copperTop, lay.copperTop);
    setIfPresent(pres.copperBottom, lay.copperBottom);
    setIfPresent(pres.maskTop, lay.maskTop);
    setIfPresent(pres.maskBottom, lay.maskBottom);
    setIfPresent(pres.silkTop, lay.silkTop);
    setIfPresent(pres.silkBottom, lay.silkBottom);
    setIfPresent(pres.pasteTop, lay.pasteTop);
    setIfPresent(pres.pasteBottom, lay.pasteBottom);
    setIfPresent(pres.drillsPTH, lay.drillsPTH);
    setIfPresent(pres.drillsNPTH, lay.drillsNPTH);
    setIfPresent(pres.clearance, lay.clearance);
    setIfPresent(pres.isolation, lay.isolation);
    setIfPresent(pres.cutout, lay.cutout);

    if (pres.copperTopSub.traces) lay.copperTopSub.traces = visible;
    if (pres.copperTopSub.pads)   lay.copperTopSub.pads = visible;
    if (pres.copperTopSub.regions)lay.copperTopSub.regions = visible;
    if (pres.copperBottomSub.traces) lay.copperBottomSub.traces = visible;
    if (pres.copperBottomSub.pads)   lay.copperBottomSub.pads = visible;
    if (pres.copperBottomSub.regions)lay.copperBottomSub.regions = visible;

    auto* pgTop = g_canvas->copperTopPadGroups();
    auto* pgBot = g_canvas->copperBottomPadGroups();
    if (pgTop) {
        for (auto& pg : *pgTop) {
            if (pg.visible != visible) {
                pg.visible = visible;
                copperFilterChanged = true;
            }
        }
    }
    if (pgBot) {
        for (auto& pg : *pgBot) {
            if (pg.visible != visible) {
                pg.visible = visible;
                copperFilterChanged = true;
            }
        }
    }

    setDrillFiltersVisible(g_canvas->drillFilterPTH(), visible);
    setDrillFiltersVisible(g_canvas->drillFilterNPTH(), visible);

    if (copperFilterChanged)
        doRecomputeClearance();
    else {
        rebuildLayerPanel();
        g_canvas->redraw();
    }
}

static void applyCopperFocusPreset() {
    if (!g_canvas) return;

    auto& lay = g_canvas->layers();
    auto& pres = g_canvas->presence();
    bool copperFilterChanged = false;

    if (pres.outline) lay.outline = true;
    if (pres.copperTop) lay.copperTop = true;
    if (pres.copperBottom) lay.copperBottom = true;

    if (pres.maskTop) lay.maskTop = false;
    if (pres.maskBottom) lay.maskBottom = false;
    if (pres.silkTop) lay.silkTop = false;
    if (pres.silkBottom) lay.silkBottom = false;
    if (pres.pasteTop) lay.pasteTop = false;
    if (pres.pasteBottom) lay.pasteBottom = false;
    if (pres.drillsPTH) lay.drillsPTH = false;
    if (pres.drillsNPTH) lay.drillsNPTH = false;
    if (pres.clearance) lay.clearance = false;
    if (pres.isolation) lay.isolation = true;

    if (pres.copperTopSub.traces) lay.copperTopSub.traces = true;
    if (pres.copperTopSub.pads)   lay.copperTopSub.pads = true;
    if (pres.copperTopSub.regions)lay.copperTopSub.regions = true;
    if (pres.copperBottomSub.traces) lay.copperBottomSub.traces = true;
    if (pres.copperBottomSub.pads)   lay.copperBottomSub.pads = true;
    if (pres.copperBottomSub.regions)lay.copperBottomSub.regions = true;

    auto* pgTop = g_canvas->copperTopPadGroups();
    auto* pgBot = g_canvas->copperBottomPadGroups();
    if (pgTop) {
        for (auto& pg : *pgTop) {
            if (!pg.visible) {
                pg.visible = true;
                copperFilterChanged = true;
            }
        }
    }
    if (pgBot) {
        for (auto& pg : *pgBot) {
            if (!pg.visible) {
                pg.visible = true;
                copperFilterChanged = true;
            }
        }
    }

    if (copperFilterChanged)
        doRecomputeClearance();
    else {
        rebuildLayerPanel();
        g_canvas->redraw();
    }
}

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
    if (msg == WM_GETMINMAXINFO) {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 1100;
        mmi->ptMinTrackSize.y = 640;
        return 0;
    }

    if (msg == WM_KEYDOWN) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (ctrl && wParam == 'O') { doOpenKicadFolder(); return 0; }
        if (ctrl && wParam == 'G') { doGenerate(); return 0; }
        if (ctrl && wParam == 'R') { doReloadProject(); return 0; }
        if (ctrl && wParam == 'L') {
            if (g_logArea) g_logArea->clear();
            return 0;
        }

        if (wParam == VK_F5) { doReloadProject(); return 0; }
        if (wParam == VK_F6) { doFitCanvasToBoard(); return 0; }
        if (wParam == VK_F7) { doToggleGrid(); return 0; }
    }

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
        if (hCtrl == g_hLayerPanel || hCtrl == g_hwndCopperSide) {
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
    // Copper layer selector ComboBox change
    if (msg == WM_COMMAND && LOWORD(wParam) == 9600 && HIWORD(wParam) == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(g_hwndCopperSide, CB_GETCURSEL, 0, 0);
        CopperSide prev = g_copperSide;
        if      (sel == 1) g_copperSide = CopperSide::Top;
        else if (sel == 2) g_copperSide = CopperSide::Bottom;
        else if (sel == 3) g_copperSide = CopperSide::Drill;
        else               g_copperSide = CopperSide::Auto;
        syncMenuOptionCheckmarks();
        syncLayerPanelWithCopperSideSelection();
        if (g_copperSide != prev)
            scheduleAutoRefresh(true);
        return 0;
    }
    // Layer panel click handler — uses g_layerItems mapping
    if (msg == WM_COMMAND && LOWORD(wParam) == LP_ID && HIWORD(wParam) == LBN_SELCHANGE) {
        int idx = (int)SendMessageW(g_hLayerPanel, LB_GETCURSEL, 0, 0);
        if (idx >= 0 && idx < (int)g_layerItems.size() && g_canvas) {
            auto& item = g_layerItems[idx];
            if (!item.isSection && item.action == LayerPanelAction::SelectDrillOnlyMode) {
                selectDrillOnlyModeFromLayerPanel();
                rebuildLayerPanel();
                g_canvas->redraw();
            } else if (!item.isSection && item.flag) {
                *item.flag = !(*item.flag);

                // Sync cutout checkbox when cutout layer is toggled in panel
                if (g_chkCutout && item.flag == &g_canvas->layers().cutout)
                    g_chkCutout->setChecked(*item.flag);

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
    const int rowH = 28;

    // ── Row 1: Primary actions + quick workflow ──────────────────────────
    int y = 8;

    styleBtn(win, new Button(m, y, 110, rowH, "Open KiCad",
        [](Button*) { doOpenKicadFolder(); }),
        CLR_ACTION_BG, CLR_ACTION_TEXT, CLR_ACTION_HOVER);

    g_btnTool = new Button(m + 118, y, 268, rowH, "Tool",
        [](Button*) { showToolPopup(); });
    styleBtn(win, g_btnTool, CLR_TOOL_BG, CLR_TOOL_TEXT, CLR_TOOL_HOVER);
    updateToolButtonText();

    addLabel(win, m + 394, y + 6, 32, L"Mat:", 10, true);
    g_fldMaterial = addField(win, m + 428, y + 2, 62, "1.5");

    g_chkUseArcs = new CheckBox(m + 498, y + 3, 70, 24, "Arcs", true);
    win->add(g_chkUseArcs);
    styleCheck(g_chkUseArcs);

    g_btnGenerate = new Button(m + 576, y, 140, rowH, "Generate GCode",
        [](Button*) { doGenerate(); });
    styleBtn(win, g_btnGenerate, CLR_EXPORT_BG, CLR_EXPORT_TEXT, CLR_EXPORT_HOVER);
    g_btnGenerate->setFont(L"Segoe UI", 11, true);

    int qx = m + 726;
    const int qGap = 6;
    styleBtn(win, new Button(qx, y, 78, rowH, "Reload", [](Button*) { doReloadProject(); }),
        CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);
    qx += 78 + qGap;
    styleBtn(win, new Button(qx, y, 58, rowH, "Fit", [](Button*) { doFitCanvasToBoard(); }),
        CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);
    qx += 58 + qGap;
    styleBtn(win, new Button(qx, y, 58, rowH, "Reset", [](Button*) { doResetCanvasView(); }),
        CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);
    qx += 58 + qGap;
    styleBtn(win, new Button(qx, y, 58, rowH, "Grid", [](Button*) { doToggleGrid(); }),
        CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);
    qx += 58 + qGap;
    styleBtn(win, new Button(qx, y, 70, rowH, "All On", [](Button*) { setAllLayersVisible(true); }),
        CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);
    qx += 70 + qGap;
    styleBtn(win, new Button(qx, y, 70, rowH, "Focus", [](Button*) { applyCopperFocusPreset(); }),
        CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    // ── Row 2: Paths and I/O ─────────────────────────────────────────────
    y = 44;

    addLabel(win, m, y + 5, 42, L"KiCad:", 10, true);
    g_fldKicadDir = addField(win, m + 44, y, 424, "");
    g_fldKicadDir->setMaxLength(512);

    Button* btnBrowse = new Button(m + 472, y, 28, rowH, "...",
        [](Button*) { doOpenKicadFolder(); });
    styleBtn(win, btnBrowse, CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    addLabel(win, m + 510, y + 5, 48, L"Output:", 10, true);
    g_fldOutputFile = addField(win, m + 560, y, 530, "");
    g_fldOutputFile->setMaxLength(512);

    Button* btnBrowseOut = new Button(m + 1094, y, 28, rowH, "...",
        [](Button*) {
            auto p = saveFileDialogUTF8(g_window->getHandle(),
                L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                L"Save GCode", L"gcode");
            if (!p.empty()) g_fldOutputFile->setText(p.c_str());
        });
    styleBtn(win, btnBrowseOut, CLR_BTN_BG, CLR_BTN_TEXT, CLR_BTN_HOVER);

    addLabel(win, m + 1132, y + 5, 360,
        L"Ctrl+O  Ctrl+G  Ctrl+R  Ctrl+L  F5  F6  F7",
        9, false);

    // ── Row 3: Copper layer selector ─────────────────────────────────────
    y = 78;

    addSectionLabel(win, m, y + 2, 70, L"Layer:");

    // Native ComboBox (CBS_DROPDOWNLIST — not editable) for copper side selection
    g_hwndCopperSide = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        m + 72, y, 200, 200,  // height 200 = dropdown list area
        win->getHandle(), (HMENU)(intptr_t)9600, _core.hInstance, NULL);
    if (g_hwndCopperSide) {
        SendMessageW(g_hwndCopperSide, WM_SETFONT,
            (WPARAM)CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"), TRUE);
        SendMessageW(g_hwndCopperSide, CB_ADDSTRING, 0, (LPARAM)L"Auto (detect)");
        SendMessageW(g_hwndCopperSide, CB_ADDSTRING, 0, (LPARAM)L"F_Cu — Top");
        SendMessageW(g_hwndCopperSide, CB_ADDSTRING, 0, (LPARAM)L"B_Cu — Bottom  \u2194");
        SendMessageW(g_hwndCopperSide, CB_ADDSTRING, 0, (LPARAM)L"Drill");
        SendMessageW(g_hwndCopperSide, CB_SETCURSEL, 0, 0);
    }

    addLabel(win, m + 290, y + 4, 44, L"Cut:", 10, true);
    g_chkCutout = new CheckBox(m + 334, y + 2, 100, 25, "Cutout", false,
        [](CheckBox* chk, bool checked) {
            if (g_canvas) {
                g_canvas->layers().cutout = checked;
                rebuildLayerPanel();
                g_canvas->redraw();
            }
        });
    win->add(g_chkCutout);
    styleCheck(g_chkCutout);

    addLabel(win, m + 444, y + 4, 480,
        L"Layer: choose copper side to isolate or Drill for drilling-only mode. Check \"Cutout\" to mill board outline.",
        9, false);

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

    // ── Menu bar ─────────────────────────────────────────────────────────
    {
        HMENU hFile  = CreatePopupMenu();
        HMENU hView  = CreatePopupMenu();
        HMENU hOpts  = CreatePopupMenu();
        HMENU hTools = CreatePopupMenu();
        HMENU hHelp  = CreatePopupMenu();

        AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN,   L"&Open KiCad folder...\tCtrl+O");
        AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVEAS, L"Set &output GCode file...");
        AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT,   L"E&xit");

        AppendMenuW(hView, MF_STRING, IDM_VIEW_RELOAD, L"&Reload project\tF5");
        AppendMenuW(hView, MF_STRING, IDM_VIEW_FIT,    L"&Fit to board\tF6");
        AppendMenuW(hView, MF_STRING, IDM_VIEW_RESET,  L"Reset &view");
        AppendMenuW(hView, MF_STRING, IDM_VIEW_GRID,   L"Toggle &grid\tF7");
        AppendMenuW(hView, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hView, MF_STRING, IDM_VIEW_ALLON,  L"All layers &on");
        AppendMenuW(hView, MF_STRING, IDM_VIEW_FOCUS,  L"&Focus copper");

        AppendMenuW(hOpts, MF_STRING | (g_copperSide == CopperSide::Bottom ? MF_CHECKED : 0), IDM_OPT_FLIP,    L"&Flip board (B_Cu / mirror X)");
        AppendMenuW(hOpts, MF_STRING | (g_optIgnoreVia       ? MF_CHECKED : 0), IDM_OPT_NOVIAS,  L"Ignore &via holes");
        AppendMenuW(hOpts, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hOpts, MF_STRING | (g_optEngraverSpindle ? MF_CHECKED : 0), IDM_OPT_SPINDLE, L"&Engraver spindle M3");

        AppendMenuW(hTools, MF_STRING, IDM_TOOLS_MANAGE, L"&Manage tools...");

        AppendMenuW(hHelp, MF_STRING, IDM_HELP_ABOUT, L"&About gerber2gcode...");

        HMENU hBar = CreateMenu();
        AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hFile,  L"&File");
        AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hView,  L"&View");
        AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hOpts,  L"&Options");
        AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hTools, L"&Tools");
        AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hHelp,  L"&Help");

        g_hMenuBar = hBar;
        win->setMenu(hBar);
    }

    win->onMenuCommand([](int cmd) {
        switch (cmd) {
            case IDM_FILE_OPEN:
                doOpenKicadFolder();
                break;
            case IDM_FILE_SAVEAS: {
                auto p = saveFileDialogUTF8(g_window->getHandle(),
                    L"GCode (*.gcode)\0*.gcode\0All (*.*)\0*.*\0",
                    L"Save GCode", L"gcode");
                if (!p.empty() && g_fldOutputFile)
                    g_fldOutputFile->setText(p.c_str());
                break;
            }
            case IDM_FILE_EXIT:
                PostMessageW(g_window->getHandle(), WM_CLOSE, 0, 0);
                break;
            case IDM_VIEW_RELOAD:  doReloadProject();       break;
            case IDM_VIEW_FIT:     doFitCanvasToBoard();    break;
            case IDM_VIEW_RESET:   doResetCanvasView();     break;
            case IDM_VIEW_GRID:    doToggleGrid();          break;
            case IDM_VIEW_ALLON:   setAllLayersVisible(true); break;
            case IDM_VIEW_FOCUS:   applyCopperFocusPreset(); break;
            case IDM_OPT_FLIP:
                // Toggle between Auto and Bottom
                g_copperSide = (g_copperSide == CopperSide::Bottom) ? CopperSide::Auto : CopperSide::Bottom;
                syncMenuOptionCheckmarks();
                syncCopperLayerCombo();
                scheduleAutoRefresh(true);
                break;
            case IDM_OPT_NOVIAS:
                g_optIgnoreVia = !g_optIgnoreVia;
                syncMenuOptionCheckmarks();
                scheduleAutoRefresh(true);
                break;
            case IDM_OPT_SPINDLE:
                g_optEngraverSpindle = !g_optEngraverSpindle;
                syncMenuOptionCheckmarks();
                break;
            case IDM_TOOLS_MANAGE:
                doShowToolPresets();
                break;
            case IDM_HELP_ABOUT:
                MessageBoxW(g_window ? g_window->getHandle() : NULL,
                    L"gerber2gcode\r\n"
                    L"CNC PCB Isolation Router\r\n"
                    L"\r\n"
                    L"Konwertuje pliki KiCad Gerber (RS-274X) i Excellon\r\n"
                    L"na G-Code dla frezarek CNC.\r\n"
                    L"\r\n"
                    L"Format wyjściowy: FluidNC-compatible G-Code\r\n"
                    L"\r\n"
                    L"Skróty klawiszowe:\r\n"
                    L"  Ctrl+O  — otwórz folder KiCad\r\n"
                    L"  Ctrl+G  — generuj G-Code\r\n"
                    L"  Ctrl+R  — przeładuj projekt\r\n"
                    L"  Ctrl+L  — wyczyść log\r\n"
                    L"  F5      — przeładuj projekt\r\n"
                    L"  F6      — dopasuj widok\r\n"
                    L"  F7      — siatka\r\n"
                    L"\r\n"
                    L"Workflow:\r\n"
                    L"  1. Otwórz folder KiCad (Ctrl+O)\r\n"
                    L"  2. Wybierz preset narzędzia (▼ Tool)\r\n"
                    L"  3. Ustaw grubość laminatu (Mat)\r\n"
                    L"  4. Wygeneruj G-Code (Ctrl+G)\r\n"
                    L"\r\n"
                    L"Biblioteki:\r\n"
                    L"  Clipper2 \u00A9 Angus Johnson\r\n"
                    L"  JQB_WindowsLib \u00A9 JAQUBA",
                    L"O programie — gerber2gcode",
                    MB_OK | MB_ICONINFORMATION);
                break;
        }
    });

    doResize(rc.right, rc.bottom);
}
