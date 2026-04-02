# Copilot Instructions — gerber2gcode

> **IMPORTANT:** Keep this file and `README.md` up to date whenever you add, rename, or remove modules, change data structures, add UI components, modify G-Code output, or change configuration keys.

## Project Description

Native Windows desktop application (C++) for converting KiCad Gerber (RS-274X) and Excellon drill files to G-Code for CNC PCB isolation routing and drilling. Parses multi-layer Gerber files, renders a real-time GDI preview with pan/zoom, generates Clipper2-based contour-parallel isolation toolpaths, optimizes path ordering with nearest-neighbor + 2-opt TSP, and exports GRBL-compatible G-Code. Built with JQB_WindowsLib.

## Architecture

```
gerber2gcode/
├── src/
│   ├── main.cpp                    # Entry point: init(), setup(), loop() — minimal lifecycle
│   ├── AppState.h / .cpp           # Global state, settings, tool presets, shared actions
│   ├── AppUI.h / .cpp              # Toolbar UI, canvas, layer panel, resize handler
│   ├── Canvas/
│   │   └── PCBCanvas.h / .cpp      # CanvasWindow subclass — GDI PCB rendering
│   ├── Config/
│   │   └── Config.h / .cpp         # MachineConfig, CamConfig, JobConfig structs + mini JSON loader
│   ├── Gerber/
│   │   └── GerberParser.h / .cpp   # RS-274X Gerber file parser → geo::Paths or GerberComponents (categorized: traces/pads/regions)
│   ├── Drill/
│   │   └── DrillParser.h / .cpp    # Excellon drill parser → DrillHole vector
│   ├── Geometry/
│   │   └── Geometry.h / .cpp       # geo:: namespace — Clipper2 wrappers, shape generators, boolean ops
│   ├── Toolpath/
│   │   └── Toolpath.h / .cpp       # Contour-parallel isolation generator + 2-opt contour ordering
│   ├── GCode/
│   │   └── GCodeGen.h / .cpp       # G-Code generator with drill ordering + time estimation
│   ├── Pipeline/
│   │   └── Pipeline.h / .cpp       # Orchestration: detect files → parse → isolate → generate
│   └── Debug/
│       └── DebugImage.h / .cpp     # Debug BMP output (re-parses G-Code for visual validation)
├── lib/
│   └── Clipper2/                   # Git submodule: Clipper2 polygon boolean & offset library
├── resources/
│   └── resources.rc               # Windows resource file (version info)
├── examples/                      # Example KiCad Gerber & Excellon files
└── platformio.ini                 # PlatformIO config (JQB_MinGW platform)
```

### Module Responsibilities

