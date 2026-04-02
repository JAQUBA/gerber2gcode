#pragma once
#include <UI/CanvasWindow/CanvasWindow.h>
#include "Geometry/Geometry.h"
#include "Toolpath/Toolpath.h"
#include "Drill/DrillParser.h"
#include "Gerber/GerberParser.h"
#include <vector>
#include <map>

// ════════════════════════════════════════════════════════════════════════════
// Drill diameter filter — per-diameter visibility for drill sub-items
// ════════════════════════════════════════════════════════════════════════════

struct DrillFilter {
    double diameter;
    int    count;
    bool   visible = true;
};

// ════════════════════════════════════════════════════════════════════════════
// Copper sub-layer visibility — per-component toggles
// ════════════════════════════════════════════════════════════════════════════

struct CopperSubVis {
    bool traces  = true;
    bool pads    = true;
    bool regions = true;
};

struct CopperSubPresence {
    bool traces  = false;
    bool pads    = false;
    bool regions = false;
};

// ════════════════════════════════════════════════════════════════════════════
// Layer visibility flags — grouped
// ════════════════════════════════════════════════════════════════════════════

struct LayerVisibility {
    // ── Board ──
    bool outline        = true;

    // ── Copper ──
    bool copperTop      = true;
    bool copperBottom   = false;
    CopperSubVis copperTopSub;
    CopperSubVis copperBottomSub;

    // ── Other layers ──
    bool maskTop        = false;
    bool maskBottom     = false;
    bool silkTop        = false;
    bool silkBottom     = false;
    bool pasteTop       = false;
    bool pasteBottom    = false;

    // ── Drills ──
    bool drillsPTH      = true;
    bool drillsNPTH     = true;

    // ── Generated ──
    bool clearance      = false;
    bool isolation      = true;
    bool cutout         = false;
};

// ════════════════════════════════════════════════════════════════════════════
// Layer presence — which layers have data to show
// ════════════════════════════════════════════════════════════════════════════

struct LayerPresence {
    bool outline        = false;
    bool copperTop      = false;
    bool copperBottom   = false;
    CopperSubPresence copperTopSub;
    CopperSubPresence copperBottomSub;
    bool maskTop        = false;
    bool maskBottom     = false;
    bool silkTop        = false;
    bool silkBottom     = false;
    bool pasteTop       = false;
    bool pasteBottom    = false;
    bool drillsPTH      = false;
    bool drillsNPTH     = false;
    bool clearance      = false;
    bool isolation      = false;
    bool cutout         = false;
};

// ════════════════════════════════════════════════════════════════════════════
// PCBCanvas — GDI rendering of PCB data for preview
// ════════════════════════════════════════════════════════════════════════════

class PCBCanvas : public CanvasWindow {
public:
    LayerVisibility& layers()   { return m_layers; }
    LayerPresence&   presence() { return m_presence; }

    // Data setters — input layers
    void setOutline(const geo::Path* p)         { m_outline = p; m_presence.outline = (p && !p->empty()); }
    void setCopperTop(const geo::Paths* p)      { m_copperTop = p; m_presence.copperTop = (p && !p->empty()); }
    void setCopperBottom(const geo::Paths* p)   { m_copperBottom = p; m_presence.copperBottom = (p && !p->empty()); }

    // Copper sub-components (traces/pads/regions)
    void setCopperTopTraces(const geo::Paths* p)    { m_copperTopTraces = p; m_presence.copperTopSub.traces = (p && !p->empty()); }
    void setCopperTopPads(const geo::Paths* p)      { m_copperTopPads = p; m_presence.copperTopSub.pads = (p && !p->empty()); }
    void setCopperTopRegions(const geo::Paths* p)   { m_copperTopRegions = p; m_presence.copperTopSub.regions = (p && !p->empty()); }
    void setCopperBottomTraces(const geo::Paths* p) { m_copperBottomTraces = p; m_presence.copperBottomSub.traces = (p && !p->empty()); }
    void setCopperBottomPads(const geo::Paths* p)   { m_copperBottomPads = p; m_presence.copperBottomSub.pads = (p && !p->empty()); }
    void setCopperBottomRegions(const geo::Paths* p){ m_copperBottomRegions = p; m_presence.copperBottomSub.regions = (p && !p->empty()); }

    CopperSubVis& copperTopSubVis()    { return m_layers.copperTopSub; }
    CopperSubVis& copperBottomSubVis() { return m_layers.copperBottomSub; }

