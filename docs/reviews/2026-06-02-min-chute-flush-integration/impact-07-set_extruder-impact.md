# Impact review 07 — `GCode::set_extruder` shared-path impact

Commit: `eef00f7032` — "Add minimum chute flush length filament option"
File under review: `src/libslic3r/GCode.cpp`
Lens: impact of the two added changes on EXISTING callers of the shared `set_extruder`
(non-BBL, no-tower, Type2, initial-extruder setup). `.github/` out of scope.

## What changed inside `set_extruder`

1. Min-chute clamp (GCode.cpp:7843-7850):
   ```cpp
   const float min_chute_length = (float) m_config.filament_minimal_purge_on_chute.get_at(new_filament_id);
   const float min_chute_purge  = min_chute_length * filament_area;
   if (min_chute_purge > EPSILON && wipe_volume > EPSILON) {
       wipe_volume = std::max(wipe_volume, min_chute_purge);
       wipe_length = wipe_volume / filament_area;
   }
   ```
2. flush_count floor (GCode.cpp:7934):
   ```cpp
   int flush_count = std::max(1, std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time)));
   ```
   (was `std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time))`)

Relevant constants (GCode.cpp:92-94): `g_min_purge_volume = 100`, `g_purge_volume_one_time = 135`, `g_max_flush_count = 4`.

## Callers of `set_extruder` (grep)

- `append_tcr` / `get_path_of_change_filament` (GCode.cpp:1218) — Type2 path. Calls
  `gcodegen.set_extruder(...)`. Reaches both changed lines.
- No-wipe-tower toolchange (GCode.cpp:5228) — `set_extruder(extruder_id, print_z)`. Reaches both.
- Initial-extruder setup at print_z 0 (GCode.cpp:3235) — `set_extruder(initial_extruder_id, 0.)`.
- Sequential per-object prime (GCode.cpp:3319) — `set_extruder(initial_extruder_id, initial_layer_print_height, true)`.

All four funnel through the same body. The single-extruder early return (7706-7737) and the
`!need_toolchange` early return (7702-7703) never reach the changed code, so single-extruder
machines and no-op tool "changes" are completely unaffected.

## Findings

### [Critical] GCode.cpp:7934 — `flush_count = std::max(1, …)` emits a chute flush where the default config previously emitted none

Interaction: This is the key regression. The change is NOT gated by the new option; it fires for
every caller in every config.

Trace of the 0→1 transition for a user who has NOT set `filament_minimal_purge_on_chute` (default 0,
so the clamp at 7847 is fully inert — verified below):

- Old: `flush_count = round(wipe_volume / 135)`. This is `0` whenever `wipe_volume / 135 < 0.5`,
  i.e. `wipe_volume < 67.5` mm³. With `flush_count == 0` the first emit loop (7937-7941) ran zero
  iterations and the back-fill loop (7943-7947) set `flush_length_1..4 = 0`. The downstream
  change_filament_gcode guards each segment with `{if flush_length_1 > 1}`
  (verified in `resources/profiles/BBL/machine/fdm_bbl_3dp_001_common.json` and
  `resources/profiles/Qidi/machine/Qidi *.json`), so **no flush G-code was emitted**.
  (The `flush_unit = wipe_length / 0` computed inf/nan but was never consumed because the first
  loop body was skipped — the "latent divide-by-zero" in the commit message was harmless.)
- New: `flush_count = max(1, 0) = 1`. The first loop now sets
  `flush_length_1 = flush_unit = wipe_length / 1 = wipe_length`. When `wipe_length > 1` mm
  (i.e. `wipe_volume > filament_area`, ≈ 2.4 mm³ for 1.75 mm filament) the `{if flush_length_1 > 1}`
  guard now passes and a **brand-new purge/flush block is emitted into the tool change**.

Net regression window (option unset): any caller where `wipe_volume ∈ (~2.4, 67.5) mm³`.
In that band the same model + same profile + same OrcaSlicer settings now produces extra
extrusion in the change_filament_gcode that older builds did not. The plain `flush_length` token
(7932) is not referenced by the stock profiles, so only `flush_length_1` matters here.