| Module | Responsibility |
|--------|---------------|
| **main.cpp** | `init()` (COM init), `setup()` (window + menu + UI + canvas), `loop()` (empty — event-driven). Minimal, delegates everything. |
| **AppState** | Global state (`g_window`, `g_canvas`, `g_logArea`, `g_progressBar`, `g_pipelineData`, all UI field pointers). `ToolPreset` struct. `loadSettings()` / `saveSettings()`. Tool preset management (`loadToolPresets`, `saveToolPresets`, `applyActiveToolPreset`, `doSelectTool`, `showToolPopup`, `doShowToolPresets`). Input field → Config conversion (`buildConfigFromGUI`). Shared actions: `doLoadKicadDir()`, `doGenerate()`, `doExportGCode()`. Auto-refresh: `scheduleAutoRefresh(bool)` debounce timer (400ms) + `doRefreshIsolation()` for isolation-only preview updates + `doRecomputeClearance()` for copper sub-layer visibility changes. Layer panel: `rebuildLayerPanel()`. Resize: `installResizeHandler()`. Logging: `logMsg()`. |
| **AppUI** | `createUI(SimpleWindow*)` — 3-row toolbar (Open KiCad / Export / Tool dropdown / parameter fields / Generate / checkboxes), canvas, layer panel, log area, progress bar. `doResize(w, h)` — dynamic layout. Button styling helpers. Browse/save file dialog wrappers. Isolation-affecting fields (Tip, Overlap, Offset) have `onTextChange` callbacks that trigger debounced auto-refresh. Position fields (X/Y offset) and checkboxes (Flip, No Vias) trigger full reparse. Browse KiCad button immediately loads and previews the selected directory. |
| **PCBCanvas** | Subclass of JQB_WindowsLib `CanvasWindow` — renders board outline, copper layers (top/bottom) with per-component sub-layers (traces/pads/regions in distinct color shades), mask, silk, paste, clearance, isolation contours, drill holes with center marks. `LayerVisibility` / `LayerPresence` with `CopperSubVis` / `CopperSubPresence` structs. `DrillFilter` groups holes by diameter for per-diameter visibility. `zoomToFit()`. Back-to-front rendering order. |
| **Config** | `Config` struct with `MachineConfig` (engraver Z, tip width, drill Z, feedrates, offsets), `CamConfig` (overlap, offset), `JobConfig` (engraver/spindle/laser feedrates). `loadConfig()` — minimal JSON parser. |
| **GerberParser** | RS-274X parser: FSLAX format, aperture definitions (Circle/Rect/Obround/Polygon/Macro), AM macro evaluation (full expression evaluator with primitives 1/4/5/7/20/21), D01/D02/D03, G36/G37 regions, G02/G03 arcs, G74/G75 quadrant modes, LPD/LPC polarity. Two output modes: `parseGerber()` → `geo::Paths` (flat union), `parseGerberComponents()` → `GerberComponents` (categorized: traces=D01, pads=D03, regions=G36/G37). `PadGroup` struct groups D03 flashes by aperture D-code with human-readable names (e.g. "Circle Ø0.800mm", "Rect 1.27×0.64mm"). `GerberComponents::combined()` unions all categories. `GerberComponents::visiblePads()` unions only visible pad groups. |
| **DrillParser** | Excellon parser: tool table (`TnnCdia`), coordinates, METRIC/INCH units, rout mode (M15/M16), slotted holes (G85 — skipped), via filtering. Outputs `std::vector<DrillHole>`. |
| **Geometry** | `geo::` namespace — Clipper2 type aliases (`Point`, `Path`, `Paths`). Shape generators: `makeCircle`, `makeRect`, `makeObround`, `makeRegPoly`. Boolean ops: `unionAll`, `difference`, `intersect`, `offset`. Utilities: `bufferLine`, `bufferPath`, `simplifyPaths`, `translate`, `flipX`, `isEmpty`, `totalArea`. |
| **Toolpath** | `generateToolpath(clearance, config)` — contour-parallel inward offset with configurable overlap. `orderContours()` — nearest-neighbor + 2-opt TSP optimization. |
| **GCodeGen** | `generateGCode(contours, holes, config, xOff, yOff)` — GRBL-compatible G0/G1 output with isolation + drilling sections. `orderDrillHoles()` — nearest-neighbor + 2-opt. `estimateJobTime()` — time estimate based on distances and feedrates. |
| **Pipeline** | `detectKicadFiles(dir)` — auto-detect layers by filename suffix. `runPipeline(params, log, result)` — full workflow (parse → normalize → clip → isolate → order → generate → export). `parsePipelineData(params, log)` — parse-only for live preview. |
| **DebugImage** | `generateDebugBMP(gcodePath, outputPath, config, holes)` — re-parses G-Code, renders as 24-bit BMP for visual validation. |

## Tech Stack

