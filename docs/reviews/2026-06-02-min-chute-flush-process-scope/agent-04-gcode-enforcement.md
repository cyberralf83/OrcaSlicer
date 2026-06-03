# Agent-04 — GCode Enforcement Correctness

Review of PROPOSAL.md: move min-chute-flush from per-filament `ConfigOptionFloats filament_minimal_purge_on_chute` to a global scalar `ConfigOptionFloat minimal_chute_flush_length`, reading it at GCode.cpp:880 via `.value` instead of `.get_at(new_filament_id)`.

## Verdict

**APPROVE — no CRITICAL/HIGH findings.** The G-code enforcement math is correct, the default path is provably byte-identical to upstream, and the `.value` scalar read is the correct idiom for a `ConfigOptionFloat` on `FullPrintConfig`. The single global `mm` value yields a per-filament `mm³` floor, which is intended and unit-consistent. No new divide-by-zero / NaN path is introduced beyond the (pre-existing, harmless, unused) upstream one. The clamp is confined to `append_tcr`; the `set_extruder` path is byte-identical to upstream and `wipe_tower_type()` routes all BBL printers to Type1/`append_tcr`. Two LOW notes only (stale comment wording the proposal already plans to fix; a latent — pre-existing — `flush_unit` inf that survives the change unchanged).

## Confirmed correct

