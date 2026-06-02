# Impact Review 05 — Config Normalization / Presets / Inheritance / Backward-Compat

**Commit:** `eef00f7032` — "Add minimum chute flush length filament option"
**Feature:** per-filament `ConfigOptionFloats filament_minimal_purge_on_chute` (default `0`), read at emission via `get_at(new_filament_id)`.
**Lens:** config normalization, preset resize, inheritance, dirty/diff, backward-compat (old 3MF/projects/presets), interaction with related toggles.
**Scope:** `.github/` ignored per instructions.

---

## Methodology / trace

Traced the new key outward through every config-machinery touchpoint the peer option
`filament_minimal_purge_on_wipe_tower` occupies, and confirmed parity:

| Machinery site | Peer (`..._wipe_tower`) | New (`..._chute`) | Status |
|---|---|---|---|
| `PrintConfig.cpp` def (coFloats, `min=0`, default vector) | yes (`{15.}`) | yes (`{0.}`) | parity |
| `PrintConfig.hpp` `PRINT_CONFIG_CLASS_DEFINE` (`ConfigOptionFloats`) | yes | yes | parity |
| `Preset.cpp` `s_Preset_filament_options` whitelist | yes | yes | parity |
| `Preset.cpp` `Preset::normalize` resize-to-`n` loop | implicit (in list) | implicit (in list) | parity |
| `filament_options_with_variant` set | absent | absent | parity (correct) |
| `Print.cpp` `invalidate_state_by_config_options` | yes (psWipeTower) | yes (psWipeTower) | parity |
| `Plater.cpp` `on_config_change` refresh | yes | yes | parity |
| `Tab.cpp` build + toggle | yes | yes (inverse `is_BBL_printer`) | parity |

### 1. Resize to filament/extruder count — SAFE

