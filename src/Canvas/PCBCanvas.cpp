#include "PCBCanvas.h"
#include <cmath>
#include <map>
#include <cstdio>

// ════════════════════════════════════════════════════════════════════════════
// Colors — layer palette
// ════════════════════════════════════════════════════════════════════════════

static const COLORREF CLR_OUTLINE       = RGB(220, 220, 60);   // Board edge — bright yellow
static const COLORREF CLR_COPPER_TOP    = RGB(200, 70, 60);    // F_Cu — red
static const COLORREF CLR_COPPER_BOT    = RGB(50, 80, 200);    // B_Cu — blue
static const COLORREF CLR_COPPER_TOP_TRACE  = RGB(200, 70, 60);    // F_Cu traces — same as copper top
static const COLORREF CLR_COPPER_TOP_PAD    = RGB(220, 100, 80);   // F_Cu pads — lighter red
static const COLORREF CLR_COPPER_TOP_REGION = RGB(170, 50, 40);    // F_Cu regions — darker red
static const COLORREF CLR_COPPER_BOT_TRACE  = RGB(50, 80, 200);    // B_Cu traces — same as copper bot
static const COLORREF CLR_COPPER_BOT_PAD    = RGB(80, 110, 220);   // B_Cu pads — lighter blue
static const COLORREF CLR_COPPER_BOT_REGION = RGB(30, 55, 170);    // B_Cu regions — darker blue
static const COLORREF CLR_MASK_TOP      = RGB(30, 130, 50);    // F_Mask — green
static const COLORREF CLR_MASK_BOT      = RGB(30, 100, 130);   // B_Mask — teal
static const COLORREF CLR_SILK_TOP      = RGB(200, 200, 100);  // F_Silk — yellow
static const COLORREF CLR_SILK_BOT      = RGB(160, 130, 200);  // B_Silk — purple
static const COLORREF CLR_PASTE_TOP     = RGB(140, 140, 140);  // F_Paste — gray
static const COLORREF CLR_PASTE_BOT     = RGB(110, 110, 130);  // B_Paste — slate
static const COLORREF CLR_PTH           = RGB(80, 220, 80);    // PTH drills — green
static const COLORREF CLR_NPTH          = RGB(220, 160, 40);   // NPTH drills — orange
static const COLORREF CLR_CLEARANCE     = RGB(80, 80, 80);     // Clearance — dim gray
static const COLORREF CLR_ISOLATION     = RGB(80, 200, 240);   // Isolation — light blue
static const COLORREF CLR_CUTOUT       = RGB(240, 140, 40);   // Cutout — orange
static const COLORREF CLR_DRILL_MARK    = RGB(40, 160, 40);    // Drill center cross
static const COLORREF CLR_ORIGIN        = RGB(120, 120, 140);  // Origin marker

// ════════════════════════════════════════════════════════════════════════════
// GDI helper — RAII pen/brush wrapper
// ════════════════════════════════════════════════════════════════════════════

struct GdiPen {
    HPEN pen;
    HPEN old;
    HDC  hdc;
    GdiPen(HDC dc, COLORREF color, int width = 1)
        : hdc(dc), pen(CreatePen(PS_SOLID, width, color)) {
        old = (HPEN)SelectObject(hdc, pen);
    }
    ~GdiPen() { SelectObject(hdc, old); DeleteObject(pen); }
};

struct GdiBrush {
    HBRUSH brush;
    HBRUSH old;
    HDC    hdc;
    GdiBrush(HDC dc, COLORREF color)
        : hdc(dc), brush(CreateSolidBrush(color)) {
        old = (HBRUSH)SelectObject(hdc, brush);
    }
    ~GdiBrush() { SelectObject(hdc, old); DeleteObject(brush); }
};

