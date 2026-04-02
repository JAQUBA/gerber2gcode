#pragma once
#include <UI/CanvasWindow/CanvasWindow.h>
#include "Geometry/Geometry.h"
#include "Toolpath/Toolpath.h"
#include "Drill/DrillParser.h"
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
// Layer visibility flags — grouped
// ════════════════════════════════════════════════════════════════════════════

struct LayerVisibility {
    // ── Board ──
    bool outline        = true;

    // ── Copper ──
    bool copperTop      = true;
    bool copperBottom   = false;

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
    void setMaskTop(const geo::Paths* p)        { m_maskTop = p; m_presence.maskTop = (p && !p->empty()); }
    void setMaskBottom(const geo::Paths* p)     { m_maskBottom = p; m_presence.maskBottom = (p && !p->empty()); }
    void setSilkTop(const geo::Paths* p)        { m_silkTop = p; m_presence.silkTop = (p && !p->empty()); }
    void setSilkBottom(const geo::Paths* p)     { m_silkBottom = p; m_presence.silkBottom = (p && !p->empty()); }
    void setPasteTop(const geo::Paths* p)       { m_pasteTop = p; m_presence.pasteTop = (p && !p->empty()); }
    void setPasteBottom(const geo::Paths* p)    { m_pasteBottom = p; m_presence.pasteBottom = (p && !p->empty()); }

    // Data setters — drills
    void setDrillsPTH(const std::vector<DrillHole>* p)  { m_drillsPTH = p; m_presence.drillsPTH = (p && !p->empty()); }
    void setDrillsNPTH(const std::vector<DrillHole>* p) { m_drillsNPTH = p; m_presence.drillsNPTH = (p && !p->empty()); }

    // Data setters — generated
    void setClearance(const geo::Paths* p)                          { m_clearance = p; m_presence.clearance = (p && !p->empty()); }
    void setContours(const std::vector<ToolpathContour>* p)         { m_contours = p; m_presence.isolation = (p && !p->empty()); }

    void zoomToFit(double boardW, double boardH);
    void clearData();

protected:
    void onDraw(HDC hdc, const RECT& rc) override;

private:
    void drawOutline(HDC hdc);
    void drawPolygons(HDC hdc, const geo::Paths* paths, COLORREF color);
    void drawIsolation(HDC hdc);
    void drawDrills(HDC hdc, const std::vector<DrillHole>* drills, COLORREF color);

    LayerVisibility m_layers;
    LayerPresence   m_presence;

    // Input layers
    const geo::Path*    m_outline       = nullptr;
    const geo::Paths*   m_copperTop     = nullptr;
    const geo::Paths*   m_copperBottom  = nullptr;
    const geo::Paths*   m_maskTop       = nullptr;
    const geo::Paths*   m_maskBottom    = nullptr;
    const geo::Paths*   m_silkTop       = nullptr;
    const geo::Paths*   m_silkBottom    = nullptr;
    const geo::Paths*   m_pasteTop      = nullptr;
    const geo::Paths*   m_pasteBottom   = nullptr;

    // Drills
    const std::vector<DrillHole>*   m_drillsPTH     = nullptr;
    const std::vector<DrillHole>*   m_drillsNPTH    = nullptr;

    // Generated
    const geo::Paths*                       m_clearance = nullptr;
    const std::vector<ToolpathContour>*     m_contours  = nullptr;
};
