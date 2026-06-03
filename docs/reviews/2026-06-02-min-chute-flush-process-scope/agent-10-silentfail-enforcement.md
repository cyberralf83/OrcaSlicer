# Agent 10 — Silent-Failure Hunter — min-chute-flush process-scope rename

Scope: silent failures only (compiles + runs but quietly does the wrong thing / nothing, no
error surfaced). Enforcement (`append_tcr`) + invalidation (`Print.cpp`, `Plater.cpp`) focus.

**Live-code state at review time:** the proposal is NOT yet applied. Every reference still uses the
OLD key `filament_minimal_purge_on_chute` (`ConfigOptionFloats`, per-extruder). This review verifies
the *proposed* transition against live code so the rename does not introduce silent failures once
applied.

## Verdict

No CRITICAL or HIGH silent-failure defects in the proposed design. The core claims hold:
default output is provably byte-identical to upstream, the `.value` read is a real static struct
member (cannot silently default-to-0 from a whitelist mismatch), invalidation maps to G-code
regeneration, and `set_extruder` is byte-identical to upstream (no reverted `max(1,…)` regression).
Two LOW divide-by-zero hazards (`flush_count==0`, `filament_area==0`) exist but are **pre-existing
and identical to upstream** — the rename neither introduces nor worsens them. One LOW
implementation risk is in the proposed Tab.cpp toggle wiring (scope + delegation), which is a
"feature silently doesn't toggle" risk, not a slicing silent failure.

## Findings

### [LOW] `flush_count == 0` → `flush_unit = purge_length / 0` (NaN/inf) — pre-existing, NOT introduced by rename
- Evidence: `GCode.cpp:969-970`
  `int flush_count = std::min(g_max_flush_count, (int)std::round(purge_volume / g_purge_volume_one_time)); float flush_unit = purge_length / flush_count;`
  Upstream raw `src/libslic3r/GCode.cpp` (fetched) is character-identical here, and the
  `set_extruder` twin at `GCode.cpp:969`/upstream:7915 has the same shape. No `std::max(1,…)` guard
  in upstream or fork.
- How it could manifest: if `purge_volume` rounds to `< 67.5 mm³` (i.e. `round(purge_volume/135) == 0`),
  `flush_count == 0`, then `flush_unit = purge_length/0` → inf, and the `for (; flush_idx<flush_count;…)`
  loop body never runs so no `flush_length_N` is emitted — silently. BUT: the enforcement path
  always raises `purge_volume` to `g_min_purge_volume = 100` whenever it is non-zero (`std::max(…,100)`),
  and `round(100/135)=1`; and when `tcr.purge_volume < EPSILON && !apply_chute_min`, `purge_volume==0`
  so the whole block contributes nothing. So `flush_count==0` is **unreachable on every realized path**
  in both upstream and fork.
- Relevance to this rename: the min-chute floor (`std::max(min_chute_purge, g_min_purge_volume)`) can
  only ever *raise* `purge_volume` to ≥100, so it cannot newly drive `flush_count` to 0. The rename
  does not touch this hazard.
- Fix (optional hardening, upstream-wide, out of scope for this rename): `int flush_count =
  std::max(1, std::min(g_max_flush_count, (int)std::round(purge_volume / g_purge_volume_one_time)));`
  Do NOT add this as part of the rename — it would diverge from upstream and is the exact
  `max(1,…)` change the project previously reverted (see Non-issue 7).

### [LOW] `filament_area == 0` if a filament diameter is 0 → `min_chute_purge` collapses / `purge_length = x/0` — pre-existing, NOT introduced
- Evidence: `GCode.cpp:879` `filament_area = (M_PI/4)*pow(filament_diameter.get_at(new_filament_id),2)`,
  `:881 min_chute_purge = min_chute_length * filament_area`, `:888 purge_length = purge_volume/filament_area`.
- How it could manifest: a `filament_diameter == 0` makes `filament_area == 0`, so (a) `min_chute_purge`
  becomes 0 → the chute floor silently never fires (feature silently no-ops for that filament), and
  (b) `purge_length = purge_volume/0` → inf flows into `flush_length`. This is silent.
