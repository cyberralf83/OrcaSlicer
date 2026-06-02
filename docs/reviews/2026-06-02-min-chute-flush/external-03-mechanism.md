# External Review 03 — Mechanism cross-check vs. UPSTREAM SoftFever/OrcaSlicer (`main`)

**Date:** 2026-06-02
**Scope:** Verify that the assumptions our min-chute-flush clamp relies on still exist verbatim in upstream, so the patch is correct and the edits apply cleanly on the next nightly merge.
**Method:** Fetched upstream `main` raw sources (commit history checked through 2026-06-02). All upstream line numbers below are from `main` at review time.

**Verdict: NO DRIFT. Every assumption the patch depends on is present and verbatim in upstream. All five insertion anchors match byte-for-byte. No conflicting upstream change. Merge-safe.**

---

## 1. Constants — MATCH (verbatim)

Upstream `src/libslic3r/GCode.cpp:92-94`:

```cpp
static const float g_min_purge_volume = 100.f;
static const float g_purge_volume_one_time = 135.f;
static const int g_max_flush_count = 4;
```

All three constants exist with the exact values our patch assumes (100, 135, 4). Same file, same `namespace Slic3r` scope. No rename, no value change.

---

## 2. append_tcr / append_tcr2 / set_extruder structure — MATCH

### 2a. `WipeTowerIntegration::append_tcr` (the BBL / Type1 chute path) — primary clamp site

Upstream `GCode.cpp` (method starts at line 712). Insertion-point lines, verbatim:

- **L873** (purge_volume floor — our primary edit replaces this line):
  ```cpp
  float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);
  ```
- **L874** `float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_filament_id), 2));`
- **L875** `float purge_length = purge_volume / filament_area;`
- **L956** (flush_count — our second edit):
  ```cpp
  int   flush_count = std::min(g_max_flush_count, (int) std::round(purge_volume / g_purge_volume_one_time));
  ```
- **L957** `float flush_unit  = purge_length / flush_count;`
- Flush loop L959-L969 emits `flush_length_%d` for 1..flush_count, then zero-fills 2..4. (Unchanged by us; consumes our enlarged `purge_length`.)
- **L920-921** also set `first_flush_volume` / `second_flush_volume` = `purge_length / 2.f` — derived from the same `purge_length`, so they scale with our floor automatically.
- **L949** `config.set_key_value("flush_length", new ConfigOptionFloat(purge_length));` — main `flush_length` placeholder, also driven by our enlarged `purge_length`.

Our diff’s context (it removes the original L873, keeps `filament_area`, inserts the new `purge_volume` block before `purge_length`, then edits the L956 `flush_count`) **matches the upstream layout exactly**. Both hunks will apply cleanly.

`tcr` fields the clamp reads are present on `WipeTower::ToolChangeResult` (`src/libslic3r/GCode/WipeTower.hpp`):
- `bool is_tool_change{false};` (L85)
- `int initial_tool;` (L100)
- `int new_tool;` (L103)
- `float purge_volume = 0.f;` (L97)

So `tcr.is_tool_change && tcr.initial_tool != tcr.new_tool` compiles and is semantically correct (both `int`).

### 2b. `WipeTowerIntegration::append_tcr2` (Type2 path) — correctly NOT patched

Upstream method at `GCode.cpp:1117`. Confirmed it contains **no** `purge_volume` / `flush_count` / `g_min_purge_volume` / `g_purge_volume_one_time` logic — it only sets `change_filament_gcode` and emits `[change_filament_gcode]`. The chute-flush placeholder mechanism is generated **only** in `append_tcr` and `set_extruder`. Since BBL printers are forced to Type1 (see §3), they never reach `append_tcr2`, so leaving it unpatched is correct.

Dispatch confirmed at `GCode.cpp:1487 / 1503 / 1592`: `if (wipe_tower_type() == WipeTowerType::Type2) … append_tcr2(...)` else fall through to `append_tcr(...)` (L1542, L1552).

### 2c. `GCode::set_extruder` (no-wipe-tower path) — mirror clamp site

