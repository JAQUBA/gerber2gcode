# Copilot Instructions — gerber2gcode

> **IMPORTANT:** Keep this file and `README.md` up to date whenever you add, rename, or remove modules, change data structures, add UI components, modify G-Code output, or change configuration keys.

## Project Description

Native Windows desktop application (C++) for converting KiCad Gerber (RS-274X) and Excellon drill files to G-Code for CNC PCB isolation routing and drilling. Parses multi-layer Gerber files, renders a real-time GDI preview with pan/zoom, generates Clipper2-based contour-parallel isolation toolpaths, optimizes path ordering with nearest-neighbor + 2-opt TSP, and exports FluidNC-compatible G-Code. Built with JQB_WindowsLib.

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
│   │   └── Geometry.h / .cpp       # geo:: namespace — Re-exports from JQB_CAMCommon Geometry module
│   ├── Toolpath/
│   │   └── Toolpath.h / .cpp       # Contour-parallel isolation generator + 2-opt contour ordering
│   ├── GCode/
│   │   └── GCodeGen.h / .cpp       # G-Code generator with isolation/drilling/cutout + time estimation
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

#### External Libraries (via lib_deps)

- **JQB_CAMCommon** (`../JQB_CAMCommon`) — Reusable utilities shared across CAM modules:
  - `PathOptimization` (generic nearest-neighbor + 2-opt for points and reversible chains)
  - `GCodeFormat` (consistent XY/Z/F/IJ/S token formatting helpers)
  - `ArcMath` (shared circle fit, point-line distance, and turn-angle primitives for arc fitting)
  - `Geometry` (universal shape/boolean operations via Clipper2)
  - `RouteStats` (rapid/cut distance accumulator + `estimateTimeSec()`)
  - Repository: https://github.com/JAQUBA/JQB_CAMCommon


### Module Responsibilities