- Relevance to this rename: `filament_area` and `purge_length` already exist verbatim in upstream
  (`purge_length = purge_volume / filament_area`), so case (b) is pre-existing and untouched. Case (a)
  is a property the proposal explicitly intends (claim #7: "a single global mm value yields a
  per-filament mm³ floor"); a 0 diameter is an invalid filament profile, not a realistic default.
  `filament_diameter` defaults to 1.75. No new hazard from the scalar read.
- Fix: none required for the rename. (Diameter validation is an upstream concern.)

### [LOW] Proposed Tab.cpp toggle wiring — risk the row silently never shows/hides on BBL
- Evidence (live): `TabPrint::toggle_options()` at `Tab.cpp:2829-2881` does NOT make inline
  `toggle_line(...)` calls for print options; it computes `is_BBL_printer` **inside** the
  `if (m_preset_bundle) { … }` block (line 2833-2836) and otherwise delegates toggling to
  `m_config_manipulation.toggle_print_fff_options(m_config, …)` (line 2838). Contrast `TabFilament`
  (live `Tab.cpp:4362`) and `TabPrinter` (e.g. `Tab.cpp:5454`) which DO call `toggle_line/toggle_option`
  inline and demonstrably work.
- How it could manifest: if the proposed `toggle_line("minimal_chute_flush_length", is_BBL_printer)`
  is dropped in at "≈2835" as written, the `is_BBL_printer` local is in scope only inside the
  `if (m_preset_bundle)` braces; placing the call outside those braces is a compile error (caught,
  not silent), while placing it inside works. The real *silent* risk is the row simply not appearing
  on the Print-Settings page if `minimal_chute_flush_length` is never added to the optgroup at
  `Tab.cpp:2682-2686` ("Flush options"), or the toggle never re-firing on printer change. `toggle_line`
  itself is a valid `Tab` member (`Tab.cpp:1442`) that operates on `m_active_page`, so an inline call
  in `TabPrint::toggle_options()` *will* take effect — provided the option line was appended to the
  active page. The proposal does specify both the append (≈2685) and the toggle (≈2835), so the
  design is sound; the risk is purely getting both edits right.
- Note: this is a "feature visibility silently wrong" GUI risk, not a slicing/G-code silent failure.
  At default 0 with the row hidden or shown, emitted G-code is unaffected.
- Fix: put the `toggle_line` call where `is_BBL_printer` is in scope (inside the `if (m_preset_bundle)`
  block, or hoist the bool), and confirm the `append_single_option_line` lands in the "Flush options"
  optgroup (2682-2686). Verify by building and toggling printer vendor.

## Non-issues (proven safe)

1. **`.value` read is real, never a silent default-0 from whitelist mismatch.**
   `FullPrintConfig` derives `(PrintObjectConfig, PrintRegionConfig, PrintConfig)`
   (`PrintConfig.hpp:1666-1669`); `PrintConfig` aggregates `GCodeConfig` (`PrintConfig.hpp:1304`,
   1483-1484). So a `ConfigOptionFloat minimal_chute_flush_length` declared in the `GCodeConfig` macro
   block is a **compile-time static member** of `FullPrintConfig`. `full_config.minimal_chute_flush_length.value`
   resolves to that field at compile time — if the member did not exist the code would **fail to
   compile** (loud), never silently yield 0. The struct location (staying in `GCodeConfig`) is
   irrelevant to read correctness; only the `s_Preset_*` whitelist governs persistence. Claim #2 holds.

2. **Default (option=0) is byte-identical to upstream — proven.**
   With `minimal_chute_flush_length.value == 0`: `min_chute_purge = 0*filament_area = 0`;
   `apply_chute_min = is_real_toolchange && (0 > EPSILON) && is_BBL = false` (the `> EPSILON` term is
   false). The ternary at `GCode.cpp:884-887` collapses to
   `purge_volume = tcr.purge_volume < EPSILON ? 0.f : std::max(tcr.purge_volume, g_min_purge_volume)`.
   Upstream raw `GCode.cpp` (fetched) line 873:
   `float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);`
   — **identical**. `filament_area` (879), `purge_length = purge_volume/filament_area` (888),
   `flush_length` (962), `flush_count` (969), `flush_unit` (970) and the `flush_length_N` loop (972-981)
   are byte-identical to upstream. Therefore purge_volume / flush_length / flush_count / flush_length_N /
   change_filament_gcode are byte-identical at default. Claim #1 holds.

3. **Whitelist reclassification is the only persistence mechanism, and it feeds the typed config.**
   `PresetBundle` constructs the `prints` collection with `Preset::print_options()` as its key set
   (`PresetBundle.cpp:322`). `construct_full_config` seeds defaults via `out.apply(FullPrintConfig::defaults())`
   then `out.apply(print_config)` (`PresetBundle.cpp:79,81`) — the whole print-preset DynamicPrintConfig
   is copied into the typed output, so a key present in the print preset (because it is whitelisted)
   lands in `full_config`. Removing from `s_Preset_filament_options` and adding to
   `s_Preset_print_options` fully reclassifies it; no other mechanism (cereal handled by macro,
   no `handle_legacy` needed — never shipped). Claim #3 holds.

4. **Invalidation maps to G-code regeneration.** `Print.cpp:289` (current old-key line) sits in the
   block that `steps.emplace_back(psWipeTower); steps.emplace_back(psSkirtBrim);` (lines 369-370).
   `psWipeTower` regenerates tool ordering / wipe-tower / purge, and `psGCodeExport` depends on it, so
   editing the value re-slices and regenerates G-code/preview — no stale output. Even a *missed* rename
   here is not a silent failure: the trailing `else` at `Print.cpp:402-407` invalidates ALL steps for
   any unrecognized key ("for legacy, if we can't handle this option let's invalidate all steps"). So
   the worst case of a missed rename is over-invalidation (slower), never stale G-code. Claim #6 holds.

5. **Plater.cpp:16692 marks the GUI dirty.** Live line 16692 (`opt_key == "filament_minimal_purge_on_chute"`)
   is in the `else if` chain that sets `update_scheduled = true` (16696). Renaming the literal keeps the
   GUI scheduling a background update on edit. If this rename were *missed*, the key falls through to the
   chain's terminal handling; the matching `Print::invalidate_state_by_config_options` safety net (point 4)
   still forces a re-slice, so preview cannot silently go stale. Claim #4/#6 hold.

6. **GCodeProcessor / libvgcode do not reference the key.** `grep` of
   `src/libslic3r/GCode/GCodeProcessor.{cpp,hpp}` and `src/libvgcode/` returns nothing for either the
   old or new key. The emitted artifact is `flush_length` / `flush_length_N` config values consumed by
   the `change_filament_gcode` placeholder parser (GCode.cpp:984), whose tag format is unchanged. No
   re-parse hazard. Claim #5 partially (no processor coupling) holds.

7. **`set_extruder` is byte-identical to upstream — no reverted `max(1,…)` regression.**
   Fork `GCode.cpp:7909-7941` vs upstream raw `GCode.cpp:7905-7941` (fetched & diffed): identical,
   including `int flush_count = std::min(g_max_flush_count, (int)std::round(wipe_volume/g_purge_volume_one_time));`
   `float flush_unit = wipe_length / flush_count;` — **no `std::max(1,…)` guard reintroduced**. The
   min-chute enforcement lives exclusively in `append_tcr` (873-888), not in `set_extruder`. Claim #7 holds.

8. **No new-name collision / stale `.get_at` / profile / localization / 3MF reference.**
   `grep` for `minimal_chute_flush_length` across `src/` returns nothing (no collision). The old key
   appears in exactly the 8 references across the 7 files the proposal lists (PrintConfig.hpp:1456,
   PrintConfig.cpp:2722, Preset.cpp:1282, GCode.cpp:880, Print.cpp:289, Plater.cpp:16692, Tab.cpp:4151,
   Tab.cpp:4362). No matches in `resources/profiles/`, `localization/`, `tests/`, or GCodeProcessor.
   The proposed GCode.cpp:880 read drops `.get_at(new_filament_id)` for `.value` (correct for a scalar);
   no stale `.get_at` would remain. Claim #5 holds.

## Sources

- Live: `src/libslic3r/GCode.cpp` (855-988 append_tcr; 7860-7941 set_extruder; 92-94 constants;
  2028 is_BBL_Printer; hpp:257)
- Live: `src/libslic3r/PrintConfig.cpp:2722-2736`; `PrintConfig.hpp:1445-1469,1304,1483-1484,1666-1669`
- Live: `src/libslic3r/Print.cpp:270-412` (invalidation; 369-370 psWipeTower/psSkirtBrim; 402-407 catch-all)
- Live: `src/libslic3r/Preset.cpp:970,1118-1128,1279-1282,1446-1447`
- Live: `src/libslic3r/PresetBundle.cpp:67-185 (construct_full_config),322 (prints collection)`
- Live: `src/slic3r/GUI/Plater.cpp:16688-16697`
- Live: `src/slic3r/GUI/Tab.cpp:1433-1442 (toggle_option/line),2682-2686 (Flush optgroup),2829-2881 (TabPrint::toggle_options),4151,4362`
- Upstream raw (WebFetch + curl): `SoftFever/OrcaSlicer main src/libslic3r/GCode.cpp` (873 default purge_volume;
  7905-7941 set_extruder) — confirmed no chute logic, set_extruder identical
- PROPOSAL.md (this review dir)

VERDICT: No CRITICAL/HIGH silent failures — default output proven byte-identical, `.value` read is a real static member, invalidation regenerates G-code with a catch-all safety net, set_extruder unchanged; only pre-existing-and-upstream-identical divide-by-zero edge cases (LOW) and a GUI toggle-wiring care-point (LOW) remain.