Upstream `GCode.cpp`:
- **L7818** `wipe_volume = std::max(0.f, wipe_volume-grab_purge_volume);` — raw flush-matrix volume (NOT pre-floored to `g_min_purge_volume`).
- **L7828** in the else branch (no real change): `wipe_volume = 0.f;`
- **L7831** (our mirror inserts right after this line):
  ```cpp
  float wipe_length = wipe_volume / filament_area;
  ```
- **L7913** `dyn_config.set_key_value("flush_length", new ConfigOptionFloat(wipe_length));`
- **L7862-7863** `first_flush_volume` / `second_flush_volume` = `wipe_length / 2.f`.
- **L7915** (our third edit):
  ```cpp
  int flush_count = std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time));
  ```
- Flush loop L7918-7928 mirrors the append_tcr loop.

Our mirror block inserts after L7831 and guards on `wipe_volume > EPSILON`, which correctly skips the L7828 zero case (finish-layer / no-tool-change). Anchor matches verbatim. Applies cleanly.

> **Bonus — our `std::max(1, …)` also fixes a latent upstream div-by-zero.** At L7916 / L957 `flush_unit = wipe_length / flush_count`; when `wipe_volume` is in (0, ~67) `flush_count` rounds to 0 and upstream divides by zero (the result `flush_unit` is then unused because the loop body doesn’t run, so it’s harmless in practice but UB). Our `std::max(1, …)` removes the zero divisor entirely. Minor, but worth keeping in mind: it is a behavioural divergence from upstream for tiny purges — intentional and beneficial.

---

## 3. `Print::wipe_tower_type()` forces BBL → Type1 — MATCH (verbatim)

Upstream `src/libslic3r/Print.hpp:1072`:

```cpp
WipeTowerType wipe_tower_type() const { return is_BBL_printer() ? WipeTowerType::Type1 : m_config.wipe_tower_type.value; }
```

Unchanged. BBL printers are always Type1, therefore always route through `append_tcr` (our primary clamp), confirming the patch covers the BBL chute case. `GCode::wipe_tower_type()` (`GCode.cpp:2022`) delegates to this.

---

## 4. change_filament_gcode placeholders + FLUSH tags re-parse — MATCH (current)

### 4a. Tag definitions & parsing — upstream-current
`src/libslic3r/GCode/GCodeProcessor.cpp`:
```cpp
const std::string GCodeProcessor::Flush_Start_Tag = " FLUSH_START";   // L105
const std::string GCodeProcessor::Flush_End_Tag   = " FLUSH_END";     // L106
```
Parser handlers L3099-3108 set `m_flushing = true/false` around the tags, so all `G1 E…` extrusion emitted between them is accounted into the flush estimate. (Also `VFLUSH_START/END` at L107-108 / L3110-3117 for virtual flush.) The preview/time-and-filament estimate re-parses our enlarged flush correctly — no change needed.

### 4b. The BBL template actually consumes the placeholders we enlarge
`resources/profiles/BBL/machine/fdm_bbl_3dp_001_common.json` `change_filament_gcode` uses `flush_length_1..4` (11 references each) wrapped in 5 `; FLUSH_START` / `; FLUSH_END` pairs. Representative window:

```
{if flush_length_1 > 1}
; FLUSH_START
…
{if flush_length_1 > 23.7}
G1 E23.7 F{old_filament_e_feedrate} ; do not need pulsatile flushing for start part
G1 E{(flush_length_1 - 23.7) * 0.02} F50
…
```

Two correctness notes this confirms for our patch:
- The template gates each segment on `{if flush_length_N > 1}` — so a sub-1mm segment emits nothing. Our `std::max(1, flush_count)` keeps `flush_count` ≥ 1, and the min-chute floor (≥ `g_min_purge_volume` = 100 mm³ ≈ 40 mm of 1.75 mm filament) puts each segment comfortably above both the 1 mm and 23.7 mm thresholds. So the enlarged purge genuinely extrudes.
- `first_flush_volume`/`second_flush_volume` (used by the other-format / older templates) also scale from the same `purge_length`/`wipe_length`, so they grow consistently.

---

## 5. Conflict / drift scan — NONE found (low risk)

