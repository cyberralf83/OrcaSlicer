# Review 02 — Config Option Registration Correctness & Consistency

**Feature:** new per-filament option `filament_minimal_purge_on_chute` (`ConfigOptionFloats`, mm, default 0 = off), mirroring `filament_minimal_purge_on_wipe_tower`.

**Lens:** registration correctness, per-filament vector sizing/normalization, FullPrintConfig reachability, label/tooltip/sidetext sanity.

**Files in scope:** `src/libslic3r/PrintConfig.cpp`, `src/libslic3r/PrintConfig.hpp`, `src/libslic3r/Preset.cpp`, `src/libslic3r/Print.cpp`, `src/slic3r/GUI/Tab.cpp`, `src/slic3r/GUI/Plater.cpp`, with cross-checks into `src/libslic3r/PresetBundle.cpp` and `src/libslic3r/Config.hpp`.

---

## Verdict

**No registration-correctness issues found.** The option is registered consistently with its `filament_minimal_purge_on_wipe_tower` template, declared in the correct config class (`GCodeConfig`, which flows into `FullPrintConfig`), and — critically — added to `s_Preset_filament_options`, which is the list `Preset::normalize()` uses to resize per-filament vector options to the filament count. That resize is what guarantees `get_at(new_filament_id)` in `GCode.cpp` reads the right per-filament slot rather than a stale or wrong index. All wiring (invalidation, GUI page, toggle, Plater config-change) is present and matches the mirrored option. One Low note on tooltip/units phrasing and one Low note on the (intentional, already-documented in the diff comment) semantic divergence from the wipe-tower option's units.

---

## Field-by-field comparison vs. `filament_minimal_purge_on_wipe_tower`

`PrintConfig.cpp` (new block ~2722–2734) vs. mirror (~2711–2720):

| Field | wipe_tower | chute (new) | OK? |
|-------|-----------|-------------|-----|
| `add(..., type)` | `coFloats` | `coFloats` | yes — matches `.hpp` macro `ConfigOptionFloats` |
| `label` | "Minimal purge on wipe tower" | "Minimal purge on chute" | yes, wrapped in `L()` |
| `tooltip` | present, `L()` | present, `L()` | yes |
| `sidetext` | `u8"mm³"` (volume) | `"mm"` (length) | intentional divergence — see Low-1 |
| `min` | `0` | `0` | yes |
| `max` | (unset) | (unset) | consistent |
| `mode` | `comAdvanced` | `comAdvanced` | yes |
| default | `ConfigOptionFloats { 15. }` | `ConfigOptionFloats { 0. }` | yes — matches spec (0 = off) |

`.hpp` declaration (`PrintConfig.hpp:1456`) sits in the `GCodeConfig` block (opened at line 1303) immediately after the `filament_minimal_purge_on_wipe_tower` line (1455). Type `ConfigOptionFloats` matches the `coFloats` in `add()`. Correct.

---

## Per-filament vector sizing — the load-bearing question

This is where a `coFloats` per-filament option can silently misbehave if mis-registered. Findings:

1. **`get_at` is index-clamped, not unchecked.** `ConfigOptionVector::get_at` (`Config.hpp:624-628`) returns `values[i]` when `i < size()`, else `values.front()`. So even an undersized vector cannot read out-of-bounds garbage; worst case it would fall back to filament 0's value. The only hard failure is an *empty* vector (`assert(!values.empty())` at line 626; UB in release if it were empty).

2. **The vector IS resized to the filament count, via the list the diff edited.** Per-filament vector options are normalized in `Preset::normalize()` (`Preset.cpp:462-476`): it iterates `Preset::filament_options()` (= `s_Preset_filament_options`) and calls `resize(n, defaults.option(key))` for each vector key, where `n` = number of filament presets. The diff adds `filament_minimal_purge_on_chute` to `s_Preset_filament_options` (`Preset.cpp:1282`), so the new key participates in this resize exactly like every other per-filament option. This is the correct and **required** registration step — without it, a multi-filament config could leave the vector at its 1-element default and every filament would read filament 0's value. It is present. Good.

3. **`m_filament_option_keys` is NOT the relevant list here.** `init_filament_option_keys()` (`PrintConfig.cpp:7378-7386`) is a small hardcoded set (diameter, retraction keys, colour, …) consumed by `DynamicPrintConfig::set_num_filaments` (`PrintConfig.cpp:8799-8813`). The mirrored `filament_minimal_purge_on_wipe_tower` is **also absent** from this list, and it works correctly — so this list is not the per-filament sizing path for these purge options, and the new key does not need to be added to it. No action required. (Adding it would be redundant, not harmful, but is unnecessary and would widen the upstream diff.)