- **Language**: C++17
- **Build system**: PlatformIO (`platform = native` via JQB_MinGW)
- **UI framework**: [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) — lightweight Win32 UI library
- **Geometry library**: [Clipper2](https://github.com/AngusJohnson/Clipper2) — polygon boolean operations & offset (git submodule in `lib/Clipper2`)
- **Rendering**: WinAPI GDI (via reusable CanvasWindow — zoom/pan/grid inherited)
- **Target platform**: Windows 10+ (x64)

## Coding Conventions

### Code Style

- All code, comments, class names, methods, and variables in **English**
- Use `std::string` for file paths and internal data
- Use `std::wstring` and `L"..."` for displayed UI text
- Use section comments `// --- ... ---` and `// ====` banners for code organization
- Indentation: 4 spaces (no tabs)
- Explicit Wide WinAPI versions: `CreateFontW()`, `CreateWindowExW()`, `MessageBoxW()` etc.
- Do not use `std::thread` (use `CreateThread`), `std::to_wstring` (use `jqb_compat::to_wstring`)

### Application Pattern

- JQB_WindowsLib defines `init()`, `setup()`, and `loop()` — Arduino-like
- `init()` → COM init (CoInitializeEx)
- `setup()` → ConfigManagers → window + menu → UI → canvas → load settings
- `loop()` → main loop (empty — event-driven via callbacks)
- UI components created via `new` and added to `SimpleWindow` via `window->add()`
- **Do not change** the `init()`, `setup()`, and `loop()` signatures — framework entry points

### Dark Theme

Window background: RGB(45, 45, 54). Canvas background: RGB(22, 22, 28). Button styles defined in `AppUI.cpp`: green for actions (`CLR_ACTION_BG`), orange for export (`CLR_EXPORT_BG`), blue for tool dropdown (`CLR_TOOL_BG`).

### Background Thread Pattern

`doGenerate()` spawns a `CreateThread` worker that runs `runPipeline()`. The `g_isRunning` flag (volatile bool) prevents concurrent runs and controls the progress bar (marquee). UI updates from the thread use `logMsg()` which is safe (TextArea::append).

### Auto-Refresh Preview

The GUI automatically updates the canvas preview when parameters change, using a debounced `WM_TIMER` mechanism (400ms delay, timer ID 9601):

- **Isolation params** (Tip width, Overlap, Offset) — `onTextChange` callback triggers `scheduleAutoRefresh(false)` → re-runs `generateToolpath()` from cached clearance data (no file re-parse)
- **Position params** (X/Y offset) and **checkboxes** (Flip, No Vias) — trigger `scheduleAutoRefresh(true)` → full re-parse via `doLoadKicadDir()`
- **Browse KiCad button** — immediately calls `doLoadKicadDir()` after path selection
- **Tool preset selection** — `applyActiveToolPreset()` sets fields → field callbacks trigger debounced isolation refresh
- **Startup** — if a saved KiCad directory exists in settings, `doLoadKicadDir()` is called automatically

`doLoadKicadDir()` calls `parsePipelineData()` (lightweight parse-only pipeline) then `generateToolpath()` for isolation preview. `doRefreshIsolation()` only regenerates isolation contours from cached `g_pipelineData.clearance`.

---

## Gerber Parser

Supports **RS-274X** format (Extended Gerber):

- **Format spec**: `%FSLAX46Y46*%` — 4 integer + 6 decimal digits
- **Aperture definitions**: Circle, Rectangle, Obround, Polygon, Macro
- **AM macro evaluation**: Full expression evaluator (`$N` variables, `+`, `-`, `*`, `/`, parentheses, unary `-`) with primitive types: 1 (Circle), 4 (Outline), 5 (Regular polygon), 7 (Thermal), 20 (Vector line), 21 (Center line/rectangle)
- **Drawing commands**: D01 (draw), D02 (move), D03 (flash)
- **Regions**: G36/G37 (filled polygons)
- **Arc modes**: G74 (single quadrant), G75 (multi quadrant)
- **Polarity**: `LPD`/`LPC` (dark/clear) — propagated to geometry
- **Layer detection**: Filename pattern matching (F_Cu, B_Cu, Edge_Cuts, etc.)
- **Units**: mm (all coordinates stored in mm)

### Coordinate Normalization

After parsing, the pipeline normalizes coordinates so the board origin is at (0, 0) bottom-left, using the Edge_Cuts outline bounding box. Y axis preserved (Cartesian). Drill points are normalized with the same offsets.

---

## Excellon Parser

- Tool definitions: `T01C0.800` (tool number + diameter)
- Coordinate commands: `X...Y...`
- Units: `METRIC`/`INCH` (auto-conversion to mm)
- Rout mode: `M15`/`M16` (skip routing moves)
- Slotted holes: `G85` (skipped — can't drill slots with plunge bit)
- Via filtering: `TF.FileFunction,ViaDrill` attribute
- `DrillHole` struct: `{ x, y, diameter, fileFunction }`

---

## Isolation Toolpath Engine

### Contour-Parallel Algorithm

1. Compute `clearance = boardOutline - copperUnion` (via Clipper2 `difference`)
2. Initial inward offset: `cam.offset + toolRadius`
3. Simplify geometry: tolerance = `step / 2`
4. Extract contours from offset result
5. Loop: offset inward by `step = toolDiameter * (1 - overlap)`, collect contours
6. Stop when offset result is empty
7. Apply `orderContours()` for path optimization

### Path Optimization (2-opt TSP)

1. **Phase 1**: Nearest-neighbor greedy ordering (pick closest unvisited contour start/end point)
2. **Phase 2**: 2-opt local improvement (up to 50 iterations) — reverse sub-sequences to reduce total rapid travel distance

---

## G-Code Engine

### Output Format (GRBL-compatible)

```gcode
; gerber2gcode — CNC PCB isolation engraving
G21 ; mm
G90 ; absolute

; === Engraver: isolation milling ===
G0 Z5.000
G0 X10.000 Y20.000       ; rapid to contour start
G1 Z-0.050 F150          ; plunge at half feed
G1 X15.000 Y20.000 F300  ; cut
G0 Z5.000                ; retract

; === Drilling ===
G0 X5.000 Y10.000
G1 Z1.000 F2400          ; approach (pre-drill Z)
G1 Z-2.000 F50           ; drill plunge
G1 Z1.000 F50            ; retract
G0 Z5.000                ; safe height

G0 Z5.000
G0 X0 Y0
M84 ; motors off
```

### Drill Optimization

Nearest-neighbor + 2-opt TSP (up to 100 iterations) — minimizes total rapid travel between drill holes.

### Time Estimation

`estimateJobTime()` sums: rapid travel / rapid feedrate + plunge/retract times + cut distances / cut feedrate + drill cycles. Returns seconds.

### Bounds Checking

If `machine.x_size > 0 && machine.y_size > 0`, validates all XY coordinates are within machine bounds. Throws `runtime_error` on violation.

---

## Pipeline

### KiCad File Detection

`detectKicadFiles(dir)` scans directory for files matching KiCad suffixes:

| Suffix | Layer |
|--------|-------|
| `-Edge_Cuts.gbr` | Board outline (required) |
| `-F_Cu.gbr` | Copper top |
| `-B_Cu.gbr` | Copper bottom |
| `-F_Mask.gbr` / `-B_Mask.gbr` | Solder mask |
| `-F_Silkscreen.gbr` / `-B_Silkscreen.gbr` | Silkscreen |
| `-F_Paste.gbr` / `-B_Paste.gbr` | Paste |
| `-PTH.drl` | Plated through-holes |
| `-NPTH.drl` | Non-plated holes |

At least one copper layer + Edge_Cuts must be found.

### Full Workflow (`runPipeline`)

1. Detect KiCad files
2. Parse Edge_Cuts → outline → compute board bounds → normalize to (0,0)
3. Parse all Gerber layers (copper, mask, silk, paste) → apply normalization
4. Parse drills (PTH/NPTH) → apply normalization + optional via filtering
5. Flip board if B_Cu only (mirror X)
6. Clip copper to board outline
7. Compute clearance = outline - copper
8. Generate isolation toolpaths (contour-parallel + ordering)
9. Order drill holes (2-opt TSP)
10. Generate G-Code (isolation + drilling sections)
11. Write output file
12. Generate debug BMP (if enabled)
13. Estimate and log job time

### Parse-Only Mode (`parsePipelineData`)

Steps 1–7 only — returns `PipelineResult` with parsed geometry for immediate canvas preview. Used by `doLoadKicadDir()` for instant visual feedback.

---

## Configuration

### gerber2gcode.ini (UI & generation state)

| Key | Description | Default |
|-----|-------------|---------|
| `kicad_dir` | Last KiCad project directory | (empty) |
| `output_file` | Last output G-Code path | (empty) |
| `tip_width` | Engraver tip width (mm) | `0.20` |
| `z_cut` | Engraver cut depth (mm, negative) | `-0.05` |
| `z_travel` | Engraver safe travel height (mm) | `5.00` |
| `feed_xy` | Engraver XY feed rate (mm/min) | `300` |
| `feed_z` | Engraver Z plunge feed rate (mm/min) | `100` |
| `overlap` | Pass-to-pass overlap ratio (0–1) | `0.40` |
| `offset` | Safety offset from copper edge (mm) | `0.00` |
| `material` | Material thickness (mm) | `1.60` |
| `x_offset` / `y_offset` | Board placement offset (mm) | `0.00` |
| `z_drill` | Drill plunge depth (mm, negative) | `-2.00` |
| `drill_dia` | Drill tool diameter (mm) | `0.80` |
| `drill_feed` | Drill plunge feed rate (mm/min) | `50` |
| `flip` | Mirror board for bottom layer | `0` |
| `ignore_via` | Skip via drill holes | `0` |
| `debug_image` | Generate debug BMP | `0` |
| `gen_isolation` | Generate isolation G-Code | `1` |
| `gen_drilling` | Generate drilling G-Code | `1` |
| `gen_cutout` | Generate cutout G-Code | `0` |

### tools.ini (Tool Presets)

| Key pattern | Description |
|-------------|-------------|
| `tool_count` | Number of tool presets |
| `tool_N_name` | Preset name |
| `tool_N_toolDiameter` | Tool diameter (mm) |
| `tool_N_cutDepth` | Cut depth (mm, negative) |
| `tool_N_safeHeight` | Safe travel height (mm) |
| `tool_N_feedXY` | XY feed rate (mm/min) |
| `tool_N_feedZ` | Z plunge feed rate (mm/min) |
| `tool_N_zDrill` | Drill depth (mm, negative) |
| `tool_N_drillFeed` | Drill feed rate (mm/min) |
| `active_tool` | Active preset index |

### Default Tool Presets (6)

| Name | Diameter | Cut Depth | Safe H | Feed XY | Feed Z | Z Drill | Drill Feed |
|------|----------|-----------|--------|---------|--------|---------|------------|
| V-bit 20deg 0.1mm | 0.1 | -0.05 | 5.0 | 200 | 50 | -2.0 | 50 |
| V-bit 30deg 0.2mm | 0.2 | -0.05 | 5.0 | 300 | 100 | -2.0 | 50 |
| End mill 0.8mm | 0.8 | -0.15 | 5.0 | 400 | 100 | -2.0 | 80 |
| End mill 1.0mm | 1.0 | -0.20 | 5.0 | 400 | 100 | -2.0 | 80 |
| Drill 0.8mm | 0.8 | -2.00 | 5.0 | 300 | 50 | -2.0 | 50 |
| Drill 1.0mm | 1.0 | -2.00 | 5.0 | 300 | 50 | -2.0 | 50 |

---

## Canvas Rendering

### Layer Rendering Order (back to front)

1. Paste Bottom / Top
2. Mask Bottom / Top
3. Clearance (gray fill)
4. Copper Bottom / Top (filled polygons, WINDING fill mode for holes)
5. Silkscreen Bottom / Top
6. Board outline (yellow polyline, 2px)
7. Isolation contours (light blue polylines)
8. Drill holes (circles with center crosses)
9. Origin marker (crosshair at 0,0)

### Layer Colors

| Layer | Color (RGB) |
|-------|-------------|
| Board outline | (220, 220, 60) bright yellow |
| Copper Top | (200, 70, 60) red |
| Copper Bottom | (50, 80, 200) blue |
| Mask Top | (30, 130, 50) green |
| Mask Bottom | (30, 100, 130) teal |
| Silk Top | (200, 200, 100) yellow |
| Silk Bottom | (160, 130, 200) purple |
| Paste Top | (140, 140, 140) gray |
| Paste Bottom | (110, 110, 130) slate |
| PTH drills | (80, 220, 80) green |
| NPTH drills | (220, 160, 40) orange |
| Clearance | (80, 80, 80) dark gray |
| Isolation | (80, 200, 240) light blue |

---

## Copilot Guidelines

### Adding New UI Components

Create in `AppUI.cpp` → `createUI()`. Use `styleBtn()` helper for button colors. Declare `extern` pointer in `AppState.h` if needed by other modules.

### Adding New Menu Commands

Not currently using menus (toolbar-only UI). If adding menu bar, follow JQB_WindowsLib `SimpleWindow::setMenu()` + `onMenuCommand()` pattern.

### Adding New Settings

1. Add `extern` variable in `AppState.h`, define in `AppState.cpp`
2. Load in `loadSettings()`, save in `saveSettings()`

### Adding New Tool Preset Fields

1. Add field to `ToolPreset` struct in `AppState.h`
2. Add `HWND` + EDIT control in `doShowToolPresets()` dialog (AppState.cpp)
3. Update field populate/save logic
4. Update `loadToolPresets()` / `saveToolPresets()` for INI persistence

### Extending Canvas Rendering

Add drawing in `PCBCanvas::onDraw()`. Base features (zoom/pan/grid/double-buffer) inherited from `CanvasWindow`. World coordinates in mm; use `toScreenX()` / `toScreenY()`. Add data setter method + pointer + presence flag.

### Extending Gerber Parser

Add new command handling in `GerberParser.cpp`. Parser uses internal `ParserState` struct. Outputs `geo::Paths` (Clipper2 polygons).

### Extending Drill Parser

Add new format support in `DrillParser.cpp`. Output: `std::vector<DrillHole>`.

### Extending G-Code Output

Add methods to `GCodeGen`. Keep GRBL-compatible format (G0/G1 only, no arcs). Use `fmtXY()`, `fmtZ()`, `fmtF()` helpers for consistent formatting.

### Adding New Pipeline Stages

Add logic in `Pipeline.cpp` — both `runPipeline()` (full) and `parsePipelineData()` (preview-only). Use `LogCallback` for progress messages.

### Canvas Resize

`installResizeHandler()` uses `SetWindowSubclass()` on the main window to intercept `WM_SIZE`. `doResize()` repositions canvas, layer panel, and log area. Layout constants: `TOOLBAR_HEIGHT = 100`, `LAYER_PANEL_W = 180`, `STATUS_BAR_H = 24`, `LOG_AREA_H = 120`.

### Layer Panel

Right-side LISTBOX (`g_hLayerPanel`, ID 9500) with section headers (prefixed `──`) and toggleable layers (☑/☐ prefix). `g_layerItems` vector maps each listbox index to a `LayerPanelItem` with a `bool*` toggle target. Click handler in `ResizeProc` looks up the clicked index and toggles the pointed-to flag. `rebuildLayerPanel()` called after loading KiCad files.

#### Drill Diameter Sub-Items

Under "PTH Drills" / "NPTH Drills", the layer panel shows per-diameter sub-items (e.g. "Ø0.800mm (4)") with extra indentation. Each sub-item toggles a `DrillFilter::visible` flag in `PCBCanvas`. `DrillFilter` groups drill holes by diameter (integer microns key), preserving user visibility state across rebuilds.

- **Canvas**: `drawDrills()` skips holes whose diameter is hidden via `DrillFilter`
- **G-Code**: `generateThread()` collects disabled drill diameters from canvas filters into `PipelineParams::disabledDrillDiameters`. `runPipeline()` filters holes before G-Code emission
- **Parent toggle**: "PTH Drills" / "NPTH Drills" acts as master on/off for the entire category; sub-items provide fine-grained per-diameter control within that category

#### Copper Sub-Layer Items

Under "Copper Top (F_Cu)" / "Copper Bottom (B_Cu)", the layer panel shows per-component sub-items: "Traces" (D01), "Pads" (D03), "Regions" (G36/G37). Each sub-item toggles a `CopperSubVis` flag in `PCBCanvas::LayerVisibility`.

- **Canvas**: When sub-component pointers are set, copper layers render each category in a distinct color shade (traces=base, pads=lighter, regions=darker). When no sub-components are set, falls back to flat layer rendering
- **Isolation**: Toggling a copper sub-vis flag triggers `doRecomputeClearance()` — recomputes clearance from cached `GerberComponents` and regenerates isolation contours (no file re-parse needed)
- **G-Code**: `generateThread()` collects `CopperVisibility` from active copper layer's sub-vis flags into `PipelineParams::copperVis`. `runPipeline()` uses `filterCopperByVisibility()` to include only enabled categories in isolation computation
- **Parser**: `parseGerberComponents()` returns `GerberComponents` with separate `traces`, `pads`, `regions` vectors plus `padGroups` (per-aperture). `combined()` unions all categories. `visiblePads()` unions only visible pad groups. `parseGerber()` preserved for backward compatibility

#### Per-Aperture Pad Groups

Under the "Pads" sub-item, the layer panel shows per-aperture pad groups as sub-sub-items (e.g. "Circle Ø0.800mm (4)", "Rect 1.27×0.64mm (12)"). Each sub-sub-item toggles `PadGroup::visible`.

- **Data**: `PadGroup` struct: `name` (human-readable aperture description), `apNum` (D-code), `paths`, `count`, `visible` (UI toggle)
- **Parser**: `parseGerberComponents()` groups D03 flashes by aperture number into `darkPadsByAperture` map, then builds named `PadGroup` entries from aperture type/params
- **Canvas**: When pad group pointers are set, `onDraw()` iterates over visible pad groups. `setCopperTopPadGroups()` / `setCopperBottomPadGroups()` store pointers
- **Isolation**: Toggling a pad group triggers `doRecomputeClearance()` — uses `comp.visiblePads()` to include only visible pad groups in clearance computation
- **G-Code**: `generateThread()` collects disabled pad apertures from canvas pad groups into `PipelineParams::disabledPadApertures`. `filterCopperByVisibility()` excludes disabled aperture pad groups from copper union

### SimpleWindow is a Singleton

Do not create a second one. For additional windows use `OverlayWindow` or raw WinAPI with `GWLP_USERDATA`.

### Logging

Use `logMsg(const std::string&)` or `logMsg(const wchar_t*)` from `AppState.h`. Logs displayed in `TextArea` (bottom of window).

### Coordinate System

- Gerber files: large absolute coordinates in mm
- After normalization: origin at (0,0), bottom-left corner, Y increases upward (Cartesian)
- Canvas: `toScreenX()` / `toScreenY()` — world Y-up → screen Y-down handled by CanvasWindow
- G-Code: coordinates match normalized world space + user XY offset

### Minimal `platformio.ini`

```ini
[env:windows_x86]
platform = https://github.com/JAQUBA/JQB_MinGW.git
lib_deps =
    https://github.com/JAQUBA/JQB_WindowsLib.git
lib_extra_dirs =
    lib/Clipper2/CPP
```

> C++17, UNICODE, static linking, and library flags are added automatically by `compile_resources.py`.
> Output binary name is set automatically from `InternalName` in `resources/resources.rc` `VS_VERSION_INFO`.

---

## Keeping Documentation Current

When making changes to this project, **always update these files**:

- **`.github/copilot-instructions.md`** (this file) — when adding/removing modules, changing data structures, adding config keys, modifying G-Code output format, adding UI components, or changing the architecture
- **`README.md`** — when adding user-visible features, changing build instructions, or modifying the application workflow