`Preset::normalize` (Preset.cpp:442) resizes every key returned by
`Preset::filament_options()` (which is `s_Preset_filament_options`, now containing the new
key) to `n` via `static_cast<ConfigOptionVectorBase*>(opt)->resize(n, defaults.option(key))`
(Preset.cpp:466-476). `n` is the filament count (single-extruder branch, `filament_diameter`
size) or extruder count (multi-extruder branch, `nozzle_diameter` size). The default used to
backfill new slots is `0.` (the def's default value). The key is **not** in
`filament_options_with_variant` (PrintConfig.cpp:8369-8409), so it is *not* skipped by the
variant guard at Preset.cpp:469 and *not* re-handled by `extend_default_config_length`
(Preset.cpp:262-275, which only special-cases the three `*_with_variant` sets). This exactly
matches the peer option's handling — both are plain per-filament floats backfilled to `n`.

### 2. `get_at(new_filament_id)` validity across configurations — SAFE

`ConfigOptionVector::get_at` (Config.hpp:624-628) clamps: `return (i < size) ? values[i] :
values.front();`. There is an `assert(!values.empty())` (debug only) and a `values.front()`
on the clamp path — both UB only if the vector is empty in release. The vector cannot be empty
in practice:

- `FullPrintConfig`/`StaticPrintConfig` static default is `ConfigOptionFloats{ 0. }` — always
  ≥1 element (PrintConfig.cpp:2736).
- At slice time `m_config` is a `FullPrintConfig` populated via `apply(print_config)`
  (GCode.cpp:5548), seeded from the static default, so the member always carries ≥1 value even
  if the source preset omitted the key.
- After `Preset::normalize`, the vector is exactly `n` (filament count). Both emission reads
  index by `new_filament_id`, which is the same index used by sibling per-filament reads
  (`filament_max_volumetric_speed.get_at(new_filament_id)`, `nozzle_temperature.get_at(...)`)
  in the same blocks (GCode.cpp:880, 7845). If `new_filament_id` ever transiently exceeds the
  vector size, `get_at` clamps to `front()` — no OOB read.

Holds across single-extruder, multi-extruder, many AMS slots, and mid-session add/remove of
filaments (any add/remove re-runs `set_num_filaments`/normalize, and `get_at` clamps in the
interim).

### 3. Inheritance / system-preset compare / dirty indicator / diff — SAFE, no spurious dirty

- Preset load starts from `default_preset.config` (key present, value `0`) and applies the
  file's values on top (Preset.cpp:1664-1666), then `Preset::normalize` (Preset.cpp:1670). A
  file lacking the key keeps the default `0` across all `n` slots.
- Dirty detection is `config.diff(parent_config)` (Preset.cpp:659, 1762, 1821). Both edited and
  parent presets pass through the same normalize → same length `n`, same `0` fill → diff yields
  nothing for an unmodified preset. No spurious "modified" marker.
- No OrcaSlicer/BBL vendor profile JSON sets `filament_minimal_purge_on_chute` (grep of
  `resources/profiles/**` and `resources/profiles_template/**` finds the peer key only). So
  system presets resolve to the same `0` default as user presets — comparison is symmetric, no
  spurious inheritance divergence.
- Inheritance resolution: handled generically by `update_diff_values_to_child_config` /
  `apply`, which operate on whatever keys are present; the new key requires no special-casing
  and gets none, identical to the peer.

### 4. Backward-compat: old 3MF / projects / presets lacking the key — SAFE, slices identically

Loading a project/preset that predates the key leaves the static default (`0`) in place, which
means "disabled". With value `0`:
- BBL/Type1 path (GCode.cpp:880-886): `min_chute_purge = 0`, so `is_real_toolchange &&
  min_chute_purge > EPSILON` is false and `std::max({..., min_chute_purge})` adds nothing →
  byte-identical purge behaviour to pre-feature.
- Type2/no-wipe-tower path (GCode.cpp:7845-7850): `min_chute_purge > EPSILON` is false → the
  `if` is skipped, `wipe_volume` untouched.

No legacy key-rename / config-substitution path is involved (the key is brand-new, not a
rename), so no `handle_legacy_*` entry is required and none is missing.

### 5. Interaction with related toggles — SANE / INERT when no purge path

- `single_extruder_multi_material`: only affects whether `normalize` uses filament-count vs
  extruder-count for `n` (Preset.cpp:445-460); the key resizes correctly under both.
- `enable_prime_tower` OFF / Type2 path: handled by the `set_extruder` branch, which only pads a
  genuine flush (`wipe_volume > EPSILON`); when there is no flush, the option is inert.
- `purge_in_prime_tower` / wipe-tower path: handled by `append_tcr`, padding only real tool
  changes (`is_real_toolchange && tcr.purge_volume`-derived). When purge is fully diverted
  (`tcr.purge_volume < EPSILON`) the option intentionally re-introduces a floored poop — the
  feature's purpose — and only for real tool changes, so finish-layer/prime/same-tool emit
  nothing spurious.

### 6. Invalidation reaches G-code export — SAFE (no stale cache)

The option is consumed at G-code emission time (`append_tcr`, `set_extruder`), i.e. during
`psGCodeExport`. The invalidation branch enqueues `psWipeTower` + `psSkirtBrim`
(Print.cpp:288-370). `Print::invalidate_step` (Print.cpp:426-433) propagates any non-gcode
step invalidation to `psGCodeExport` unconditionally, so changing the chute value forces a
re-export and the new value takes effect. No stale-cache risk.

---

## Findings

**No issues found in config normalization / presets / inheritance / backward-compat.**

The new key is wired identically to its established peer `filament_minimal_purge_on_wipe_tower`
at every config-machinery site, normalizes/resizes correctly across all extruder/AMS
topologies, never produces an out-of-bounds or empty-vector read, introduces no spurious dirty
or inheritance divergence, loads old projects with an inert default, and correctly invalidates
through to G-code export.

### Out-of-lens observations (NOT config-normalization defects — noted only for the owning reviewers)

- **[Low] GCode.cpp:7847 (set_extruder / Type2 path)** — semantic asymmetry vs the wipe-tower
  path: this branch floors only to `min_chute_purge` and does **not** apply the built-in
  `g_min_purge_volume` (100 mm³) floor that the `append_tcr` branch applies
  (GCode.cpp:884-885). The tooltip claims a "built-in minimum of about 40 mm (100 mm³) already
  applies"; that statement is only true on the BBL/Type1 path. This is a behaviour/doc
  consistency matter for the G-code-emission reviewer, not a config-normalization issue.
- **[Low] PrintConfig.cpp:2722 (units)** — the option is a *length* in mm while the adjacent
  `filament_minimal_purge_on_wipe_tower` is a *volume* in mm³. The tooltip explicitly calls
  this out, so it is intentional, but the two sit on adjacent lines in the same optgroup and
  could confuse users. UX/Tab reviewer's call, not a normalization defect.

Both are flagged so they are not lost; neither affects preset resize, inheritance, dirty/diff,
or backward-compat.
