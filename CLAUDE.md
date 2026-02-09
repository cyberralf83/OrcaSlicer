# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

OrcaSlicer is an open-source 3D slicer application forked from Bambu Studio. Built with C++17, wxWidgets GUI, and CMake. The codebase is 500k+ lines across core slicing engine (`src/libslic3r/`), GUI (`src/slic3r/GUI/`), and bundled dependencies (`deps_src/`).

## This Fork

- Tracks upstream OrcaSlicer nightly builds, rebased every few days
- The **only** custom code is a Bambu Connect export plugin
- A custom GitHub Actions workflow rebuilds and publishes a new GitHub release on each upstream sync
- **Do NOT make changes unrelated to the Bambu Connect plugin**
- Keep the diff from upstream as minimal as possible

## Build Commands

### Build System

Two-phase build: dependencies first (`deps/`), then main application. Dependencies are built once and cached.

- CMake minimum 3.13
- Windows: Visual Studio generator
- macOS: Xcode by default, Ninja with `-x` flag
- Linux: Ninja generator
- Tests require `BUILD_TESTS=ON` CMake option

### Building

**Windows:**
```bash
cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

**macOS:**
```bash
cmake --build build/arm64 --config RelWithDebInfo --target all --
```

**Linux:**
```bash
cmake --build build/arm64 --config RelWithDebInfo --target all --
```

Build scripts for CI/release are at the repo root: `build_linux.sh`, `build_release_macos.sh`, `build_release_vs2022.bat`. Use `build_release_macos.sh -sx` for macOS debugging. `scripts/DockerBuild.sh` for reproducible container builds.

### Testing

Tests use Catch2 v2 (`#include <catch2/catch.hpp>`). Must be enabled with `BUILD_TESTS=ON`.

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run individual test suites directly
./build/tests/libslic3r/libslic3r_tests --order rand --warn NoAssertions
./build/tests/fff_print/fff_print_tests
./build/tests/sla_print/sla_print_tests