    // Pad groups — per-aperture sub-items (pointers to GerberComponents::padGroups)
    void setCopperTopPadGroups(std::vector<PadGroup>* p)    { m_copperTopPadGroups = p; }
    void setCopperBottomPadGroups(std::vector<PadGroup>* p) { m_copperBottomPadGroups = p; }
    std::vector<PadGroup>* copperTopPadGroups()     { return m_copperTopPadGroups; }
    std::vector<PadGroup>* copperBottomPadGroups()  { return m_copperBottomPadGroups; }

    void setMaskTop(const geo::Paths* p)        { m_maskTop = p; m_presence.maskTop = (p && !p->empty()); }
    void setMaskBottom(const geo::Paths* p)     { m_maskBottom = p; m_presence.maskBottom = (p && !p->empty()); }
    void setSilkTop(const geo::Paths* p)        { m_silkTop = p; m_presence.silkTop = (p && !p->empty()); }
    void setSilkBottom(const geo::Paths* p)     { m_silkBottom = p; m_presence.silkBottom = (p && !p->empty()); }
    void setPasteTop(const geo::Paths* p)       { m_pasteTop = p; m_presence.pasteTop = (p && !p->empty()); }
    void setPasteBottom(const geo::Paths* p)    { m_pasteBottom = p; m_presence.pasteBottom = (p && !p->empty()); }

    // Data setters — drills
    void setDrillsPTH(const std::vector<DrillHole>* p)  {
        m_drillsPTH = p; m_presence.drillsPTH = (p && !p->empty());
        buildDrillFilters(p, m_drillFilterPTH);
    }
    void setDrillsNPTH(const std::vector<DrillHole>* p) {
        m_drillsNPTH = p; m_presence.drillsNPTH = (p && !p->empty());
        buildDrillFilters(p, m_drillFilterNPTH);
    }

    // Drill diameter filters
    std::vector<DrillFilter>& drillFilterPTH()  { return m_drillFilterPTH; }
    std::vector<DrillFilter>& drillFilterNPTH() { return m_drillFilterNPTH; }

    // Data setters — generated
    void setClearance(const geo::Paths* p)                          { m_clearance = p; m_presence.clearance = (p && !p->empty()); }
    void setContours(const std::vector<ToolpathContour>* p)         { m_contours = p; m_presence.isolation = (p && !p->empty()); }
    void setCutout(const std::vector<geo::Point>* p)                { m_cutout = p; m_presence.cutout = (p && !p->empty()); }

    void zoomToFit(double boardW, double boardH);
    void clearData();

protected:
    void onDraw(HDC hdc, const RECT& rc) override;

private:
    void drawOutline(HDC hdc);
    void drawPolygons(HDC hdc, const geo::Paths* paths, COLORREF color);
    void drawIsolation(HDC hdc);
    void drawCutout(HDC hdc);
    void drawDrills(HDC hdc, const std::vector<DrillHole>* drills,
                    const std::vector<DrillFilter>& filters, COLORREF color);
    void buildDrillFilters(const std::vector<DrillHole>* drills,
                           std::vector<DrillFilter>& filters);

    LayerVisibility m_layers;
    LayerPresence   m_presence;

    // Input layers
    const geo::Path*    m_outline       = nullptr;
    const geo::Paths*   m_copperTop     = nullptr;
    const geo::Paths*   m_copperBottom  = nullptr;
    const geo::Paths*   m_copperTopTraces   = nullptr;
    const geo::Paths*   m_copperTopPads     = nullptr;
    const geo::Paths*   m_copperTopRegions  = nullptr;
    const geo::Paths*   m_copperBottomTraces  = nullptr;
    const geo::Paths*   m_copperBottomPads    = nullptr;
    const geo::Paths*   m_copperBottomRegions = nullptr;
    std::vector<PadGroup>* m_copperTopPadGroups    = nullptr;
    std::vector<PadGroup>* m_copperBottomPadGroups = nullptr;
    const geo::Paths*   m_maskTop       = nullptr;
    const geo::Paths*   m_maskBottom    = nullptr;
    const geo::Paths*   m_silkTop       = nullptr;
    const geo::Paths*   m_silkBottom    = nullptr;
    const geo::Paths*   m_pasteTop      = nullptr;
    const geo::Paths*   m_pasteBottom   = nullptr;

    // Drills
    const std::vector<DrillHole>*   m_drillsPTH     = nullptr;
    const std::vector<DrillHole>*   m_drillsNPTH    = nullptr;
    std::vector<DrillFilter>        m_drillFilterPTH;
    std::vector<DrillFilter>        m_drillFilterNPTH;

    // Generated
    const geo::Paths*                       m_clearance = nullptr;
    const std::vector<ToolpathContour>*     m_contours  = nullptr;
    const std::vector<geo::Point>*          m_cutout    = nullptr;
};
