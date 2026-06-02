# Cross-Cutting Compatibility Review — `filament_minimal_purge_on_chute`

Commit under review: `eef00f7032` ("Add minimum chute flush length filament option")

Scope/lens: cross-cutting impact across the whole repo (calibration, CLI/headless, 3MF
round-trip, sequential/by-object/multi-plate, and any other consumers of purge/flush/poop
or filament-option enumeration). `.github/` excluded per instructions.

Feature summary: a new per-filament `coFloats` option (mm of filament, default `0` = disabled)
that floors the chute "poop" flush on Bambu AMS tool changes. Enforced in two emitters:
`WipeTowerIntegration::append_tcr` (Type1/BBL path, GCode.cpp:873-885) and `GCode::set_extruder`
(Type2/no-wipe-tower path, GCode.cpp:7843-7850). Also adds `flush_count = std::max(1, ...)`
guards at GCode.cpp:969 and 7934.

---

## Verdict

**No blocking issues found in cross-cutting compatibility.** The option is wired exactly like
its sibling `filament_minimal_purge_on_wipe_tower` and flows through every generic mechanism
(filament-option whitelist, vector resize, 3MF serialize/deserialize, CLI config-key iteration,
invalidation). One Low note (preview/estimate does not reflect the chute floor — pre-existing
design, not a regression) and two informational observations are recorded below.

---

## Findings

### [Low] Wipe-tower preview / pre-slice estimate does not reflect the chute floor — by design, not a regression
- File: `src/libslic3r/GCode/WipeTower2.cpp:2195-2198` (`get_wipe_volumes`)
- Interaction: The wipe-tower volume estimate used for preview includes
  `filament_minimal_purge_on_wipe_tower` but, correctly, **not** the chute minimum — the chute
  purge is emitted through `change_filament_gcode` (the AMS poop chute), not through wipe-tower
  geometry. So a user who raises `filament_minimal_purge_on_chute` sees no change in the wipe-tower
  preview.
- Why it is not a defect: The chute purge has never been part of the wipe-tower volume model;
  this is consistent with upstream behaviour for the chute mechanism in general. Final
  filament-usage statistics are computed post-hoc by `GCodeProcessor` parsing the actually-emitted
  flush moves (`UsedFilaments::update_flush_per_filament`, GCodeProcessor.cpp:1537), so the floored
  purge **is** reflected in the reported total filament used. There is no stats discrepancy.
- Fix: None required. If exact pre-slice estimation of chute purge were ever desired it would be a
  separate enhancement; out of scope for this change.

### [Info] Calibration flows are unaffected — they do not pass through the modified emitters
- Files: `src/slic3r/Utils/CalibUtils.cpp`, `src/libslic3r/calib.cpp:804-805`
- Interaction: Calibration G-code uses `GCodeWriter::set_extruder` (GCodeWriter.cpp:1200), a
  low-level writer, **not** the modified `GCode::set_extruder` (GCode.cpp:7699). `calib.cpp` builds
  custom G-code directly and does not invoke `append_tcr`/`get_path_of_change_filament`. The clamp
  cannot affect calibration prints.
- Why: The two methods are distinct (`GCodeWriter::set_extruder` vs `GCode::set_extruder`); only the
  latter was touched. Calibration is single-extruder/single-filament in practice and never reaches
  the multi-filament toolchange flush block.
- Fix: None.

### [Info] CLI / headless and 3MF round-trip are handled generically and correctly
- Files: `src/OrcaSlicer.cpp` (config merge via `config.keys()` at 2691, 3152; `update_full_config`),
  `src/libslic3r/Format/3mf.cpp` (write via `config.keys()`/`opt_serialize` at 2947, 3073, 3107,
  3164; read via `set_deserialize` at 904, 1152, 2208), `src/libslic3r/Preset.cpp:462-476`
  (filament-vector resize), `src/libslic3r/Config.hpp:624-628` (`get_at`).
