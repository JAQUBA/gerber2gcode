<div align="center">

# gerber2gcode

**KiCad Gerber/Drill → G-Code for CNC PCB milling**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010+-0078d4.svg)](https://www.microsoft.com/windows)
[![Build: PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](https://platformio.org/)

Native Windows desktop application for converting KiCad Gerber (RS-274X) and Excellon drill files to FluidNC-compatible G-Code for CNC PCB isolation routing and drilling.

</div>

---

## Features

- **Multi-layer Gerber parser** — RS-274X with full aperture macro evaluation (AM primitives 1/4/5/7/20/21), regions (G36/G37), arcs (G02/G03), and dark/clear polarity (LPD/LPC)
- **Excellon drill parser** — tool table, metric/inch auto-conversion, PTH/NPTH separation, via filtering, rout mode support
- **Clipper2-based isolation** — contour-parallel inward offset toolpaths with configurable overlap and safety offset from copper edge
- **Real-time GDI preview** — zoomable/pannable canvas with 13 layer types: board outline, copper (top/bottom), mask, silkscreen, paste, clearance, isolation paths, and drill holes
- **Layer visibility panel** — toggle individual layers on/off for focused inspection, with collapsible dropdown-style sections and nested dropdowns for lower sub-layers (copper sub-components, pad groups, drill diameters)
- **Drill-only switching** — both the layer panel `Drill Only` item and the top layer-selection dropdown can switch generation to drilling-only G-Code
- **Polished dark workstation UI** — high-contrast themed numeric fields, wider layer panel with improved readability, and refined spacing/typography for long CAM sessions
- **Quick action strip** — one-click `Reload`, `Fit`, `Reset`, `Grid`, `All On`, and `Focus` actions for faster preview iteration
- **Keyboard shortcuts** — `Ctrl+O` open KiCad folder, `Ctrl+G` generate, `Ctrl+R` reload, `Ctrl+L` clear log, `F5` reload, `F6` fit, `F7` grid toggle
- **Tool presets** — save/load grouped V-bit, combo, drill, and cutout presets with one-click switching, including common PCB sizes such as 30 deg V-bits, 1/64in and 1/32in end mills, 1/16in cutout mills, and micro-drills from 0.30 mm upward
- **Selective generation** — independent checkboxes for Isolation, Drilling, and Cutout
- **2-opt TSP path optimization** — nearest-neighbor + 2-opt local improvement for both isolation contours and drill hole ordering, minimizing rapid travel
- **FluidNC-compatible G-Code** — clean G0/G1/G2/G3 output with conservative arc generation for trusted circular pads, linear-only cutout emission, time estimation, and optional machine bounds checking
- **Debug BMP export** — re-parses generated G-Code and renders a bitmap for visual verification
- **KiCad auto-detection** — point at a KiCad fabrication output folder and all layers are detected automatically by filename suffix

## Screenshot

*(coming soon)*

## Quick Start

### Prerequisites

- [PlatformIO CLI](https://platformio.org/install/cli) or [PlatformIO IDE](https://platformio.org/install/ide) (VS Code extension)
- Git (used by pre-build script to auto-download Clipper2 if missing)

### Build

```bash
# Clone project
git clone https://github.com/JAQUBA/gerber2gcode.git
cd gerber2gcode

# Build
pio run
```

Clipper2 is downloaded automatically on build (if missing) by JQB_CAMCommon library manifest (`library.json` → `build.extraScript`).

The checked-in `platformio.ini` uses GitHub dependencies for `JQB_WindowsLib` and `JQB_CAMCommon`:

```ini
lib_deps =
  https://github.com/JAQUBA/JQB_WindowsLib.git
  https://github.com/JAQUBA/JQB_CAMCommon.git
```

For local library development in a multi-repo workspace, `platformio.ini` can use:

```ini
lib_deps =
  ../JQB_WindowsLib
  ../JQB_CAMCommon
```

If PlatformIO cache does not pick up freshly added files in local dependencies,
remove `./.pio/libdeps/windows_x86/JQB_WindowsLib` or `./.pio/libdeps/windows_x86/JQB_CAMCommon` and rebuild.

The output binary `gerber2gcode.exe` is placed in `.pio/build/windows_x86/`.

> **Note:** C++17, UNICODE, static linking, and Windows library flags are added automatically by the build system. The resulting `.exe` is fully self-contained — no DLLs needed.

### Dependencies

| Library | Purpose | Integration |
|---------|---------|-------------|
| [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) | Win32 UI framework (including `Util/FileDialogs`) | Git dependency in `platformio.ini` (local path optional for multi-repo development) |
| [JQB_CAMCommon](https://github.com/JAQUBA/JQB_CAMCommon) | Shared CAM utilities (`PathOptimization`, `GCodeFormat`, `ArcMath`, `Geometry`, `RouteStats`) | Git dependency in `platformio.ini` (local path optional for multi-repo development) |
| [Clipper2](https://github.com/AngusJohnson/Clipper2) | Polygon boolean & offset | Auto-downloaded to `lib/Clipper2` by pre-build script when missing |

## Usage

1. **Open KiCad directory** — click "Open KiCad..." and select a folder with Gerber (`.gbr`) and drill (`.drl`) files. Layers are detected automatically by KiCad naming convention.
2. **Select tool preset** — pick a machining preset from the dropdown. The app automatically applies matching generation mode (Isolation / Combo / Drill / Cutout), feed/depth tool values, overlap/offset defaults, and resets position toggles.
3. **Choose layer/mode** — use the `Layer` dropdown to select `Auto`, `F_Cu — Top`, `B_Cu — Bottom`, or `Drill`. Choosing `Drill` synchronizes the side panel to drilling-only generation.
4. **Set laminate thickness** — adjust only the `Mat` field for your board. Other machining fields (feeds/depths/offsets/drill/cutout toggles) are auto-managed and locked to prevent accidental mismatch.
5. **Use quick actions** — `Reload` to re-parse, `Fit` to frame board, `Reset` to default view, `Grid` to toggle grid, `All On` to reveal all layers, `Focus` for copper-centric inspection
6. **Select drilling-only mode if needed** — either pick `Drill` in the `Layer` dropdown or click `Drill Only` in the side panel to generate G-Code only for PTH/NPTH drilling without isolation or cutout
7. **Use collapsible layer sections** — click section headers in the side panel (`▾` / `▸`) to expand or collapse groups and focus on the subset you are currently editing
8. **Generate** — click "Generate" (or `Ctrl+G`) to compute toolpaths (runs in background thread)
9. **Preview** — inspect the result in the canvas (scroll to zoom, drag to pan, double-click to reset)
10. **Export G-Code** — click "Export GCode" to save the `.gcode` file

The `Help → About gerber2gcode...` dialog inside the binary lists the bundled/open-source libraries together with their licenses.

### KiCad File Naming

The application auto-detects layers by filename suffix:

| Suffix | Layer |
|--------|-------|
| `-Edge_Cuts.gbr` | Board outline *(required)* |
| `-F_Cu.gbr` / `-B_Cu.gbr` | Copper top / bottom *(at least one required)* |
| `-F_Mask.gbr` / `-B_Mask.gbr` | Solder mask |
| `-F_Silkscreen.gbr` / `-B_Silkscreen.gbr` | Silkscreen |
| `-F_Paste.gbr` / `-B_Paste.gbr` | Solder paste |
| `-PTH.drl` | Plated through-holes |
| `-NPTH.drl` | Non-plated through-holes |

### Tool Presets

28 default presets are created on first run.

- Presets are grouped by task type: Isolation, Combo (Isolation + Drill), Drilling, and Cutout.
- Default feed values are tuned as conservative starting points for FluidNC CNC workflows.
- V-bits for fine isolation: 20 deg, 30 deg, 45 deg, and 60 deg, including common 0.003in and 0.005in PCB engraving tips.
- Flat end mills for milling and contour work: 1/64in, 1/32in, 1/16in cutout mill, and 1/8in.
- Micro-drills from 0.30 mm to 3.20 mm.
- Each preset stores both engraver parameters and spindle diameter, so drilling warnings and cutout offsets are computed from the spindle tool rather than from the V-bit tip.

## Architecture

```
src/
├── main.cpp                    # Entry point — init(), setup(), loop()
├── AppState.h / .cpp           # Global state, settings, tool presets, shared actions
├── AppUI.h / .cpp              # Toolbar, canvas, layer panel, resize handler
├── Canvas/
│   └── PCBCanvas.h / .cpp      # CanvasWindow subclass — GDI PCB rendering
├── Config/
│   └── Config.h / .cpp         # MachineConfig, CamConfig, JobConfig structs
├── Gerber/
│   └── GerberParser.h / .cpp   # RS-274X Gerber parser → Clipper2 polygons
├── Drill/
│   └── DrillParser.h / .cpp    # Excellon drill parser
├── Geometry/
│   └── Geometry.h / .cpp       # geo:: namespace — re-export of JQB_CAMCommon Geometry module
├── Toolpath/
│   └── Toolpath.h / .cpp       # Contour-parallel isolation + 2-opt TSP ordering + exact-circle metadata
├── GCode/
│   └── GCodeGen.h / .cpp       # G-Code generator + exact circular-pad arc emission + drill ordering + time estimation
├── Pipeline/
│   └── Pipeline.h / .cpp       # Orchestration: detect → parse → isolate → classify exact circles → generate
└── Debug/
    └── DebugImage.h / .cpp     # Debug BMP output (re-parses G-Code for validation)

lib/
└── Clipper2/                   # Auto-downloaded local copy used by JQB_CAMCommon Geometry
```

`JQB_CAMCommon` is consumed as an external PlatformIO dependency. It provides shared CAM modules such as `Geometry`, `PathOptimization`, `ArcMath`, `GCodeFormat`, and `RouteStats`.

### Processing Pipeline

```
KiCad fabrication folder
  │
  ├─ detectKicadFiles()         auto-detect layers by suffix
  ├─ parseBoardOutline()        Edge_Cuts → board outline
  ├─ parseGerber() × N          copper, mask, silk, paste → Clipper2 polygons
  ├─ parseDrill() × 2           PTH + NPTH → drill holes
  ├─ normalize to (0,0)         shift origin, optional mirror for B_Cu
  ├─ clip copper to outline     intersect with board boundary
  ├─ clearance = outline - copper
  │
  ├─ generateToolpath()         contour-parallel inward offset
  ├─ markArcEligible()          exact circular-pad match (conservative)
  ├─ orderContours()            nearest-neighbor + 2-opt TSP
  ├─ orderDrillHoles()          nearest-neighbor + 2-opt TSP
  ├─ generateGCode()            FluidNC-compatible G0/G1/G2/G3
  │
  └─ generateDebugBMP()         optional visual verification
```

## G-Code Output

FluidNC-compatible format with explicit modal reset preamble, exact two-semicircle output for trusted full circular pad-offset loops, and safe spindle sequencing for drilling/cutout sections:

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
G0 Z6.500 ; initial safe Z

; === Engraver: isolation milling ===
G0 Z6.500
G0 X10.000 Y20.000
G1 Z1.450 F100
G1 X15.000 Y20.000 F300
G0 Z6.500

; === Drilling ===
M3 S255 ; spindle on
G4 P1.0 ; spindle settle
G0 X5.000 Y10.000
G1 Z2.500 F2400
G1 Z0.000 F50
G1 Z2.500 F50
G0 Z6.500

G0 Z6.500
G0 X0 Y0
M5 ; spindle off
M2 ; program end
```

Generator always emits a FluidNC-oriented footer:

```gcode
M5 ; spindle off
M2 ; program end
```

## Configuration Files

The application persists settings across sessions in two INI files (created automatically next to the `.exe`):

- **`gerber2gcode.ini`** — last used paths, engraver/drill parameters, overlap/offset, checkboxes
- **`tools.ini`** — saved tool presets with engraver/spindle values, feeds, overlap/offset, and preset-specific position/filter defaults (X/Y, Flip, No Vias, Debug)

`gerber2gcode.ini` stores machining and workflow state used by the FluidNC output pipeline.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Commit your changes (`git commit -am 'Add my feature'`)
4. Push to the branch (`git push origin feature/my-feature`)
5. Open a Pull Request

### Development Notes

- Built with [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) — an Arduino-style Win32 UI framework
- Geometry operations use [Clipper2](https://github.com/AngusJohnson/Clipper2) (Angus Johnson's polygon library)
- See [`.github/copilot-instructions.md`](.github/copilot-instructions.md) for detailed architecture documentation and coding guidelines

## License

The source code in this repository is licensed under the [MIT License](LICENSE).

This repository also depends on third-party components with separate licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

Important: while this repository's own source files are MIT-licensed, distributed binaries must also comply with the licenses of linked dependencies (currently LGPL for JQB_WindowsLib and JQB_CAMCommon, plus BSL-1.0 for Clipper2).

Because the current build uses static linking, a compliant distribution should also include the required third-party license texts and a practical LGPL relinking path for JQB_WindowsLib and JQB_CAMCommon (for example relinkable object files or an equivalent mechanism).

## Acknowledgments

- [Clipper2](https://github.com/AngusJohnson/Clipper2) by Angus Johnson — polygon boolean operations and offsetting
- [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) — lightweight Win32 UI framework
- Inspired by [FlatCAM](http://flatcam.org/) and the PCB isolation routing community