# Run via ctest with labels
cd build && ctest --test-dir tests -L "Http|PlaceholderParser" --output-on-failure -j
```

Test structure:
- `tests/libslic3r/` - Core library tests (21 files): geometry, algorithms, file formats
- `tests/fff_print/` - FFF print tests (16 files): slicing, G-code, fill patterns, supports
- `tests/sla_print/` - SLA tests: support generation, slicing
- `tests/libnest2d/` - 2D nesting algorithm tests
- `tests/data/` - Test models and configs used by tests

## Architecture

### Source Layout

```
src/
├── OrcaSlicer.cpp              # Application entry point
├── libslic3r/                  # Core slicing engine (platform-independent)
│   ├── Print.cpp/hpp           # Slicing pipeline orchestrator
│   ├── PrintConfig.cpp/hpp     # All print/printer/material settings (10k+ lines)
│   ├── GCode/                  # G-code generation pipeline (45+ files)
│   ├── Fill/                   # Infill patterns (30+ algorithms)
│   ├── Support/                # Tree and traditional support generation
│   ├── Arachne/                # Variable-width perimeter generation
│   ├── Format/                 # File I/O: 3MF, STL, AMF, OBJ, STEP
│   ├── Geometry/               # Geometric operations, Voronoi, medial axis
│   ├── SLA/                    # Stereolithography processing
│   └── Algorithm/              # Generic algorithm implementations
├── slic3r/                     # Application framework
│   ├── GUI/                    # wxWidgets GUI (~580 files)
│   │   ├── GUI_App.cpp/hpp     # Application main class
│   │   ├── MainFrame.cpp/hpp   # Primary window
│   │   ├── Plater.cpp/hpp      # 3D model placement interface
│   │   ├── Tab.cpp/hpp         # Settings panels (Print/Filament/Printer)
│   │   ├── GLCanvas3D.cpp/hpp  # OpenGL 3D viewport
│   │   ├── Gizmos/             # 3D manipulation tools
│   │   ├── Jobs/               # Async job processing
│   │   └── Widgets/            # Custom wxWidgets controls
│   ├── Config/                 # Configuration management
│   └── Utils/                  # Utility functions
├── libvgcode/                  # G-code visualization engine
└── deps_src/                   # Bundled dependency sources (clipper2, eigen, imgui, etc.)
```

### Slicing Pipeline

The `Print` class orchestrates slicing via a state machine. Each step is tracked for caching/invalidation.

**Per-object steps** (run in parallel via TBB):
1. `posSlice` - Mesh slicing into layers
2. `posPerimeters` - Wall generation (Arachne variable-width)
3. `posPrepareInfill` - Infill region preparation
4. `posInfill` - Fill pattern generation
5. `posIroning` - Surface ironing
6. `posSupportMaterial` - Support generation
7. `posSimplifyPath/Wall/Infill` - Path optimization

**Print-level steps** (sequential):
1. `psWipeTower` - Tool ordering and wipe tower
2. `psSkirtBrim` - Skirt/brim generation
3. `psGCodeExport` - G-code output
4. `psConflictCheck` - Collision detection

### Configuration System

`PrintConfig.cpp/hpp` defines all settings using a hierarchical model:
- `PrintObjectConfig` - Per-object settings
- `PrintRegionConfig` - Per-region (volumes sharing same extruder) settings
- `PrintConfig` - Overall print settings, inherits from above
- `GCodeConfig` - G-code generation settings
- `DynamicPrintConfig` - Runtime-flexible config used in GUI
- `StaticPrintConfig` - Compile-time typed config used in slicing

Settings are registered with name, type, default value, bounds, label, and tooltip. Filament profiles can override per-extruder settings via `filament_extruder_override_keys`.

### G-Code Generation Pipeline

Key components in `src/libslic3r/GCode/`:
- `GCodeProcessor` - Main engine (~258KB), parses and processes G-code
- `CoolingBuffer` - Fan speed and travel time optimization
- `AvoidCrossingPerimeters` - Safe travel path planning
- `ToolOrdering` / `WipeTower` / `WipeTower2` - Multi-tool sequencing
- `SeamPlacer` - Layer seam positioning
- `PressureEqualizer` / `AdaptivePAProcessor` - Pressure advance
- `FanMover` - Dynamic fan speed ramping
- `ConflictChecker` - Nozzle/build volume collision detection

### Profile System

Profiles in `resources/profiles/` organized by manufacturer (64+ vendors):
```
resources/profiles/
├── BBL.json                    # Vendor manifest with machine/process/filament lists
├── [Manufacturer]/
│   ├── machine/                # Printer definitions
│   ├── process/                # Print profiles (speed, quality settings)
│   └── filament/               # Material definitions
```

Profiles use JSON with an inheritance system (`"inherits": "base_profile_name"`). The top-level vendor JSON (e.g., `BBL.json`) is a manifest referencing individual profile files via `sub_path`.

### GUI Structure

The GUI uses wxWidgets with OpenGL for 3D rendering:
- `GUI_App` - Application lifecycle and initialization
- `MainFrame` - Window management, menu bar, status bar
- `Plater` - Central workspace: model loading, arrangement, plate management
- `Tab` / `Page` / `ConfigOptionsGroup` - Dynamic settings UI generated from config definitions
- `GLCanvas3D` - 3D viewport with camera, selection, gizmos
- `GCodeViewer` - G-code visualization and analysis (uses `libvgcode/`)

### Key Dependencies

Built externally (`deps/`): Boost 1.83+, TBB, wxWidgets, OpenSSL, CURL, CGAL, OpenVDB 5.0+, NLopt 1.4+, Eigen 3.3+

Bundled (`deps_src/`): clipper2, libigl, imgui, libnest2d, admesh, cereal, eigen, expat

## Code Conventions

- C++17, PascalCase for classes, snake_case for functions/variables, SCREAMING_CASE for constants
- `#pragma once` for header guards
- `.clang-format` enforces 4-space indents, 140-column limit, brace wrapping for classes/functions. Run `clang-format -i <file>` before committing.
- Parallelization via TBB (not raw threads)
- Translation macros: `L()`, `_L()`, `_u8L()` for translatable strings
- Translations in `localization/i18n/` (.po files), compiled to `resources/i18n/` (.mo files)
- Generate translations: `scripts/run_gettext.sh`
- Commit style: concise sentence-style subjects with optional issue refs, e.g. `Fix grid lines origin for multiple plates (#10724)`
- Do not modify vendored code in `deps/` or `deps_src/` without mirroring upstream tags

## Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | OFF | Build unit tests |
| `SLIC3R_GUI` | ON | Build GUI components |
| `SLIC3R_STATIC` | ON | Static linking |
| `SLIC3R_PCH` | ON | Precompiled headers |
| `SLIC3R_ASAN` | OFF | AddressSanitizer |
| `SLIC3R_BUILD_SANDBOXES` | OFF | Development sandboxes |