4. **Empty-vector / assert risk: none in practice.** Because the key is in `s_Preset_filament_options`, every loaded/edited config resizes the vector to `n >= 1` filaments before slicing. The `GCode.cpp` call `full_config.filament_minimal_purge_on_chute.get_at(new_filament_id)` therefore always hits a populated vector, identical to the proven `filament_minimal_purge_on_wipe_tower` path.

---

## FullPrintConfig reachability (so GCode.cpp can read it)

Inheritance chain confirmed in `PrintConfig.hpp`:

- New key declared in `GCodeConfig` (block at line 1303).
- `PrintConfig` derives from `(MachineEnvelopeConfig, GCodeConfig)` (line 1483-1485).
- `FullPrintConfig` derives from `(PrintObjectConfig, PrintRegionConfig, PrintConfig)` (line 1666-1668).

`GCode.cpp` reads it through `FullPrintConfig& full_config = gcodegen.m_config;` (`GCode.cpp:859`) as `full_config.filament_minimal_purge_on_chute.get_at(...)` (~882). Because `FullPrintConfig` transitively includes `GCodeConfig`, this resolves and compiles. The same `apply_only` path (`Print.cpp:3022`) that populates the wipe-tower option populates this one. Correct.

---

## Supporting wiring (all present and consistent with the mirror)

- **Invalidation:** `Print.cpp:289` adds the key to `invalidate_state_by_config_options`, adjacent to the wipe-tower key. Correct — a change must re-trigger G-code export.
- **GUI page:** `Tab.cpp:4151` appends the option line on the Filament → Multimaterial → "Wipe tower parameters" group, right after the wipe-tower option.
- **GUI toggle:** `Tab.cpp:4360` does `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` — correctly the inverse of the wipe-tower-family options which are gated `!is_BBL_printer`. This matches the tooltip's "Only effective on … chute via the change filament G-code (e.g. Bambu Lab)" claim.
- **Plater config-change:** `Plater.cpp:16692` adds the key to the wipe-tower-related refresh branch.

---

## Findings

### Low-1 — Units intentionally differ from the mirrored option (length mm vs. volume mm³)
**File:** `src/libslic3r/PrintConfig.cpp:2731` (sidetext `"mm"`); compare `GCode.cpp:880-882`.
**Issue:** `filament_minimal_purge_on_wipe_tower` is a **volume** (`mm³`); the new `filament_minimal_purge_on_chute` is a **filament length** (`mm`). The clamp in `GCode.cpp` converts it via `min_chute_length * filament_area` to mm³ before comparing against `tcr.purge_volume`. This is internally consistent (sidetext `mm` matches the length semantics and the conversion), but it is a deliberate divergence from the option it "mirrors."
**Why it matters:** Low. Functionally correct, but a user familiar with the mm³ wipe-tower field may misread the chute field's magnitude. The tooltip does say "Minimum length of filament … (mm)," which mitigates this.
**Fix (optional):** None required. If maximal consistency with the mirror were desired, the option could be expressed in mm³ to match `filament_minimal_purge_on_wipe_tower`; but length-in-mm is arguably the more intuitive unit for "poop length," so the current choice is defensible. Leave as-is unless the broader review prefers unit parity.

### Low-2 — Tooltip embeds a literal escaped quote; verify it renders/translates cleanly
**File:** `src/libslic3r/PrintConfig.cpp:2726-2727`.
**Issue:** Tooltip contains `the \"poop\"`. The escaped double-quotes are valid C++ and will display as straight quotes. Translation extraction (`L()` + gettext) handles embedded quotes, so this is fine, but the slang term "poop" inside a translatable string is unusual and will be surfaced to translators.
**Why it matters:** Low/cosmetic. No functional impact.
**Fix (optional):** Consider rephrasing to "the leftover chute purge can become too small to fall free" without the parenthetical slang, or keep it — purely editorial.

---

## Items explicitly checked and clean

- Type parity `.cpp` `coFloats` ↔ `.hpp` `ConfigOptionFloats` — clean.
- `min = 0`, `mode = comAdvanced`, default `{0.}` all present and sane — clean.
- Key present in `s_Preset_filament_options` → per-filament resize via `Preset::normalize` — clean (this is the critical one).
- Reachable in `FullPrintConfig` for `GCode.cpp` — clean.
- `get_at(new_filament_id)` cannot read a wrong/out-of-range index after normalization; clamps to front() in the degenerate case and the vector is never empty on the slicing path — clean.
- Invalidation, GUI page, GUI toggle (correctly BBL-only, inverse of wipe-tower family), Plater refresh — all present and consistent — clean.
- No need to add the key to `m_filament_option_keys` (the mirror isn't there either; that list is not this sizing path) — clean.
