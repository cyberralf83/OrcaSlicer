# Review 01 — `filament_minimal_purge_on_chute` rescope, clamp-logic correctness

- **Commit reviewed:** `4673720c01` — "Scope min chute flush to BBL append_tcr; revert set_extruder changes"
- **Scope:** clamp logic correctness only (`GCode.cpp` `append_tcr` ~873–887 and `flush_count` ~969; `set_extruder` revert ~7842/7926). `.github/` excluded per instructions.
- **Files changed by commit:** `src/libslic3r/GCode.cpp` only (9 insertions, 17 deletions).
- **Verdict:** No issues found. The rescope is correct, default behaviour is value-identical to upstream, no unused-variable/dangling-local, no harmful div-by-zero.

---

## Findings

No issues at any severity (Critical / High / Medium / Low) in the rescope clamp logic.

The items below document the verification performed for each item in the review charter.

### V1 — `apply_chute_min` predicate is correct (real toolchange AND option set AND BBL)

`GCode.cpp:882-883`

```cpp
const bool  is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool;
const bool  apply_chute_min    = is_real_toolchange && min_chute_purge > EPSILON && gcodegen.is_BBL_Printer();
```

- `is_real_toolchange` — true only for a genuine colour change (a flagged tool change whose initial and new tool differ). Finish-layer / priming / same-tool results fail this and are excluded. Correct.
- `min_chute_purge > EPSILON` — the "option is set" test. `min_chute_purge = min_chute_length * filament_area`; at the default option value 0 this is 0, which is `< EPSILON` (1e-4, `libslic3r.h:52`), so the floor never engages at default. Any meaningful nonzero option (mm of filament × area ≈ 2.4 mm² for 1.75 mm) is far above 1e-4. The `float > double` comparison is a harmless implicit promotion. Correct.
- `gcodegen.is_BBL_Printer()` — restricts the floor to chute printers, exactly mirroring the BBL-only GUI exposure (`Tab.cpp:4362` `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)`). This removes the prior GUI-vs-execution mismatch. Correct.

### V2 — Both ternary branches walked

`GCode.cpp:884-887`

```cpp
float purge_volume = (tcr.purge_volume < EPSILON)
    ? (apply_chute_min ? std::max(min_chute_purge, g_min_purge_volume) : 0.f)
    : (apply_chute_min ? std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})
                       : std::max(tcr.purge_volume, g_min_purge_volume));
```

| Case | `apply_chute_min` | `tcr.purge_volume < EPSILON` | Result | Notes |
|------|-------------------|------------------------------|--------|-------|
| Default (option 0) | false (purge ≤ 0) | yes | `0.f` | = upstream `0` |
| Default (option 0) | false | no | `std::max(tcr.purge_volume, g_min_purge_volume)` | = upstream |
| BBL real TC, option set, fully absorbed | true | yes | `std::max(min_chute_purge, 100)` ≥ 100 | floor applied |
| BBL real TC, option set, nonzero purge | true | no | `std::max({purge, 100, min_chute_purge})` ≥ 100 | floor applied |
| Non-BBL, option set | false (BBL check fails) | either | `0.f` or `std::max(purge, 100)` | inert — identical to upstream |
| Finish-layer / non-toolchange | false (`is_real_toolchange` false) | either | `0.f` or `std::max(purge, 100)` | inert — identical to upstream |

All inert branches reduce to the upstream expression `tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume)`.

### V3 — Byte-identical at default

Upstream (`6a26284ba6:GCode.cpp`):

```cpp
float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);
```