Why it matters: the no-wipe-tower toolchange path (5228) and the Type2 `append_tcr` path (1218)
routinely produce small `wipe_volume` after grab-length subtraction (7829,
`wipe_volume = max(0, wipe_volume - grab_purge_volume)`) and flush-multiplier scaling. A small but
nonzero residual lands squarely in the regression band. Existing BBL/Qidi users who never touched
the new option get changed G-code (extra material flushed, longer tool change). For a BBL user this
defeats the very "divert purge into infill" optimization the feature is supposed to preserve.

Severity rationale: silent G-code change in the DEFAULT configuration for existing users on the
shared path → Critical per the brief ("regression in default config").

Fix options (pick one):
- Gate the floor on the option, mirroring the clamp's own intent:
  ```cpp
  int min_segments = (min_chute_purge > EPSILON && wipe_volume > EPSILON) ? 1 : 0;
  int flush_count  = std::max(min_segments,
                              std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time)));
  ```
  This keeps byte-identical output when the option is 0 and only forces a segment once the user has
  asked for a minimum chute purge.
- Or, if the only goal was to kill the nan, guard the division instead of forcing a segment:
  `float flush_unit = flush_count > 0 ? wipe_length / flush_count : 0.f;` and leave
  `flush_count = std::min(...)` unchanged. This preserves the original (no-flush) output for
  small-volume default users.

The same `std::max(1, …)` was also added at GCode.cpp:969 in `append_tcr`; the identical reasoning
applies there and it should get the same gated treatment (covered by the `append_tcr` impact review,
flagged here because it shares the regression mechanism).

### [Low] GCode.cpp:7843-7850 — min-chute clamp is correctly inert at the default (option = 0)

Interaction: confirmation, not a defect. With `filament_minimal_purge_on_chute` defaulting to
`ConfigOptionFloats { 0. }` (PrintConfig.cpp), `min_chute_length = 0` → `min_chute_purge = 0` →
the gate `min_chute_purge > EPSILON` is false → `wipe_volume` and `wipe_length` are untouched.
For every existing user the clamp block is a no-op and the path is byte-identical up to line 7850.
No other added line in `set_extruder` alters behavior at option = 0. No fix needed.

### [Low] GCode.cpp:7845 — `get_at(new_filament_id)` on the new option is bounds- and crash-safe in every caller

Interaction: the brief asks whether `new_filament_id` is a valid index and whether a non-BBL/Type2
user who sets the option via import/inheritance is handled sanely.

- `ConfigOptionVector::get_at` (Config.hpp:624-628) is bounds-safe: out-of-range index falls back to
  `values.front()`, with only an `assert(!values.empty())`. The option's default vector has one
  element, so it is never empty. No crash.
- The same line block already calls `get_at(new_filament_id)` on `filament_diameter` (7794) and
  `filament_max_volumetric_speed` (7851), so `new_filament_id` is an established valid filament index
  in all four callers, including the two initial-setup callers (3235, 3319) where it is
  `initial_extruder_id` / `tool_ordering.first_extruder()` (guarded `!= (unsigned)-1` at 3293).
- If a value arrives on a non-BBL printer via import/inheritance (the GUI hides the control via
  `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` at Tab.cpp), the clamp still
  applies sanely: it only ever raises a genuine `wipe_volume > EPSILON` up to `min_chute_purge`. The
  resulting flush is rendered only if the printer's change_filament_gcode references `flush_length_N`;
  printers without those tokens ignore it. No crash, no malformed G-code. No fix needed.

## Verdict

One Critical regression: the `flush_count = std::max(1, …)` floor at GCode.cpp:7934 (and its twin at
:969) changes emitted tool-change G-code for existing users in the default configuration whenever
`wipe_volume` lands in the ~2.4–67.5 mm³ band — it is not gated by the new option. The min-chute
clamp itself (7843-7850) is correctly inert at the default and crash-safe across all callers; the
problem is solely the ungated flush_count floor. Recommend gating the floor on the option (or
guarding the division instead) so default-config output stays byte-identical.
