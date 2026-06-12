# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

OrcaSlicer is an open-source 3D slicer application forked from Bambu Studio. Built with C++17, wxWidgets GUI, and CMake. The codebase is 500k+ lines across core slicing engine (`src/libslic3r/`), GUI (`src/slic3r/GUI/`), and bundled dependencies (`deps_src/`).

## This Fork

- Tracks upstream OrcaSlicer (`SoftFever/OrcaSlicer`) nightly builds, merged every few days
- The **only** custom code is a Bambu Connect export plugin and the CI workflow to build it
- **Do NOT make changes unrelated to the Bambu Connect plugin**
- Keep the diff from upstream as minimal as possible
- **GitHub fork:** `cyberralf83/OrcaSlicer`, branch `nightly-builds-with-bc`
- Always use `-R cyberralf83/OrcaSlicer` with `gh` CLI commands (workflows, issues, PRs, etc.)

### Bambu Connect Plugin

Adds a "Send to BC" button for BBL printers that exports the sliced file and opens it in Bambu Connect via the `bambu-connect://` URL scheme. The custom code touches these files (relative to upstream):

- `src/slic3r/GUI/GLToolbar.cpp/hpp` - Declares/defines `EVT_GLTOOLBAR_SEND_BAMBU_CONNECT` event
- `src/slic3r/GUI/MainFrame.cpp/hpp` - Adds `eSendBambuConnect = 10` to `PrintSelectType` enum, "Send to BC" button label, dropdown menu entry, enable/disable logic, and event dispatch
- `src/slic3r/GUI/Plater.cpp` - Implements `on_action_send_bamcu_conect()` handler: calls `send_gcode()`, gets the 3MF path, URL-encodes parameters, and launches `bambu-connect://import-file?path=...&name=...&version=1.0.0` via `wxLaunchDefaultBrowser`. Also sets default print button to `eSendBambuConnect` for BBL printers.

Flow: Slice -> "Send to BC" button -> export 3MF -> URL-encode path/name -> open `bambu-connect://` URL scheme

### Minimum Chute Flush feature (`minimal_chute_flush_length`)

A custom fork feature (beyond the Bambu Connect plugin) that enforces a **minimum droppable chute "poop"** on BBL AMS colour changes. Process-scoped (Print Settings preset, "Flush options" optgroup), BBL-only, `coFloat` in **mm of filament feed**, default `0` = off. Touches `PrintConfig.cpp/hpp`, `Preset.cpp` (whitelist: in `s_Preset_print_options`), `GCode.cpp`, `Print.cpp` (invalidation), `Tab.cpp`, `ConfigManipulation.cpp`, `Plater.cpp`.

**Why it exists — the starvation regime stock Orca does NOT cover.** On BBL the chute purge volume is decided in `GCode.cpp:885-888`:
```cpp
float purge_volume = (tcr.purge_volume < EPSILON)
    ? (apply_chute_min ? std::max(min_chute_purge, g_min_purge_volume) : 0.f)
    : (apply_chute_min ? std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})
                       : std::max(tcr.purge_volume, g_min_purge_volume));
```
- `g_min_purge_volume = 100.f` (GCode.cpp:92) is the **upstream** Bambu floor (committed 2022, present in stock Orca) — NOT fork code. It only lives in the `≥ EPSILON` branch.
- `min_chute_purge = minimal_chute_flush_length × filament_area` (1.75 mm filament ⇒ `area ≈ 2.405 mm³/mm`).
- `apply_chute_min = is_real_toolchange && min_chute_purge > EPSILON && is_BBL_Printer()`.

**Flush-into-infill** (`mark_wiping_extrusions`, ToolOrdering.cpp) diverts purge into object infill *first*, shrinking `tcr.purge_volume`. When it absorbs ~everything, `tcr.purge_volume < EPSILON` → with the feature **off** the top branch returns **`0.f`** — the 100 mm³ floor is **bypassed, not applied**. Result: no real poop, only ooze/stringing (a thin ~30–40 mm × ~0.4 mm wisp ≈ 4–5 mm³) that clings to the nozzle instead of dropping. This is the real-world failure that motivated the feature; the feature is the **only** path that lifts those bypassed-to-0 changes back to a droppable ≥100 mm³ coil.

**Behaviour (three regimes; counterintuitive, by design — not a bug):**
- **0 mm = off:** absorbed changes emit 0 at the chute (lowest waste, but the sticky/non-drop case).
- **~1–41.6 mm = dead zone:** `min_chute_purge < 100`, so `max(setting, 100) = 100` regardless — every value gives the same result. (Below `100/area ≈ 41.6 mm` the slider appears stuck; first nonzero step still flips absorbed changes 0→100 mm³, a visible jump.)
- **> ~41.6 mm = real scaling:** `min_chute_purge` clears 100 and raises EVERY real colour change linearly (~2.4 mm³/mm).
- No value lands a chute purge **between 1 and 99 mm³** on a real change — the hardcoded 100 mm³ floor forbids it. To allow an intermediate droppable poop, the chute path would need to use the user's value as its own floor and bypass `g_min_purge_volume` (do NOT lower `g_min_purge_volume` globally — it floors normal colour-flush purge everywhere).