At default the option contributes `min_chute_purge == 0`, forcing `apply_chute_min == false`. The ternary then evaluates to `0.f` (true branch) or `std::max(tcr.purge_volume, g_min_purge_volume)` (false branch) — the same value upstream computes (upstream's literal `0` int promotes to `0.f` into the `float`). `purge_volume`, `purge_length`, every downstream `config.set_key_value(...)`, and the resulting `flush_*` values are therefore value-identical. The `flush_count` line was reverted to upstream `std::min(...)` (see V5), so the emitted G-code at default is identical to upstream.

### V4 — No unused-variable warning, no dangling local

`is_real_toolchange` is declared at `GCode.cpp:882` and consumed at `GCode.cpp:883` inside `apply_chute_min` (only those two references exist; verified by grep — 2 hits total). It is read, so no `-Wunused-variable`. `apply_chute_min` is itself consumed at lines 885 and 886. The reverted `set_extruder` (`GCode.cpp:7842`) computes `wipe_length` straight from `wipe_volume` with no leftover `min_chute_length` / `min_chute_purge` locals — the removed block left no dangling reference. Clean compile expected.

### V5 — `flush_count` revert: no harmful div-by-zero

`GCode.cpp:969-970` (and mirror `7926-7927` in `set_extruder`), both reverted to upstream:

```cpp
int   flush_count = std::min(g_max_flush_count, (int) std::round(purge_volume / g_purge_volume_one_time));
float flush_unit  = purge_length / flush_count;
```

Constants (`GCode.cpp:92-94`): `g_min_purge_volume = 100`, `g_purge_volume_one_time = 135`, `g_max_flush_count = 4`.

From V2, when a poop is emitted `purge_volume` is either `0.f` or `>= 100.f`:

- `purge_volume == 0` → `flush_count = min(4, round(0)) = 0`. `flush_unit = purge_length / 0` is **float** division (`purge_length` float, `flush_count` int) → IEEE NaN, **not** a SIGFPE. The emit loop `for (; flush_idx < flush_count; ...)` with `flush_count == 0` never executes, so the NaN `flush_unit` is never read; the second loop fills all 4 slots with `0.f`. Harmless — matches upstream behaviour exactly.
- `purge_volume >= 100` → `flush_count = min(4, round(100/135 = 0.74) = 1) = 1` (≥ 1). No integer-zero divisor; `flush_unit` is finite.

Therefore there is no integer divide-by-zero (which would be UB/crash); the only zero divisor case is a benign float NaN that is never consumed. The earlier `std::max(1, ...)` form (removed here) was what changed default G-code by forcing a flush block on sub-67 mm³ purges; its removal restores upstream output.

### V6 — `m_curr_print` guaranteed set during `append_tcr`

`is_BBL_Printer()` (`GCode.cpp:2028-2033`) returns `false` when `m_curr_print` is null. `m_curr_print` is assigned at the very top of `GCode::do_export` (`GCode.cpp:2047`), the single entry point of the G-code generation pipeline; `WipeTowerIntegration::append_tcr` (`GCode.cpp:712`) runs deep inside the per-layer processing invoked from `do_export`. Thus `m_curr_print` is non-null whenever `append_tcr` runs, and `is_BBL_Printer()` cannot spuriously return false in a valid slicing context. The feature will not be wrongly disabled. (`Print::is_BBL_printer()` is a plain accessor of `m_isBBLPrinter`, `Print.hpp:1070-1071`.)

### V7 — Type2 / non-wipe-tower path fully inert

The `set_extruder` chute clamp was removed entirely (diff lines 7840-7848 deleted) and `append_tcr2` (`GCode.cpp:1130`, the Type2 emitter) was confirmed to contain no `min_chute` / `flush_count`-floor residue. Since BBL printers are always Type1 (`Print.hpp:1072` `wipe_tower_type() { return is_BBL_printer() ? Type1 : ...; }`), the feature is correctly confined to the single reachable BBL path and is inert elsewhere.

---

## Summary

The rescope is logically correct and conservative. At the default option value every reviewed expression collapses to the upstream form, the `flush_count` lines are restored verbatim, and the only feature-active path is gated on a genuine BBL colour change with the option set — matching the BBL-only GUI exposure. No compile hazard, no dangling local, no harmful divide-by-zero.
