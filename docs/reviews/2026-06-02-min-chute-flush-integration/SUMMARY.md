# Min-chute-flush INTEGRATION review — consolidated findings (2026-06-02)

10 agents reviewed how committed `eef00f7032` impacts/connects to existing code (8 integration lenses + 2 silent-failure). Raw output: `impact-01..08`, `silent-01..02`. Count = agents raising it.

## Headline
The BBL/Type1 path (`append_tcr`) — the one the user actually uses — is sound: estimates re-parse the emitted flush, wipe-tower geometry is unaffected, config/preset/GUI plumbing is correct, backward-compat holds. **The problems cluster on the `set_extruder` mirror added in the previous review round**, which is dead-for-BBL (BBL is always Type1→append_tcr) yet changes default behavior and creates a GUI-vs-execution mismatch.

## Consolidated issues

| # | Severity | Count | Issue | Location | Disposition |
|---|----------|-------|-------|----------|-------------|
| 1 | Critical/Med | 3 (A7,A10,A1) | `set_extruder` `flush_count = std::max(1,…)` is **ungated** → at default (option=0) it now emits a flush block for small purges (~2.4–67.5 mm³) that were previously suppressed (`{if flush_length_1>1}`). Default-config behavior change for existing no-tower/Type2 (incl. non-BBL) users. Twin at append_tcr:969 is benign (purge there is always 0 or ≥100). | GCode.cpp:7934 (and 969) | FIX |
| 2 | High | 2 (A10) + LOW (A3,A5,A7) | Brand-agnostic clamp vs **BBL-only GUI**: option can silently act on a non-BBL printer (value imported/inherited) or silently no-op on Type2; the `set_extruder` mirror is dead-for-BBL and the main source of this. | GCode.cpp:7843; Tab.cpp:4362 | FIX (scope to BBL/append_tcr) |
| 3 | Medium | 2 (A4,A10) | `is_real_toolchange` gates only the `<EPSILON` branch; the `≥EPSILON` branch pads finish-layer/merged TCRs too when the option is set — contradicts the code comment. (Default 0 = identical to upstream, so not a regression.) | GCode.cpp:882-885 | FIX (apply gate in both branches) |
| 4 | Medium | 2 (A4,A9) | `set_extruder` floor skipped when `wipe_volume` is zeroed by `grab_purge_volume`/diversion (`wipe_volume>EPSILON` gate) — asymmetric with Type1. | GCode.cpp:7847 | Moot if #1/#2 remove the set_extruder clamp |
| 5 | Low | 3 (A2,A4,A9) | `ToolOrdering` `filament_flush_weight` from un-floored matrix — but used only as a single-vs-multi **delta** (cancels); absolute stats come from the floored GCodeProcessor re-parse. Do NOT add a second floor here (double-count). | ToolOrdering.cpp:140 | Won't fix |
| 6 | Low | 1 (A6) | Redundant `Plater::update()` refresh (chute doesn't touch the scene). | Plater.cpp:16692 | Optional |
| 7 | Low | 2 (A3,A8) | Wipe-tower preview/pre-slice estimate excludes the chute floor — by design (chute ≠ tower geometry); final stats reflect it. | WipeTower2.cpp:2195 | Won't fix |
| 8 | Low | 1 (A1) | H2D/X2D firmware-flush (`M620.10 L[]`, G1 flush commented out) isn't counted by GCodeProcessor — pre-existing template design, only magnified. | BBL H2D profile | Won't fix |

## Confirmed clean (multiple agents)
- **Estimates/stats consistent** (A2,A9): weight/cost/time all derive from GCodeProcessor re-parsing the emitted (floored) FLUSH_START/END moves; no reported-vs-actual divergence on absolute numbers.
- **Wipe-tower geometry unaffected** (A3): chute poop (`tcr.purge_volume`) is architecturally separate from tower depth (`prime_volume`).
- **Config/preset/normalize correct** (A5,A8): whitelist edit is load-bearing — it resizes the per-filament vector so `get_at` never falls back to `front()`; old projects load at default 0 → identical slice.
- **GUI plumbing correct** (A6); **calibration/CLI/3MF unaffected** (A8).

## Plan
1. **Verify** (read-only): (a) the `set_extruder` `flush_count` change really alters default-config G-code; (b) Type2 chute purge flows through `set_extruder` (so removing the mirror = no Type2 support, acceptable since GUI is BBL-only & BBL=Type1); (c) exact revert restores upstream; (d) whether `is_BBL_printer()` is cleanly reachable in `append_tcr` to gate the clamp.
2. **Fix** (one agent): remove the `set_extruder` chute clamp + revert its `flush_count` (fixes #1,#2,#4); apply `is_real_toolchange` to both branches in `append_tcr` (#3); keep append_tcr's benign `flush_count` guard; optionally gate the append_tcr clamp on BBL.

## Resolution (applied + verified)
Verification (read-only agent) CONFIRMED: the `set_extruder` `flush_count` change altered default-config G-code (its nan "fix" was harmless); Type2 routes through `set_extruder` and BBL is always Type1→`append_tcr`, so removing the `set_extruder` clamp loses nothing for BBL; `gcodegen.is_BBL_Printer()` is cleanly reachable in `append_tcr`; the `is_real_toolchange` gate is default-safe.

The feature was **rescoped to the BBL `append_tcr` (Type1) path only** — 4 edits, all in GCode.cpp:
- **#1, #2, #4 FIXED** — `set_extruder` chute clamp **removed** and its `flush_count = max(1,…)` **reverted to `std::min(...)`** → that path is now byte-identical to upstream; no default-config regression, no Type2 silent-no-op, no asymmetry.
- **#2 (GUI/exec mismatch) FIXED** — the `append_tcr` clamp now gates on `gcodegen.is_BBL_Printer()`, so the slicer applies the floor only for BBL printers, exactly matching the BBL-only GUI.
- **#3 FIXED** — `is_real_toolchange` now gates BOTH branches (via `apply_chute_min`), so finish-layer/merged TCRs are never padded.
- `append_tcr` `flush_count` also reverted to `std::min(...)` (purge there is always 0 or ≥100 mm³, so it's never needed).

Net: at default (option=0) the entire change is byte-identical to upstream; the floor activates only for a real color change, on a BBL printer, with the option set. LOW items (#5–#8: ToolOrdering delta, redundant Plater refresh, preview-excludes-chute, H2D firmware-flush) left as-is — informational / by-design / pre-existing.

Status: fixes applied to working tree, **not compiled** (CI's job).