| Module | Responsibility |
|--------|---------------|
| **main.cpp** | `init()` (COM init), `setup()` (window + menu + UI + canvas), `loop()` (empty — event-driven). Minimal, delegates everything. |
| **AppState** | Global state (`g_window`, `g_canvas`, `g_logArea`, `g_progressBar`, `g_pipelineData`, all UI field pointers). `ToolPreset` struct. `loadSettings()` / `saveSettings()`. Tool preset management (`loadToolPresets`, `saveToolPresets`, `applyActiveToolPreset`, `doSelectTool`, `showToolPopup`, `doShowToolPresets`). `applyActiveToolPreset()` auto-applies tool kind workflow: generation mode mapping (Isolation / Combo / Drill / Cutout), CAM defaults (overlap/offset), XY/flip/via reset, and redraw/reparse scheduling. Input field → Config conversion (`buildConfigFromGUI`). Shared actions: `doLoadKicadDir()`, `doGenerate()`, `doExportGCode()`. Auto-refresh: `scheduleAutoRefresh(bool)` debounce timer (400ms) + `doRefreshIsolation()` for isolation-only preview updates + `doRecomputeClearance()` for copper sub-layer visibility changes. Layer panel: `rebuildLayerPanel()` — uses `TreePanel` widget (`g_treePanel`, JQB_WindowsLib `UI/TreePanel`) with `onToggle` callbacks; `Drill Only` action item triggers `selectDrillOnlyModeFromLayerPanel()`. Resize: `installResizeHandler()`. Logging: `logMsg()`. |
| **AppUI** | `createUI(SimpleWindow*)` — polished 4-row toolbar with section headers (Project / Machining / Position), styled action buttons, themed numeric fields, canvas, wider layer panel, log area, progress bar, and quick-action strip (`Reload`, `Fit`, `Reset`, `Grid`, `All On`, `Focus`). `doResize(w, h)` — dynamic layout. Button styling helpers. Uses JQB_WindowsLib `Util/FileDialogs` for folder/save dialogs. Auto-managed machining controls (feeds/depths/overlap/offset/drill/XY/Flip/No Vias/Debug/Eng M3/Dwell) are read-only or disabled; workflow expects selecting a tool preset and editing mainly `Mat` (material thickness). Arcs toggle controls G2/G3 arc fitting. Browse KiCad button immediately loads and previews the selected directory. Main window keyboard shortcuts: `Ctrl+O`, `Ctrl+G`, `Ctrl+R`, `Ctrl+L`, `F5`, `F6`, `F7`. The `Layer` dropdown supports `Auto`, `F_Cu — Top`, `B_Cu — Bottom`, and `Drill`; choosing `Drill` synchronizes the side panel to drilling-only generation. Layer panel click handling also supports the `Drill Only` action item. |
| **PCBCanvas** | Subclass of JQB_WindowsLib `CanvasWindow` — renders board outline, copper layers (top/bottom) with per-component sub-layers (traces/pads/regions in distinct color shades), mask, silk, paste, clearance, isolation contours, drill holes with center marks. `LayerVisibility` / `LayerPresence` with `CopperSubVis` / `CopperSubPresence` structs. `DrillFilter` groups holes by diameter for per-diameter visibility. `zoomToFit()`. Back-to-front rendering order. |
| **Config** | `Config` struct with `MachineConfig` (engraver Z, tip width, drill Z, feedrates, offsets), `CamConfig` (overlap, offset), `JobConfig` (engraver/spindle/laser feedrates). `loadConfig()` — minimal JSON parser. |
| **Geometry** | `geo::` namespace — Re-exports universal functions from JQB_CAMCommon `Geometry` module (Clipper2-based). Shape generators: `makeCircle`, `makeRect`, `makeObround`, `makeRegPoly`. Boolean ops: `unionAll`, `difference`, `intersect`, `offset`. Utilities: `bufferLine`, `bufferPath`, `simplifyPaths`, `translate`, `flipX`, `isEmpty`, `totalArea`. |
| **GerberParser** | RS-274X parser: FSLAX format, aperture definitions (Circle/Rect/Obround/Polygon/Macro), AM macro evaluation (full expression evaluator with primitives 1/4/5/7/20/21), D01/D02/D03, G36/G37 regions, G02/G03 arcs, G74/G75 quadrant modes, LPD/LPC polarity. Two output modes: `parseGerber()` → `geo::Paths` (flat union), `parseGerberComponents()` → `GerberComponents` (categorized: traces=D01, pads=D03, regions=G36/G37). `PadGroup` struct groups D03 flashes by aperture D-code with human-readable names (e.g. "Circle Ø0.800mm", "Rect 1.27×0.64mm"), `isCircular` flag (Circle aperture), `apertureRadius`, and `centers` (D03 flash positions). `GerberComponents::combined()` unions all categories. `GerberComponents::visiblePads()` unions only visible pad groups. |
| **DrillParser** | Excellon parser: tool table (`TnnCdia`), coordinates, METRIC/INCH units, rout mode (M15/M16), slotted holes (G85 — skipped), via filtering. Outputs `std::vector<DrillHole>`. |
| **Toolpath** | `generateToolpath(clearance, config)` — contour-parallel inward offset with configurable overlap. `orderContours()` — nearest-neighbor + 2-opt TSP optimization. `ToolpathContour` stores `arcEligible` plus exact-circle metadata (`hasExactCircle`, `arcCenterX/Y`, `arcRadius`) set by `markArcEligible()`. |
| **GCodeGen** | `generateGCode(contours, holes, cutoutPath, config, xOff, yOff)` — FluidNC-compatible G0/G1/G2/G3 output with isolation + drilling + cutout sections. Full circular pad-offset contours emit as two exact semicircle commands using stored center/radius metadata; non-qualified contours remain G1. Configurable via `use_arcs` (isolation only). Cutout is emitted as G1-only for robustness. Multi-pass depth cutting for cutout. Optional engraver spindle (M3) before isolation. Optional drill dwell (G4) at hole bottom. G28 return to home at program end. `orderDrillHoles()` — nearest-neighbor + 2-opt. `estimateJobTime()` — accumulates `routestats::RouteStats` and delegates to `estimateTimeSec()`. |
| **Pipeline** | `detectKicadFiles(dir)` — auto-detect layers by filename suffix. `runPipeline(params, log, result)` — full workflow (parse → normalize → clip → isolate → mark arc eligibility → order → generate → export). `parsePipelineData(params, log)` — parse-only for live preview. `markArcEligible(contours, circPads, tolerance)` — matches contours to known circular pad centers, validates radial spread against that exact center, and stores exact-circle metadata for robust G2/G3 output. `CircPadInfo` — circular pad center + radius from Gerber parsing. |
| **DebugImage** | `generateDebugBMP(gcodePath, outputPath, config, holes)` — re-parses G-Code, renders as 24-bit BMP for visual validation. |

