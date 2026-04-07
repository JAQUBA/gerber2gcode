<div align="center">

# gerber2gcode

**KiCad fabrication output to CNC-ready G-Code for PCB isolation routing, drilling, and cutout work.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Windows 10+](https://img.shields.io/badge/Platform-Windows%2010+-0078d4.svg)](https://www.microsoft.com/windows)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Build: PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](https://platformio.org/)

Native Windows desktop CAM application that converts KiCad Gerber (RS-274X) and Excellon drill files into FluidNC-compatible G-Code for PCB manufacturing workflows.

</div>

## What It Does

gerber2gcode is built for the common PCB CNC workflow:

- load a KiCad fabrication folder
- inspect copper, mask, drill, and outline layers in a native preview
- generate isolation toolpaths with configurable overlap and offset
- optimize contour and drill ordering
- export machine-ready G-Code for FluidNC-style controllers

The project focuses on being practical for real bench use: conservative arc generation, strong preview tooling, predictable presets, and minimal operator friction.

## Highlights

### Fabrication Input

- RS-274X Gerber parser with aperture macro support
- Excellon drill parser with tool table and unit conversion support
- automatic KiCad layer detection by filename suffix
- support for top/bottom copper, solder mask, silkscreen, paste, PTH, and NPTH

### Toolpath Generation

- Clipper2-based clearance and offset geometry
- contour-parallel isolation generation
- independent isolation, drilling, and cutout generation modes
- exact circular-pad qualification for safer G2/G3 emission
- nearest-neighbor + 2-opt ordering for both contours and drill hits

### Operator Workflow

- real-time GDI preview with zoom/pan/grid
- layer tree with collapsible sections
- copper sub-layer control for traces, pads, and regions
- per-aperture pad-group visibility control
- per-diameter drill visibility control
- quick actions for reload, fit, reset, grid, focus, and full visibility

### Output

- FluidNC-oriented G-Code
- isolation, drilling, and cutout sections in one export
- optional debug BMP re-render from generated G-Code
- rough job-time estimation

## Why It Is Useful

Most PCB milling utilities either stop at simplistic geometry or hide too much of the job state. gerber2gcode is intentionally explicit:

- you can inspect what the parser understood
- you can turn sub-layers on and off before generating
- you can keep presets task-oriented instead of editing raw parameters every run
- you can export conservative G-Code aimed at practical controller compatibility

## Build

### Requirements

- [PlatformIO CLI](https://platformio.org/install/cli) or PlatformIO for VS Code
- Git
- Windows 10+

No manual compiler installation is required when using the intended PlatformIO workflow.

### Build Command

```bash
git clone https://github.com/JAQUBA/gerber2gcode.git
cd gerber2gcode
pio run -e windows_x86
```

Output binary:

```text
.pio/build/windows_x86/gerber2gcode.exe
```

### Dependencies

The checked-in `platformio.ini` uses GitHub dependencies for shared libraries:

```ini
lib_deps =
    https://github.com/JAQUBA/JQB_WindowsLib.git
    https://github.com/JAQUBA/JQB_CAMCommon.git
lib_extra_dirs =
    lib/Clipper2/CPP
```

Dependency roles:

| Dependency | Role |
|------------|------|
| [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib) | native Windows UI, dialogs, canvas, tree panel, widgets |
| [JQB_CAMCommon](https://github.com/JAQUBA/JQB_CAMCommon) | geometry, arc math, G-code formatting, route stats, path optimization |
| [Clipper2](https://github.com/AngusJohnson/Clipper2) | polygon boolean operations and offsets |

Clipper2 is checked automatically during build by JQB_CAMCommon's build hook and downloaded into the consumer workspace if needed.

### Local Multi-Repo Development

For local development against sibling repos:

```ini
lib_deps =
    ../JQB_WindowsLib
    ../JQB_CAMCommon
```

If PlatformIO cache holds stale copies of local dependencies, clean the relevant `.pio/libdeps/...` folders before rebuilding.

## Quick Start

1. Open a KiCad fabrication output directory.
2. Let the application auto-detect Gerber and drill files.
3. Select a preset matching your tool and workflow.
4. Set laminate thickness.
5. Inspect the preview and visible layers.
6. Generate toolpaths.
7. Export G-Code.

The application is designed so most workflows revolve around presets and preview inspection, not repetitive manual CAM re-entry.

## User Workflow

### Typical Session

1. **Open KiCad directory** to load fabrication outputs.
2. **Pick a preset** for isolation, combo, drill-only, or cutout work.
3. **Select copper side / drill mode** using the top controls.
4. **Inspect visible layers** using the right-side panel.
5. **Toggle pad groups or drill diameters** if you want a narrower operation scope.
6. **Generate** the paths.
7. **Export** the `.gcode` result.

### Built-In Shortcuts

- `Ctrl+O` open KiCad folder
- `Ctrl+G` generate G-Code
- `Ctrl+R` reload project
- `Ctrl+L` clear log
- `F5` reload
- `F6` fit to board
- `F7` toggle grid

### In-App About Dialog

`Help → About gerber2gcode...` shows the libraries used by the binary together with their licenses.

## Supported Input Naming

The application auto-detects common KiCad output names:

| Suffix | Meaning |
|--------|---------|
| `-Edge_Cuts.gbr` | board outline |
| `-F_Cu.gbr` | top copper |
| `-B_Cu.gbr` | bottom copper |
| `-F_Mask.gbr`, `-B_Mask.gbr` | solder mask |
| `-F_Silkscreen.gbr`, `-B_Silkscreen.gbr` | silkscreen |
| `-F_Paste.gbr`, `-B_Paste.gbr` | paste |
| `-PTH.drl` | plated drill hits |
| `-NPTH.drl` | non-plated drill hits |

At least one copper layer plus `Edge_Cuts` is required for a full PCB routing workflow.

## Presets and Configuration

### Tool Presets

The application ships with a broad starter set of presets for:

- V-bits for fine PCB isolation
- end mills for contour work
- drill bits for hole operations
- cutout workflows

Presets bundle the values that should move together: generation mode, feeds, depths, overlap, offsets, spindle diameter, and related options.

### Persistent Files

Settings are persisted next to the executable:

- `gerber2gcode.ini` for project and workflow state
- `tools.ini` for tool presets

## Architecture Overview

```text
src/
├── main.cpp
├── AppState.h / AppState.cpp
├── AppUI.h / AppUI.cpp
├── Canvas/PCBCanvas.h / PCBCanvas.cpp
├── Config/Config.h / Config.cpp
├── Gerber/GerberParser.h / GerberParser.cpp
├── Drill/DrillParser.h / DrillParser.cpp
├── Geometry/Geometry.h / Geometry.cpp
├── Toolpath/Toolpath.h / Toolpath.cpp
├── GCode/GCodeGen.h / GCodeGen.cpp
├── Pipeline/Pipeline.h / Pipeline.cpp
└── Debug/DebugImage.h / DebugImage.cpp
```

Pipeline summary:

1. detect input files
2. parse outline, copper, and drill layers
3. normalize board coordinates
4. compute clearance and offsets
5. optimize contour and drill order
6. emit G-Code
7. optionally emit debug BMP

## Output Format

gerber2gcode targets FluidNC-compatible motion output with explicit modal setup and conservative section ordering.

Example:

```gcode
G21
G90
G17
G94
G54
M5
G0 Z6.500

; isolation
G0 X10.000 Y20.000
G1 Z1.450 F100
G1 X15.000 Y20.000 F300

; drilling
M3 S255
G4 P1.0
G0 X5.000 Y10.000
G1 Z0.000 F50
M5
M2
```

## Open Source and Licensing

The source code in this repository is licensed under the [MIT License](LICENSE).

The resulting binaries also depend on third-party components with their own licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Current dependency licensing model:

- JQB_WindowsLib — LGPL-3.0-or-later
- JQB_CAMCommon — LGPL-3.0-or-later
- Clipper2 — Boost Software License 1.0

Because the current build uses static linking, compliant binary distribution should also provide required third-party license texts and a practical relinking path for the LGPL libraries.

## Contributing

Contributions are welcome.

When changing architecture, build flow, UI behavior, tool presets, generation logic, or dependency/licensing behavior, keep the following synchronized:

- [README.md](README.md)
- [.github/copilot-instructions.md](.github/copilot-instructions.md)
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## Acknowledgments

- [JQB_WindowsLib](https://github.com/JAQUBA/JQB_WindowsLib)
- [JQB_CAMCommon](https://github.com/JAQUBA/JQB_CAMCommon)
- [Clipper2](https://github.com/AngusJohnson/Clipper2)
- the PCB CNC / isolation-routing community and KiCad fabrication workflow ecosystem