// ════════════════════════════════════════════════════════════════════════════
// zoomToFit
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::zoomToFit(double boardW, double boardH) {
    if (boardW < 0.001 || boardH < 0.001) return;

    RECT rc;
    GetClientRect(getHandle(), &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;

    double zx = (cw * 0.85) / (boardW * 10.0);
    double zy = (ch * 0.85) / (boardH * 10.0);
    double zoom = (zx < zy) ? zx : zy;

    double cx = boardW * 0.5;
    double cy = boardH * 0.5;
    double panX = cw * 0.5 - cx * zoom * 10.0;
    double panY = ch * 0.5 - cy * zoom * 10.0;

    setDefaultZoom(zoom);
    setDefaultPan(panX, panY);
    resetView();
    redraw();
}

// ════════════════════════════════════════════════════════════════════════════
// clearData
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::clearData() {
    m_outline = nullptr;
    m_copperTop = nullptr; m_copperBottom = nullptr;
    m_copperTopTraces = nullptr; m_copperTopPads = nullptr; m_copperTopRegions = nullptr;
    m_copperBottomTraces = nullptr; m_copperBottomPads = nullptr; m_copperBottomRegions = nullptr;
    m_copperTopPadGroups = nullptr; m_copperBottomPadGroups = nullptr;
    m_maskTop = nullptr;   m_maskBottom = nullptr;
    m_silkTop = nullptr;   m_silkBottom = nullptr;
    m_pasteTop = nullptr;  m_pasteBottom = nullptr;
    m_drillsPTH = nullptr; m_drillsNPTH = nullptr;
    m_clearance = nullptr; m_contours = nullptr; m_cutout = nullptr;
    m_presence = LayerPresence();
    redraw();
}

// ════════════════════════════════════════════════════════════════════════════
// onDraw — main rendering entry
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::onDraw(HDC hdc, const RECT& rc) {
    // Origin marker
    {
        GdiPen pen(hdc, CLR_ORIGIN, 1);
        int sx = toScreenX(0), sy = toScreenY(0);
        MoveToEx(hdc, sx - 12, sy, NULL); LineTo(hdc, sx + 12, sy);
        MoveToEx(hdc, sx, sy - 12, NULL); LineTo(hdc, sx, sy + 12);
    }

    // Render back-to-front: paste → mask → clearance → copper → silk → outline → isolation → drills
    if (m_layers.pasteBottom)   drawPolygons(hdc, m_pasteBottom, CLR_PASTE_BOT);
    if (m_layers.pasteTop)      drawPolygons(hdc, m_pasteTop,    CLR_PASTE_TOP);
    if (m_layers.maskBottom)    drawPolygons(hdc, m_maskBottom,  CLR_MASK_BOT);
    if (m_layers.maskTop)       drawPolygons(hdc, m_maskTop,     CLR_MASK_TOP);
    if (m_layers.clearance)     drawPolygons(hdc, m_clearance,   CLR_CLEARANCE);

    // Copper bottom — sub-layers if available, else full layer
    if (m_layers.copperBottom) {
        if (m_copperBottomTraces || m_copperBottomPads || m_copperBottomRegions) {
            if (m_layers.copperBottomSub.regions) drawPolygons(hdc, m_copperBottomRegions, CLR_COPPER_BOT_REGION);
            if (m_layers.copperBottomSub.traces)  drawPolygons(hdc, m_copperBottomTraces,  CLR_COPPER_BOT_TRACE);
            if (m_layers.copperBottomSub.pads) {
                if (m_copperBottomPadGroups && !m_copperBottomPadGroups->empty()) {
                    for (auto& pg : *m_copperBottomPadGroups)
                        if (pg.visible) drawPolygons(hdc, &pg.paths, CLR_COPPER_BOT_PAD);
                } else {
                    drawPolygons(hdc, m_copperBottomPads, CLR_COPPER_BOT_PAD);
                }
            }
        } else {
            drawPolygons(hdc, m_copperBottom, CLR_COPPER_BOT);
        }
    }

    // Copper top — sub-layers if available, else full layer
    if (m_layers.copperTop) {
        if (m_copperTopTraces || m_copperTopPads || m_copperTopRegions) {
            if (m_layers.copperTopSub.regions) drawPolygons(hdc, m_copperTopRegions, CLR_COPPER_TOP_REGION);
            if (m_layers.copperTopSub.traces)  drawPolygons(hdc, m_copperTopTraces,  CLR_COPPER_TOP_TRACE);
            if (m_layers.copperTopSub.pads) {
                if (m_copperTopPadGroups && !m_copperTopPadGroups->empty()) {
                    for (auto& pg : *m_copperTopPadGroups)
                        if (pg.visible) drawPolygons(hdc, &pg.paths, CLR_COPPER_TOP_PAD);
                } else {
                    drawPolygons(hdc, m_copperTopPads, CLR_COPPER_TOP_PAD);
                }
            }
        } else {
            drawPolygons(hdc, m_copperTop, CLR_COPPER_TOP);
        }
    }

    if (m_layers.silkBottom)    drawPolygons(hdc, m_silkBottom,  CLR_SILK_BOT);
    if (m_layers.silkTop)       drawPolygons(hdc, m_silkTop,     CLR_SILK_TOP);
    if (m_layers.outline)       drawOutline(hdc);
    if (m_layers.isolation)     drawIsolation(hdc);
    if (m_layers.cutout)        drawCutout(hdc);
    if (m_layers.drillsNPTH)    drawDrills(hdc, m_drillsNPTH, m_drillFilterNPTH, CLR_NPTH);
    if (m_layers.drillsPTH)     drawDrills(hdc, m_drillsPTH,  m_drillFilterPTH,  CLR_PTH);
}

// ════════════════════════════════════════════════════════════════════════════
// drawOutline — board edge as closed polyline
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::drawOutline(HDC hdc) {
    if (!m_outline || m_outline->size() < 2) return;

    GdiPen pen(hdc, CLR_OUTLINE, 2);
    auto& pts = *m_outline;

    MoveToEx(hdc, toScreenX(pts[0].x), toScreenY(pts[0].y), NULL);
    for (size_t i = 1; i < pts.size(); i++)
        LineTo(hdc, toScreenX(pts[i].x), toScreenY(pts[i].y));
    LineTo(hdc, toScreenX(pts[0].x), toScreenY(pts[0].y));
}

// ════════════════════════════════════════════════════════════════════════════
// drawPolygons — generic filled polygon renderer
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::drawPolygons(HDC hdc, const geo::Paths* paths, COLORREF color) {
    if (!paths || paths->empty()) return;

    GdiPen   pen(hdc, color, 1);
    GdiBrush brush(hdc, color);
    SetPolyFillMode(hdc, WINDING);

    for (auto& path : *paths) {
        if (path.size() < 3) continue;
        std::vector<POINT> screenPts(path.size());
        for (size_t i = 0; i < path.size(); i++) {
            screenPts[i].x = toScreenX(path[i].x);
            screenPts[i].y = toScreenY(path[i].y);
        }
        ::Polygon(hdc, screenPts.data(), (int)screenPts.size());
    }
}

// ════════════════════════════════════════════════════════════════════════════
// drawIsolation — toolpath contours as polylines
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::drawIsolation(HDC hdc) {
    if (!m_contours || m_contours->empty()) return;

    GdiPen pen(hdc, CLR_ISOLATION, 1);

    for (auto& contour : *m_contours) {
        auto& pts = contour.points;
        if (pts.size() < 2) continue;

        MoveToEx(hdc, toScreenX(pts[0].x), toScreenY(pts[0].y), NULL);
        for (size_t i = 1; i < pts.size(); i++)
            LineTo(hdc, toScreenX(pts[i].x), toScreenY(pts[i].y));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// drawCutout — board cutout path as closed polyline (orange)
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::drawCutout(HDC hdc) {
    if (!m_cutout || m_cutout->size() < 2) return;

    GdiPen pen(hdc, CLR_CUTOUT, 2);
    auto& pts = *m_cutout;

    MoveToEx(hdc, toScreenX(pts[0].x), toScreenY(pts[0].y), NULL);
    for (size_t i = 1; i < pts.size(); i++)
        LineTo(hdc, toScreenX(pts[i].x), toScreenY(pts[i].y));
    // Close the loop
    LineTo(hdc, toScreenX(pts[0].x), toScreenY(pts[0].y));
}

// ════════════════════════════════════════════════════════════════════════════
// drawDrills — drill holes as circles with center cross (filtered by diameter)
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::drawDrills(HDC hdc, const std::vector<DrillHole>* drills,
                           const std::vector<DrillFilter>& filters, COLORREF color) {
    if (!drills || drills->empty()) return;

    // Build set of hidden diameters for fast lookup
    std::map<int, bool> hiddenDia; // key = diameter * 1000 (integer microns)
    for (auto& f : filters) {
        if (!f.visible)
            hiddenDia[(int)(f.diameter * 1000 + 0.5)] = true;
    }

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    for (auto& d : *drills) {
        // Skip holes with hidden diameter
        int diaKey = (int)(d.diameter * 1000 + 0.5);
        if (hiddenDia.count(diaKey)) continue;

        double r = d.diameter * 0.5;

        // Circle outline
        {
            GdiPen pen(hdc, color, 1);
            int sx = toScreenX(d.x - r);
            int sy = toScreenY(d.y + r);
            int ex = toScreenX(d.x + r);
            int ey = toScreenY(d.y - r);
            Ellipse(hdc, sx, sy, ex, ey);
        }

        // Center cross
        {
            GdiPen pen(hdc, CLR_DRILL_MARK, 1);
            double s = r * 0.4;
            MoveToEx(hdc, toScreenX(d.x - s), toScreenY(d.y), NULL);
            LineTo(hdc, toScreenX(d.x + s), toScreenY(d.y));
            MoveToEx(hdc, toScreenX(d.x), toScreenY(d.y - s), NULL);
            LineTo(hdc, toScreenX(d.x), toScreenY(d.y + s));
        }
    }

    SelectObject(hdc, oldBrush);
}

// ════════════════════════════════════════════════════════════════════════════
// buildDrillFilters — group drill holes by diameter, preserve visibility
// ════════════════════════════════════════════════════════════════════════════

void PCBCanvas::buildDrillFilters(const std::vector<DrillHole>* drills,
                                   std::vector<DrillFilter>& filters) {
    // Preserve old visibility state
    std::map<int, bool> oldState; // key = diameter * 1000
    for (auto& f : filters)
        oldState[(int)(f.diameter * 1000 + 0.5)] = f.visible;

    filters.clear();
    if (!drills || drills->empty()) return;

    // Count holes per diameter
    std::map<int, std::pair<double, int>> groups; // key → (diameter, count)
    for (auto& h : *drills) {
        int key = (int)(h.diameter * 1000 + 0.5);
        if (groups.count(key))
            groups[key].second++;
        else
            groups[key] = {h.diameter, 1};
    }

    // Build sorted filter list
    for (auto& [key, grp] : groups) {
        DrillFilter f;
        f.diameter = grp.first;
        f.count    = grp.second;
        f.visible  = oldState.count(key) ? oldState[key] : true;
        filters.push_back(f);
    }
}