**1. Default byte-identity (claim #1) — PROVEN.**
Live fork at GCode.cpp:879-888:
```cpp
float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_filament_id), 2));
const float min_chute_length   = (float) full_config.filament_minimal_purge_on_chute.get_at(new_filament_id); // → .value after rename
const float min_chute_purge    = min_chute_length * filament_area;
const bool  is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool;
const bool  apply_chute_min    = is_real_toolchange && min_chute_purge > EPSILON && gcodegen.is_BBL_Printer();
float purge_volume = (tcr.purge_volume < EPSILON)
    ? (apply_chute_min ? std::max(min_chute_purge, g_min_purge_volume) : 0.f)
    : (apply_chute_min ? std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})
                       : std::max(tcr.purge_volume, g_min_purge_volume));
float purge_length = purge_volume / filament_area;
```
At default `0`: `min_chute_length = 0` → `min_chute_purge = 0 * filament_area = 0`. The gate tests `min_chute_purge > EPSILON`, and `EPSILON = 1e-4` (libslic3r.h:52, `static constexpr double EPSILON = 1e-4`), so `0 > 1e-4` is **false** → `apply_chute_min = false` unconditionally (the `is_real_toolchange` / `is_BBL_Printer()` factors are short-circuited / irrelevant). The expression collapses to:
```cpp
float purge_volume = (tcr.purge_volume < EPSILON) ? 0.f : std::max(tcr.purge_volume, g_min_purge_volume);
```
Upstream `WipeTowerIntegration::append_tcr` (SoftFever/OrcaSlicer main):
```cpp
float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);
float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_filament_id), 2));
float purge_length = purge_volume / filament_area;
```
`0` vs `0.f` both assign to a `float` → identical bits. `purge_length`, `first_flush_volume = purge_length/2`, `second_flush_volume = purge_length/2`, `flush_length = purge_length`, `flush_count = std::min(g_max_flush_count, (int)std::round(purge_volume / g_purge_volume_one_time))`, `flush_unit = purge_length / flush_count`, and the `flush_length_%d` loop are textually identical to upstream and derive only from `purge_volume`/`purge_length`. **Byte-identical at default. The array→scalar type change does not alter this** — at `0` both `.get_at(i)` and `.value` yield `0`.

**2. `.value` scalar read (claim #2) — VALID.**
`ConfigOptionFloat : ConfigOptionSingle<double>` (Config.hpp:764) and `ConfigOptionSingle<T>` has `public T value;` (Config.hpp:305-307). The `PRINT_CONFIG_CLASS_DEFINE(GCodeConfig, …)` macro generates a `ConfigOptionFloat minimal_chute_flush_length;` data member. `GCodeConfig` is a base of `PrintConfig` (PrintConfig.hpp:1485 `(MachineEnvelopeConfig, GCodeConfig)`), which `FullPrintConfig` derives from, so `full_config.minimal_chute_flush_length.value` (a `double`, cast to `float`) is reachable. This matches the idiomatic sibling reads in the same file: GCode.cpp:3058 `m_config.initial_layer_print_height.value` (ConfigOptionFloat), :816 `gcodegen.m_config.prime_tower_skip_points.value` (bool), :2149 `m_config.enable_prime_tower.value`. No `option<ConfigOptionFloat>("…")->value` indirection is needed — that form is for `DynamicPrintConfig`, not the typed `StaticPrintConfig`/`FullPrintConfig`. **The proposed `full_config.minimal_chute_flush_length.value` is correct.**

**3. mm→mm³ conversion (claim #7) — CORRECT & intended, no unit confusion.**
`min_chute_purge [mm³] = min_chute_length [mm] * filament_area [mm²]`, with `filament_area` from the *new* filament's diameter. With a global `mm` value this yields a **per-filament mm³ floor** (a thicker filament → larger volume for the same length). Units are consistent: the result is compared against `g_min_purge_volume` (100 mm³) and `tcr.purge_volume` (mm³), and `purge_length = purge_volume / filament_area` converts back to mm. The option's `sidetext = "mm"` and the (planned) reworded tooltip correctly describe it as a length. No mm-treated-as-mm³ confusion.

**4. Divide-by-zero / NaN (focus item) — no new path; floor guarantees flush_count ≥ 1 when the feature fires.**
`flush_unit = purge_length / flush_count` (line 970). `flush_count = std::min(4, round(purge_volume / 135))`. When `apply_chute_min == true`, `purge_volume` is `std::max(min_chute_purge, 100)` or `std::max({tcr.purge_volume, 100, min_chute_purge})` — both include `g_min_purge_volume = 100`, so `purge_volume ≥ 100` ⇒ `round(100/135) = round(0.74) = 1` ⇒ `flush_count ≥ 1` ⇒ no division by zero on the feature path. The ≥100 mm³ floor (claim) is confirmed. `g_purge_volume_one_time = 135.f` (GCode.cpp:93), `g_max_flush_count = 4` (:94), `g_min_purge_volume = 100.f` (:92). The only `flush_count == 0` case is when `0 < purge_volume < ~67.5` (i.e. small `tcr.purge_volume` below 100 — only reachable with `apply_chute_min == false`, including the default), in which case `flush_unit` becomes `+inf` but the `for (; flush_idx < flush_count; …)` loop never executes, so the inf is **computed-but-never-stored**. This is identical to upstream (upstream has the same `flush_unit = purge_length / flush_count` with the same guard loop) and is **not introduced or widened by this change**. See LOW-2.

**5. Scope confinement (focus item) — clamp ONLY in `append_tcr`; `set_extruder` byte-identical.**
`set_extruder` (GCode.cpp:7701-7941) computes `wipe_volume`/`wipe_length` with no chute clamp, no `min_chute_*`, no `g_min_purge_volume` floor, and `flush_count = std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time))` / `flush_unit = wipe_length / flush_count` (7928-7929) exactly as upstream. Confirmed `grep` shows the only `filament_minimal_purge_on_chute` reference in GCode.cpp is line 880 (inside `append_tcr`). **`set_extruder` is untouched.**

**6. BBL routing (focus item) — all BBL printers → Type1 → `append_tcr`.**
`Print::wipe_tower_type()` (Print.hpp:1072): `return is_BBL_printer() ? WipeTowerType::Type1 : m_config.wipe_tower_type.value;`. In `WipeTowerIntegration` the dispatch (GCode.cpp:1500/1516/1605) routes `Type2 → append_tcr2`, **else `append_tcr`** (lines 1555, 1565). So every BBL wipe-tower toolchange flows through `append_tcr` where the clamp lives. The `is_BBL_Printer()` factor in `apply_chute_min` (GCode.cpp:257 declares `bool GCode::is_BBL_Printer();`) is a correct belt-and-braces guard matching the BBL-only UI; for non-BBL it would be Type2 (`append_tcr2`, no clamp) anyway, so the clamp is doubly BBL-scoped.

**7. is_real_toolchange / is_BBL_Printer gates — no missed colour change, no spurious fire.**
`is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool` (WipeTower.hpp:85/100/103 confirm the fields). A legitimate colour change *requires* both conditions, so none is missed; a non-toolchange or same-tool/priming entry has `is_real_toolchange == false`, so the floor never spuriously fires. These gates are moot for default byte-identity (already false via `min_chute_purge == 0`).

**8. Reference completeness (claim #5) — exactly 8 references in 7 files; no stray, no collision.**
`grep` across `src/`: the old key appears at PrintConfig.cpp:2722, PrintConfig.hpp:1456, Print.cpp:289, GCode.cpp:880, Preset.cpp:1282, Plater.cpp:16692, Tab.cpp:4151, Tab.cpp:4362 — exactly the 8 the proposal enumerates. Zero references to `minimal_chute_flush_length` (no collision) and zero references to either key under `resources/` (no profile JSON, no `.po`/`.mo`, no 3MF). The proposal's "never compiled/shipped → skip handle_legacy" reasoning holds.

## Findings

**[LOW] GCode.cpp:873-874 — comment says "per-filament"; after rename the value is global.** The proposal (edit #4) already plans `// per-filament` → `// global`. Confirming the change is needed: the block comment at 873 ("Enforce a per-filament minimum chute flush") and 874 ("The option is a filament length (mm)") should be reworded so the comment matches the global scalar. Fix: as proposed — drop "per-filament", e.g. "Enforce a global minimum chute flush; the option is a filament length (mm) converted per-filament to mm³ via the new filament's diameter." Cosmetic only; no behavior impact.

**[LOW] GCode.cpp:970 (and pre-existing :7929) — `flush_unit = purge_length / flush_count` divides by `flush_count` which can be 0.** Pre-existing in both fork and upstream; the inf result is never stored because the consuming loop is bounded by the same `flush_count`. This change neither introduces nor fixes it; flagged only for completeness. No action required for this proposal. (If ever hardened, guard with `flush_count > 0 ? purge_length / flush_count : 0.f` in both `append_tcr` and `set_extruder` — but that would itself be a non-byte-identical upstream divergence and is out of scope here.)

## Sources

- Live fork: `src/libslic3r/GCode.cpp` lines 92-94 (g_* constants), 712-741 (`append_tcr` entry), 855-983 (clamp + flush block), 1500-1608 (Type1/Type2 dispatch), 7701-7941 (`set_extruder`, byte-identical).
- `src/libslic3r/libslic3r.h:52` — `EPSILON = 1e-4`.
- `src/libslic3r/Config.hpp:305-307, 764-768` — `ConfigOptionSingle<T>::value`, `ConfigOptionFloat`.
- `src/libslic3r/PrintConfig.hpp:1303-1304, 1453-1457, 1483-1485` — `GCodeConfig` macro block, option at 1456, `PrintConfig` derivation.
- `src/libslic3r/PrintConfig.cpp:2711-2736` — current `filament_minimal_purge_on_chute` def.
- `src/libslic3r/Print.hpp:1072` — `wipe_tower_type()` BBL→Type1.
- `src/libslic3r/GCode/WipeTower.hpp:82-103` — `priming`, `is_tool_change`, `initial_tool`, `new_tool`.
- `src/libslic3r/GCode.hpp:257` — `bool GCode::is_BBL_Printer();`.
- `grep` over `src/` and `resources/` — 8 old-key references, 0 new-name collisions, 0 resource references.
- Upstream `SoftFever/OrcaSlicer` `src/libslic3r/GCode.cpp` `WipeTowerIntegration::append_tcr` (raw.githubusercontent.com/SoftFever/OrcaSlicer/main) — `purge_volume`/`purge_length`/`flush_count`/`flush_unit` block + g_* constants, diffed against the fork's default-collapsed form.