**Units/intuition:** report chute purge as VOLUME (mm³) or MASS (~0.12 g PLA per 100 mm³). The real ~100 mm³ poop is a ~2 mm-thick coil ~30 mm long with a thin drawn tail — NOT a clean 0.4 mm thread (100 mm³ as a 0.4 mm thread would be ~796 mm, which never physically forms). To inspect per-change chute volumes, slice headless and sum `G1 E` between `; FLUSH_START`/`; FLUSH_END`.

### CI Workflows

Two macOS workflows, both producing signed + notarized DMGs published to the `nightly-mac-arm64` GitHub release:

**`.github/workflows/build4mac.yml`** — cloud-only build, the source of truth.
- Manual trigger only (scheduled cron temporarily disabled — commented out, easy to restore; the schedule has been moved to `build4mac_local.yml`)
- Fetches and merges upstream `nightly-builds` tag into `nightly-builds-with-bc` branch
- Builds macOS ARM64 on `macos-14` using `build_release_macos.sh`
- Creates a DMG and publishes the release

**`.github/workflows/build4mac_local.yml`** — runs on schedule (every 4 days at 2 AM UTC) or manual trigger; routes to a self-hosted Mac ARM64 runner if one is online + idle, otherwise falls back to `macos-14`. Same final output as the cloud workflow. The `pick-runner` job probes `repos/cyberralf83/OrcaSlicer/actions/runners` via `PAT_TOKEN`. Cache is split into `actions/cache/restore` + `actions/cache/save@v4` with `if: always()` so the 1-hour deps build is preserved even if a later step fails.

### Self-hosted runner setup (Mac ARM64)

The runner lives at `/Users/michael/GitHub/self-hosted-runner-environment/orca-runner/`, registered as `Michaels-Mac-Studio-orca` with labels `[self-hosted, macOS, ARM64]`. It's per-repo (self-hosted runners on personal GitHub accounts can't be org/account-scoped).

**One-time host setup that the workflow itself cannot do:**

1. **Apple Developer ID intermediates → System.keychain.** macOS cloud runners get this from bundled Xcode; a Command-Line-Tools-only Mac does not. Without these in System.keychain, `codesign` returns `errSecInternalComponent` with "unable to build chain to self-signed root" — even with the intermediates imported into a temp keychain via the workflow (codesign on macOS 26.x Tahoe ignores user-domain keychains for chain validation). Fix:
   ```
   curl -fsSL -o /tmp/DeveloperIDCA.cer   https://www.apple.com/certificateauthority/DeveloperIDCA.cer
   curl -fsSL -o /tmp/DeveloperIDG2CA.cer https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer
   sudo security add-certificates -k /Library/Keychains/System.keychain \
     /tmp/DeveloperIDCA.cer /tmp/DeveloperIDG2CA.cer
   ```
   G1 expires Feb 2027, G2 expires Sep 2031. Re-install when refreshed.

2. **git-lfs.** Required by `actions/checkout@v5` with `lfs: true`. Cloud `macos-14` ships it; CLT does not. `brew install git-lfs && git lfs install`.

3. **Full Xcode is NOT required.** Command Line Tools is sufficient for the deps build and the OrcaSlicer build. The workflow's "Free disk space" step (which wipes other Xcode installs on cloud) is gated by `if: runner.environment == 'github-hosted'`, so the runner Mac's `/Applications/Xcode_*.app` is never touched.

**Repo secrets used by the sign + notarize step** (already configured for the cloud workflow, reused here):
- `PAT_TOKEN` — PAT with `repo` + `workflow` scopes (also used to probe the runners API in `pick-runner`)
- `BUILD_CERTIFICATE_BASE64`, `P12_PASSWORD`, `KEYCHAIN_PASSWORD`, `MACOS_CERTIFICATE_ID` — codesign
- `APPLE_DEV_ACCOUNT`, `TEAM_ID`, `APP_PWD` — notarytool (app-specific password from appleid.apple.com)

**Sign-step gotchas (Tahoe-specific):**
- `notarytool store-credentials` defaults to `login.keychain`, which can't be unlocked non-interactively. Workflow passes `--keychain "$KEYCHAIN_PATH"` to store the notarytool profile in the temp keychain instead.
- `security add-trusted-cert` prompts for admin authorization and hangs forever on non-interactive runners. Don't use it; rely on System.keychain trust instead.
- `security list-keychains -d user -s <new>` *replaces* the search list. The workflow captures the original list into a bash array via `mapfile`-style read and restores it via an `EXIT` trap so a failed sign step never strands the host Mac without `login.keychain` in its search path.

**Operating notes:**
- Trigger: `gh workflow run build4mac_local.yml -R cyberralf83/OrcaSlicer --ref nightly-builds-with-bc`
- Runner status: `gh api repos/cyberralf83/OrcaSlicer/actions/runners --jq '.runners[] | {name, status, busy, labels: [.labels[].name]}'`
- A fresh deps cache lives at the cache key `macos-14-cache-orcaslicer_deps-build-<hash of deps/**>`; first run is ~1h, subsequent runs skip Install-build-tools + Build-dependencies entirely.

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
