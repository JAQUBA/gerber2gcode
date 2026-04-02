<div align="center">

# gerber2gcode

**KiCad Gerber/Drill → G-Code for CNC PCB milling**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010+-0078d4.svg)](https://www.microsoft.com/windows)
[![Build: PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](https://platformio.org/)

Native Windows desktop application for converting KiCad Gerber (RS-274X) and Excellon drill files to Mach3/FluidNC-compatible G-Code for CNC PCB isolation routing and drilling.

</div>

---

## Features

- **Multi-layer Gerber parser** — RS-274X with full aperture macro evaluation (AM primitives 1/4/5/7/20/21), regions (G36/G37), arcs (G02/G03), and dark/clear polarity (LPD/LPC)
- **Excellon drill parser** — tool table, metric/inch auto-conversion, PTH/NPTH separation, via filtering, rout mode support
- **Clipper2-based isolation** — contour-parallel inward offset toolpaths with configurable overlap and safety offset from copper edge
- **Real-time GDI preview** — zoomable/pannable canvas with 13 layer types: board outline, copper (top/bottom), mask, silkscreen, paste, clearance, isolation paths, and drill holes
- **Layer visibility panel** — toggle individual layers on/off for focused inspection
- **Polished dark workstation UI** — high-contrast themed numeric fields, wider layer panel with improved readability, and refined spacing/typography for long CAM sessions
- **Tool presets** — save/load grouped V-bit, combo, drill, and cutout presets with one-click switching, including common PCB sizes such as 30 deg V-bits, 1/64in and 1/32in end mills, 1/16in cutout mills, and micro-drills from 0.30 mm upward
- **Selective generation** — independent checkboxes for Isolation, Drilling, and Cutout
- **2-opt TSP path optimization** — nearest-neighbor + 2-opt local improvement for both isolation contours and drill hole ordering, minimizing rapid travel
- **Mach3/FluidNC-compatible G-Code** — clean G0/G1 output with time estimation and optional machine bounds checking
- **Debug BMP export** — re-parses generated G-Code and renders a bitmap for visual verification
- **KiCad auto-detection** — point at a KiCad fabrication output folder and all layers are detected automatically by filename suffix

## Screenshot

*(coming soon)*

## Quick Start

### Prerequisites

- [PlatformIO CLI](https://platformio.org/install/cli) or [PlatformIO IDE](https://platformio.org/install/ide) (VS Code extension)
- Git (for cloning with submodules)

### Build

```bash
# Clone with Clipper2 submodule
git clone --recurse-submodules https://github.com/JAQUBA/gerber2gcode.git
cd gerber2gcode

# Build
pio run
```

The output binary `gerber2gcode.exe` is placed in `.pio/build/windows_x86/`.

> **Note:** C++17, UNICODE, static linking, and Windows library flags are added automatically by the build system. The resulting `.exe` is fully self-contained — no DLLs needed.

### Dependencies

| Library | Purpose | Integration |
|---------|---------|-------------|
| [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) | Win32 UI framework | Auto-fetched by PlatformIO |
| [Clipper2](https://github.com/AngusJohnson/Clipper2) | Polygon boolean & offset | Git submodule in `lib/Clipper2` |

## Usage

1. **Open KiCad directory** — click "Open KiCad..." and select a folder with Gerber (`.gbr`) and drill (`.drl`) files. Layers are detected automatically by KiCad naming convention.
2. **Select tool preset** — pick a machining preset from the dropdown. The app automatically applies matching generation mode (Isolation / Combo / Drill / Cutout), feed/depth tool values, overlap/offset defaults, and resets position toggles.
3. **Set laminate thickness** — adjust only the `Mat` field for your board. Other machining fields (feeds/depths/offsets/drill/cutout toggles) are auto-managed and locked to prevent accidental mismatch.
4. **Choose post profile** — enable `FluidNC` checkbox for FluidNC-oriented preamble/footer (`M2` end), leave unchecked for Mach3-oriented output (`M30` end).
5. **Generate** — click "Generate" to compute toolpaths (runs in background thread)
6. **Preview** — inspect the result in the canvas (scroll to zoom, drag to pan, double-click to reset)
7. **Export G-Code** — click "Export GCode" to save the `.gcode` file

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
- Default feed values are tuned as conservative starting points for Mach3/FluidNC CNC workflows.
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
│   └── Geometry.h / .cpp       # geo:: namespace — Clipper2 wrappers & shape generators
├── Toolpath/
│   └── Toolpath.h / .cpp       # Contour-parallel isolation + 2-opt TSP ordering
├── GCode/
│   └── GCodeGen.h / .cpp       # G-Code generator + drill ordering + time estimation
├── Pipeline/
│   └── Pipeline.h / .cpp       # Orchestration: detect → parse → isolate → generate
└── Debug/
    └── DebugImage.h / .cpp     # Debug BMP output (re-parses G-Code for validation)
```

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
  ├─ orderContours()            nearest-neighbor + 2-opt TSP
  ├─ orderDrillHoles()          nearest-neighbor + 2-opt TSP
  ├─ generateGCode()            Mach3/FluidNC-compatible G0/G1
  │
  └─ generateDebugBMP()         optional visual verification
```

## G-Code Output

Mach3/FluidNC-compatible format with explicit modal reset preamble and safe spindle sequencing for drilling/cutout sections:

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
; post profile: Mach3
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
M30 ; program end
```

When `FluidNC` profile is selected, generator emits a FluidNC-oriented footer:

```gcode
M5 ; spindle off
M2 ; program end
```

## Configuration Files

The application persists settings across sessions in two INI files (created automatically next to the `.exe`):

- **`gerber2gcode.ini`** — last used paths, engraver/drill parameters, overlap/offset, checkboxes
- **`tools.ini`** — saved tool presets with engraver/spindle values, feeds, overlap/offset, and preset-specific position/filter defaults (X/Y, Flip, No Vias, Debug)

`gerber2gcode.ini` additionally stores:

- `post_profile` — `mach3` or `fluidnc`

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

This project is licensed under the [MIT License](LICENSE).

## Acknowledgments

- [Clipper2](https://github.com/AngusJohnson/Clipper2) by Angus Johnson — polygon boolean operations and offsetting
- [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) — lightweight Win32 UI framework
- Inspired by [FlatCAM](http://flatcam.org/) and the PCB isolation routing community