## Tech Stack

- **Language**: C++17
- **Build system**: PlatformIO (`platform = native` via JQB_MinGW)
- **UI framework**: [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) — lightweight Win32 UI library
- **CAM library**: [JQB_CAMCommon](https://github.com/JAQUBA/JQB_CAMCommon) — reusable CAM utilities (path optimization, G-code format, geometry, arc math, route stats)
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

Window background: RGB(34, 37, 46). Canvas background: RGB(18, 22, 31). Button styles defined in `AppUI.cpp`: green for actions (`CLR_ACTION_BG`), amber for primary generate (`CLR_EXPORT_BG`), blue for tool dropdown (`CLR_TOOL_BG`). Numeric `InputField` controls and layer `LISTBOX` are themed through `WM_CTLCOLOREDIT` / `WM_CTLCOLORLISTBOX` for high-contrast dark readability.

### Background Thread Pattern

`doGenerate()` spawns a `CreateThread` worker that runs `runPipeline()`. The `g_isRunning` flag (volatile bool) prevents concurrent runs and controls the progress bar (marquee). UI updates from the thread use `logMsg()` which is safe (TextArea::append).

### Auto-Refresh Preview

The GUI automatically updates the canvas preview when parameters change, using a debounced `WM_TIMER` mechanism (400ms delay, timer ID 9601) built on JQB_WindowsLib `Util/TimerUtils` helpers (`restartDebounceTimer`, `stopTimer`):

- **Isolation params** (Tip width, Overlap, Offset) — `onTextChange` callback triggers `scheduleAutoRefresh(false)` → re-runs `generateToolpath()` from cached clearance data (no file re-parse)
- **Position params** (X/Y offset) and **checkboxes** (Flip, No Vias) — trigger `scheduleAutoRefresh(true)` → full re-parse via `doLoadKicadDir()`
- **Browse KiCad button** — immediately calls `doLoadKicadDir()` after path selection
- **Tool preset selection** — `applyActiveToolPreset()` sets fields → field callbacks trigger debounced isolation refresh
- **Tool preset selection** — `applyActiveToolPreset()` sets all core machining fields and auto-selects generation mode by `ToolPresetKind`: `Isolation` (isolation only), `Combo` (isolation + drills), `Drill` (drills only), `Cutout` (cutout only). It also applies overlap/offset defaults and resets X/Y/Flip/No Vias.
- **Startup** — if a saved KiCad directory exists in settings, `doLoadKicadDir()` is called automatically

`doLoadKicadDir()` calls `parsePipelineData()` (lightweight parse-only pipeline) then `generateToolpath()` for isolation preview. `doRefreshIsolation()` only regenerates isolation contours from cached `g_pipelineData.clearance`, then re-runs exact-circle qualification via `markArcEligible()`.

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

### Z Coordinate Model

Z=0 is the **machine bed** (table surface). The PCB laminate sits on the bed:

| Z Position | Description | Example (mat=1.5, depth=0.05, safe=5.0) |
|-----------|-------------|------------------------------------------|
| `materialThickness + safeHeight` | Program safe Z (start/end only) | Z6.500 |
| `materialThickness + 1.0` | Rapid clearance Z between operations | Z2.500 |
| `materialThickness` | Top surface of PCB copper | Z1.500 |
| `materialThickness - engravingDepth` | Isolation cut depth | Z1.450 |
| `max(0, materialThickness - drillDepth)` | Drill depth (clamped to bed) | Z0.000 |
| 0 | Cutout final depth (bed level) | Z0.000 |

All user-entered depth values are **positive** (e.g., engravingDepth=0.05, drillDepth=2.0). The engine computes actual Z from `materialThickness`.

**Rapid clearance strategy**: Full safe Z (`materialThickness + safeHeight`) is used only at the program start and end. Between individual operations (contour-to-contour, hole-to-hole), the machine retracts only to `materialThickness + 1.0mm` (rapid clearance) to minimize travel time.

### Output Format (FluidNC-compatible)

```gcode
; gerber2gcode — CNC PCB isolation engraving
G21 ; mm
G90 ; absolute
G17 ; XY plane
G94 ; feed per minute
G54 ; work offset
G40 ; cancel cutter compensation
G49 ; cancel tool length offset
G80 ; cancel canned cycles
M5 ; spindle off
; post profile: FluidNC
G0 Z6.5000 ; initial safe Z
G28.1 ; store current position as home

; === Engraver: isolation milling ===
; (optional, if engraver_spindle_on=true:)
; M3 S255 ; spindle on
; G4 P1.0 ; spindle settle
G0 Z2.5000
G0 X10.000 Y20.000       ; rapid to contour start
G1 Z1.4500 F150          ; plunge at half feed
G1 X15.000 Y20.000 F300  ; cut (linear)
; (when use_arcs=true, exact circular pad loops emit as two semicircles:)
G2 X20.000 Y25.000 I5.000 J0.000 F300  ; semicircle 1
G2 X10.000 Y20.000 I-5.000 J0.000 F300 ; semicircle 2
G0 Z2.5000               ; retract

; === Drilling ===
M3 S255 ; spindle on
G4 P1.0 ; spindle settle
G0 X5.000 Y10.000
G1 Z2.5000 F2400         ; approach (pre-drill Z)
G1 Z0.0000 F50           ; drill plunge (through material to bed)
; (optional, if drill_dwell > 0:)
; G4 P0.500 ; dwell
G1 Z2.5000 F50           ; retract
G0 Z2.5000               ; rapid clearance

G0 Z6.5000
G28 ; return to home position
M5 ; spindle off
M2 ; program end
```

Output footer uses `M2`:

```gcode
G28 ; return to home position
M5 ; spindle off
M2 ; program end
```

When cutout is enabled, an additional section is appended after drilling:

```gcode
; === Cutout ===
G0 X10.000 Y0.000         ; rapid to cutout start
G1 Z1.0000 F150           ; plunge to first pass depth (from material top)
G1 X50.000 Y0.000 F300    ; cut along outline
G1 X50.000 Y40.000 F300
...
G0 Z6.5000                ; retract after final pass
```

Cutout uses multi-pass depth: starts at Z=materialThickness, descends by `cutout_z_step` each pass until reaching Z=0 (bed level). The cutout path is the board outline offset outward by `spindle_tool_radius + cutout_offset`.

### Arc Fitting (G2/G3 Post-Processor)

Arc fitting is **Gerber-aware**: the decision whether a contour is eligible for G2/G3 conversion is determined from the original Gerber aperture types, not from polyline geometry alone.

For full circular contours around round pads, the generator does **not** rely on heuristic polyline fitting anymore. Instead it emits two exact semicircles using the known pad center and the measured offset radius, which is more robust for Grbl/FluidNC-style controllers than attempting a single inferred full-circle command.

**Arc eligibility pipeline:**

1. **GerberParser** marks `PadGroup.isCircular = true` for Circle apertures and stores flash center positions (`PadGroup.centers`) and radius (`PadGroup.apertureRadius`); additionally, it infers circular macro pad groups from geometry (`inferCircularPadRadius`) when aperture type alone is not enough
2. **Pipeline** collects `CircPadInfo` (center + radius) from circular PadGroups into `PipelineResult.circularPads`
3. **`markArcEligible()`** checks each toolpath contour against known circular pad centers with radial consistency checks and stores exact-circle metadata (`hasExactCircle`, center, radius)
4. **GCodeGen** emits two exact semicircles for contours with `hasExactCircle == true`; remaining contours fall back to pure G1

**Arc fitting algorithm** (conservative fallback for arc-qualified isolation contours):

1. For each contour/cutout polyline, build closed loop (append start point)
2. Starting from each point, seed a circumscribed circle through 3 consecutive points
3. Radius must be in bounds (0.05–100mm) — larger radii are essentially straight lines
4. **Validate 4th point** against the seeded circle (first real test — 3 seed points always lie exactly on their circumscribed circle)
5. **Collinearity gate**: cross product of first two edge vectors must indicate curvature > ~2° (`sinAngle > 0.035`), rejecting near-straight segments
6. Determine CW/CCW from cross product sign: negative = CW (G2), positive = CCW (G3)
7. Extend arc window greedily while points stay within tolerance (0.005mm) AND maintain the same curvature direction (direction consistency check)
8. **Sweep angle validation**: arc must subtend 10°–355° (reject near-straight and near-full-circle arcs)
9. **Endpoint refinement**: recompute center using start/mid/end points via `circumCircle` for maximum IJ precision (eliminates endpoint radius mismatch that CNC controllers would reject)
10. Re-verify all intermediate points against the refined circle (tolerance × 2)
11. Emit G2 (CW) or G3 (CCW) with I/J offsets (incremental from arc start to center)
12. Fall back to G1 for segments that don't pass all checks

Constants: `ARC_TOLERANCE=0.005mm`, `MIN_ARC_RADIUS=0.05mm`, `MAX_ARC_RADIUS=100mm`, `MIN_ARC_POINTS=4`, `MIN_ARC_SWEEP=10°`, `MAX_ARC_SWEEP=355°`.

Benefits: smaller G-Code files, smoother motion (controller plans arc natively), and better controller stability because only trusted circular pad loops are emitted as arcs while cutout and non-qualified contours stay as G1.

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
| `z_cut` | Engraving depth into material (mm, positive) | `0.05` |
| `z_travel` | Safe clearance height above material (mm) | `5.00` |
| `feed_xy` | Engraver XY feed rate (mm/min) | `300` |
| `feed_z` | Engraver Z plunge feed rate (mm/min) | `100` |
| `overlap` | Pass-to-pass overlap ratio (0–1) | `0.40` |
| `offset` | Safety offset from copper edge (mm) | `0.00` |
| `material` | Material thickness (mm); Z=0 at bed, top at Z=material | `1.50` |
| `x_offset` / `y_offset` | Board placement offset (mm) | `0.00` |
| `z_drill` | Drill depth from top (mm, positive; clamped to Z>=0) | `2.00` |
| `drill_dia` | Spindle tool diameter for drilling and cutout (mm) | `0.80` |
| `drill_feed` | Spindle feed rate for drilling and cutout (mm/min) | `50` |
| `flip` | Mirror board for bottom layer | `0` |
| `ignore_via` | Skip via drill holes | `0` |
| `debug_image` | Generate debug BMP | `0` |
| `gen_isolation` | Generate isolation G-Code | `1` |
| `gen_drilling` | Generate drilling G-Code | `1` |
| `gen_cutout` | Generate cutout G-Code | `0` |
| `engraver_spindle` | M3 spindle on before isolation milling | `0` |
| `drill_dwell` | Dwell at drill bottom (seconds, 0=disabled) | `0` |
| `use_arcs` | G2/G3 arc fitting for isolation/cutout paths | `1` |

### tools.ini (Tool Presets)

| Key pattern | Description |
|-------------|-------------|
| `tool_count` | Number of tool presets |
| `tool_N_name` | Preset name |
| `tool_N_toolDiameter` | Tool diameter (mm) |
| `tool_N_cutDepth` | Engraving depth (mm, positive) |
| `tool_N_safeHeight` | Safe travel height (mm) |
| `tool_N_feedXY` | XY feed rate (mm/min) |
| `tool_N_feedZ` | Z plunge feed rate (mm/min) |
| `tool_N_zDrill` | Drill depth from top (mm, positive) |
| `tool_N_kind` | Preset group (`isolation`, `combo`, `drill`, `cutout`) |
| `tool_N_drillDiameter` | Spindle diameter for drilling/cutout (mm) |
| `tool_N_drillFeed` | Drill feed rate (mm/min) |
| `tool_N_overlap` | Isolation overlap ratio |
| `tool_N_offset` | Isolation safety offset (mm) |
| `tool_N_xOffset` / `tool_N_yOffset` | Placement offsets (mm) |
| `tool_N_flip` | Mirror board for bottom workflow (0/1) |
| `tool_N_ignoreVia` | Skip via holes (0/1) |
| `tool_N_debugImage` | Generate debug BMP (0/1) |
| `tool_N_engraverSpindle` | M3 spindle on before isolation (0/1) |
| `tool_N_drillDwell` | Dwell at drill bottom (seconds, 0=disabled) |
| `active_tool` | Active preset index |

### Default Tool Presets (28)

Defaults are tuned as conservative starting points for FluidNC.

| Name | Diameter | Cut Depth | Safe H | Feed XY | Feed Z | Z Drill | Spindle Dia | Drill Feed |
|------|----------|-----------|--------|---------|--------|---------|-------------|------------|
| V-bit 10deg 0.05mm | 0.05 | 0.03 | 5.0 | 120 | 20 | 2.0 | 0.80 | 50 |
| V-bit 20deg 0.10mm | 0.10 | 0.05 | 5.0 | 160 | 25 | 2.0 | 0.80 | 60 |
| V-bit 30deg 0.08mm (0.003in) | 0.08 | 0.05 | 5.0 | 140 | 22 | 2.0 | 0.80 | 60 |
| V-bit 30deg 0.10mm | 0.10 | 0.06 | 5.0 | 180 | 28 | 2.0 | 0.80 | 60 |
| V-bit 30deg 0.13mm (0.005in) | 0.13 | 0.08 | 5.0 | 220 | 35 | 2.0 | 0.80 | 70 |
| V-bit 30deg 0.20mm | 0.20 | 0.10 | 5.0 | 260 | 45 | 2.0 | 0.80 | 80 |
| V-bit 45deg 0.20mm | 0.20 | 0.10 | 5.0 | 300 | 55 | 2.0 | 0.80 | 90 |
| V-bit 60deg 0.30mm | 0.30 | 0.15 | 5.0 | 340 | 65 | 2.0 | 0.80 | 100 |
| End mill 0.40mm (1/64in) | 0.40 | 0.10 | 5.0 | 180 | 35 | 2.0 | 0.40 | 90 |
| End mill 0.60mm | 0.60 | 0.12 | 5.0 | 220 | 45 | 2.0 | 0.60 | 110 |
| End mill 0.80mm (1/32in) | 0.80 | 0.15 | 5.0 | 260 | 55 | 2.0 | 0.80 | 130 |
| End mill 1.00mm | 1.00 | 0.20 | 5.0 | 300 | 65 | 2.0 | 1.00 | 150 |
| End mill 1.20mm | 1.20 | 0.25 | 5.0 | 340 | 75 | 2.0 | 1.20 | 170 |
| Cutout mill 1.60mm (1/16in) | 1.60 | 0.35 | 5.0 | 420 | 90 | 2.0 | 1.60 | 220 |
| End mill 2.00mm | 2.00 | 0.40 | 5.0 | 480 | 100 | 2.0 | 2.00 | 260 |
| End mill 3.20mm (1/8in) | 3.20 | 0.60 | 5.0 | 650 | 140 | 2.0 | 3.20 | 320 |
| Drill 0.30mm | 0.30 | 2.00 | 5.0 | 160 | 20 | 2.0 | 0.30 | 25 |
| Drill 0.40mm | 0.40 | 2.00 | 5.0 | 180 | 25 | 2.0 | 0.40 | 30 |
| Drill 0.50mm | 0.50 | 2.00 | 5.0 | 200 | 30 | 2.0 | 0.50 | 35 |
| Drill 0.60mm | 0.60 | 2.00 | 5.0 | 220 | 35 | 2.0 | 0.60 | 40 |
| Drill 0.80mm | 0.80 | 2.00 | 5.0 | 240 | 40 | 2.0 | 0.80 | 50 |
| Drill 0.90mm | 0.90 | 2.00 | 5.0 | 260 | 45 | 2.0 | 0.90 | 55 |
| Drill 1.00mm | 1.00 | 2.00 | 5.0 | 280 | 50 | 2.0 | 1.00 | 60 |
| Drill 1.20mm | 1.20 | 2.00 | 5.0 | 300 | 55 | 2.0 | 1.20 | 70 |
| Drill 1.50mm | 1.50 | 2.00 | 5.0 | 320 | 60 | 2.0 | 1.50 | 85 |
| Drill 2.00mm | 2.00 | 2.00 | 5.0 | 360 | 70 | 2.0 | 2.00 | 100 |
| Drill 3.00mm | 3.00 | 2.00 | 5.0 | 420 | 90 | 2.0 | 3.00 | 120 |
| Drill 3.20mm | 3.20 | 2.00 | 5.0 | 450 | 95 | 2.0 | 3.20 | 130 |

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
8. Cutout path (orange polyline, 2px)
9. Drill holes (circles with center crosses)
10. Origin marker (crosshair at 0,0)

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
| Cutout | (240, 140, 40) orange |

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

Add methods to `GCodeGen`. Keep FluidNC-compatible format (G0/G1/G2/G3). Use `fmtXY()`, `fmtZ()`, `fmtF()`, `fmtIJ()` helpers for consistent formatting. Arc fitting via `fitArcs()` converts polylines to G2/G3 when `use_arcs` is enabled.

### Adding New Pipeline Stages

Add logic in `Pipeline.cpp` — both `runPipeline()` (full) and `parsePipelineData()` (preview-only). Use `LogCallback` for progress messages.

### Canvas Resize

`installResizeHandler()` uses `SetWindowSubclass()` on the main window to intercept `WM_SIZE` plus color-theme messages (`WM_CTLCOLOREDIT`, `WM_CTLCOLORLISTBOX`). The same subclass handles `WM_GETMINMAXINFO` (minimum window size for full toolbar usability) and keyboard shortcuts (`WM_KEYDOWN`). `doResize()` repositions canvas, layer panel, and log area. Layout constants: `TOOLBAR_HEIGHT = 166`, `LAYER_PANEL_W = 290`, `STATUS_BAR_H = 20`, `LOG_AREA_H = 112`.

### Layer Panel

Right-side LISTBOX (`g_hLayerPanel`, ID 9500) with collapsible section headers (prefix `▾` expanded / `▸` collapsed) and toggleable layers (☑/☐ prefix). Sections: `Board`, `Copper`, `Layers`, `Drills`, `Generated`. Lower levels also support dropdown behavior: `Top Sub-layers` / `Bottom Sub-layers`, `Pad Groups`, and per-category drill `Diameters` for PTH/NPTH. `g_treePanel` (`TreePanel*`, JQB_WindowsLib `UI/TreePanel`) wraps the LISTBOX and manages node state. Click handler in `ResizeProc` calls `g_treePanel->handleClick(idx)`; `addItem()` callbacks fire side-effects (e.g. `doRecomputeClearance`). `rebuildLayerPanel()` rebuilds the tree after loading KiCad files and after section state changes.

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
- G-Code XY: coordinates match normalized world space + user XY offset
- G-Code Z: Z=0 is machine bed (table surface), Z=materialThickness is copper surface. All depth values entered as positive numbers

### Minimal `platformio.ini`

```ini
[env:windows_x86]
platform = https://github.com/JAQUBA/JQB_MinGW.git
lib_deps =
    ../JQB_WindowsLib
    ../JQB_CAMCommon
lib_extra_dirs =
    lib/Clipper2/CPP
```

> C++17, UNICODE, static linking, and library flags are added automatically by `compile_resources.py`.
> Output binary name is set automatically from `InternalName` in `resources/resources.rc` `VS_VERSION_INFO`.
> **Note**: `JQB_CAMCommon` is a standalone library in `d:\Programowanie\JQB_CAMCommon` (canonical location). Both gerber2gcode and WektoroweLitery2 reference it via `../JQB_CAMCommon`.
> **Note**: Clipper2 is auto-downloaded by `../JQB_CAMCommon/library.json` (`build.extraScript`) when `lib/Clipper2` is missing.

---

## Keeping Documentation Current

When making changes to this project, **always update these files**:

- **`.github/copilot-instructions.md`** (this file) — when adding/removing modules, changing data structures, adding config keys, modifying G-Code output format, adding UI components, or changing the architecture
- **`README.md`** — when adding user-visible features, changing build instructions, or modifying the application workflow