- Interaction:
  - **CLI**: never references the key by name; it iterates `config.keys()` and applies generically.
    No CLI code needs awareness of the new key.
  - **3MF save/reload (key set)**: `coFloats` serializes/deserializes through the generic metadata
    path; preserved and applied on reload.
  - **Pre-feature 3MF (no key)**: the key is simply absent on load; `Preset::filament_options()`
    includes it (Preset.cpp:1282 whitelist edit), so the resize loop at Preset.cpp:474-475 pads it
    to the default `{0.}` for the filament count. Default 0 → clamp inert → identical slice. Round-trip
    requirement holds.
  - **New 3MF into old build**: key recorded as an `unrecogized_keys` non-fatal substitution
    (Config.cpp:583-585) — standard forward-compat, not this fork's concern.
- Why this is safe — the load-bearing dependency: the resize at Preset.cpp:474-475 only runs for keys
  returned by `Preset::filament_options()`. Because the Preset.cpp whitelist edit is present, the new
  key's vector is always sized to the filament count before slicing, so `get_at(new_filament_id)`
  never reads an empty/short vector. Had the whitelist edit been omitted, an old 3MF (or any config
  missing the key) could leave the vector empty and `get_at`'s `values.front()` fallback
  (Config.hpp:627) would be UB in a release build. The edit is present and correct.
- Fix: None.

### [Info] Sequential / by-object / multi-plate printing — no special interaction
- File: `src/libslic3r/GCode.cpp:7699,7749-7750` (`set_extruder(..., bool by_object, ...)`)
- Interaction: The Type2 clamp sits after `wipe_volume` is computed and is independent of the
  `by_object` branch (which only injects object-change labels). Per-object toolchanges still
  flow through the same `wipe_volume > EPSILON` guard, so the floor applies identically whether
  printing by-layer or by-object. The Type1 path uses `tcr.is_tool_change && initial_tool != new_tool`
  (`ToolChangeResult` fields confirmed in WipeTower.hpp:85,100,103) to pad only genuine tool changes,
  correctly excluding finish-layer/priming/same-tool `tcr` records. Multiple plates reuse the same
  per-filament config; no shared mutable state is introduced.
- Fix: None.

### [Info] No other purge/flush/poop consumer needs awareness of the new key
- Files scanned: all `*.cpp`/`*.hpp` under `src/` for `poop`/`chute`/`purge`/`flush`.
  - The only chute-purge emit sites are the two modified blocks (GCode.cpp:883 and 7842); the third
    `wipe_volumes` site (GCode.cpp:4760, `get_next_extruder`) is a next-tool selection heuristic that
    emits no purge and correctly stays untouched.
  - `MachineObject::command_xcam_control_purgechutepileup_detection` (DeviceManager.cpp:2189) and the
    `purgechutepileup_detection` UI (PrintOptionsDialog) are a **device-side, runtime** AMS chute
    pile-up detection feature with no slicing coupling. Raising the chute floor lengthens the poop,
    which is the intended effect; there is no code-level interaction or shared state. Operational note
    only: users who set a large chute minimum and also enable chute pile-up detection may see the
    printer flag pile-up sooner — expected, not a defect.
  - `filament_options_with_variant` (PrintConfig.cpp:8369) does **not** contain the new key nor its
    sibling, so both follow the identical generic resize path (Preset.cpp:474-475). Multi-extruder
    per-variant mapping (`update_values_to_printer_extruders`) does not need the key.
  - `EditGCodeDialog` (EditGCodeDialog.cpp:253) lists the key as an available custom-G-code
    placeholder via `Preset::filament_options()` — consistent with the sibling; the value is read from
    the fully-sized `full_config`, so no empty-vector access.
- Fix: None.

---

## Compatibility checklist

| Concern | Result |
|---|---|
| Calibration prints affected by clamp | No — different code path (`GCodeWriter::set_extruder`) |
| CLI/headless needs key awareness | No — generic `config.keys()` flow |
| 3MF save+reload preserves & applies | Yes — generic `coFloats` serialize/deserialize |
| Pre-feature 3MF → default 0 → identical slice | Yes — whitelist + resize pad to `{0.}` |
| Sequential / by-object / multi-plate | No special interaction; floor applies uniformly |
| Filament stats reflect floored purge | Yes — post-hoc G-code parse in GCodeProcessor |
| Other purge/flush/poop consumers | None require changes |
| `config.keys()`/option-enum surprises | None — key handled generically everywhere |
| Empty-vector `get_at` UB risk | Mitigated by whitelist-driven resize (load-bearing) |