Recent `main` commits touching `GCode.cpp` (newest first): #11761 (support outline order, Jun 2), #13868 (air filtration, May 31), #13892 (built-in placeholders, May 28), #13895 (`total_toolchanges` placeholder, May 28), #13681 (line-type preview, May 21), #13703 (travel-to-wipe-tower Type2, May 17), #11202, #13388, #13460, #13390, #12736, #13415, #12269, #11784.

- **None** touch `purge_volume`, `wipe_volume`, `flush_count`, `flush_length`, `g_min_purge_volume`, `g_purge_volume_one_time`, `append_tcr`, or `set_extruder`’s flush logic.
- #13895 (`total_toolchanges`) and #13892 (built-in placeholders) add unrelated placeholders; they don’t move or alter our anchor lines.
- #13703 (Type2 travel-to-tower) only affects the `append_tcr2`/Type2 branch, which BBL never uses.

No better insertion point suggested — the current sites (the `purge_volume`/`wipe_volume` derivation and the `flush_count` line in each emitter) are exactly where the floor must apply, and upstream’s structure is stable there.

### GUI / config anchors (also verified verbatim, for completeness)
- `Preset.cpp:1272` — `s_Preset_filament_options` line ending `… "filament_minimal_purge_on_wipe_tower",` (we append the chute key). **Whitelist gotcha respected** — the new key is added to the filament-options whitelist, so it serializes.
- `Print.cpp:288` — invalidate-by-config list, after the wipe-tower key. (Note: `filament_minimal_purge_on_wipe_tower` is also consumed at `Print.cpp:3451/3458` for the redirect-to-infill budget; our chute key is independent and correctly does not touch that path.)
- `PrintConfig.hpp:1446-1447` — insertion between `filament_minimal_purge_on_wipe_tower` and `filament_cooling_before_tower`.
- `PrintConfig.cpp:2711-2722` — the `def = this->add("filament_minimal_purge_on_wipe_tower"…)` block ends at L2720 (`set_default_value(new ConfigOptionFloats { 15. });`); our new `def` block inserts before `filament_cooling_before_tower` (L2722).
- `Tab.cpp:4141` (build line) and `Tab.cpp:4345-4348` (toggle). Upstream disables the wipe-tower minimum FOR BBL via `toggle_option(el, !is_BBL_printer)`; our `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` is the correct inverse (chute purge is a BBL-only concept). Matches upstream’s own "hide specific settings for BBL printers" intent.
- `Plater.cpp:16643-16644` — `on_config_change` reslice trigger list, after the wipe-tower key.

All seven edited files anchor on lines that exist verbatim upstream.

---

## Summary

| Assumption | Upstream status | Path |
|---|---|---|
| `g_min_purge_volume=100`, `g_purge_volume_one_time=135`, `g_max_flush_count=4` | MATCH verbatim | `GCode.cpp:92-94` |
| `append_tcr` purge_volume/flush_count layout | MATCH verbatim | `GCode.cpp:873-875, 956-957` |
| `append_tcr2` has no purge logic (correctly unpatched) | MATCH | `GCode.cpp:1117…` |
| `set_extruder` wipe_volume/wipe_length/flush_count | MATCH verbatim | `GCode.cpp:7818, 7831, 7915` |
| `tcr.is_tool_change/initial_tool/new_tool` exist (int) | MATCH | `WipeTower.hpp:85,100,103` |
| `Print::wipe_tower_type()` forces BBL→Type1 | MATCH verbatim | `Print.hpp:1072` |
| FLUSH_START/END tags + re-parse | MATCH current | `GCodeProcessor.cpp:105-106, 3099-3108` |
| BBL template consumes flush_length_1..4 in FLUSH blocks | CONFIRMED | `profiles/BBL/machine/fdm_bbl_3dp_001_common.json` |
| Conflicting recent upstream change | NONE | — |

**Severity of drift/conflict risk: NONE / negligible.** The patch is internally consistent with current upstream and will apply cleanly on the next nightly merge. The only intentional behavioural divergence (`std::max(1, flush_count)`) is benign and additionally removes a latent upstream divide-by-zero.
