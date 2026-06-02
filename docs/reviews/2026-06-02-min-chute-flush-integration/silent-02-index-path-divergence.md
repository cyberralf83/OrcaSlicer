# Silent-failure hunt 02 — index validity & path divergence

Commit under review: `eef00f7032` — "Add minimum chute flush length filament option"
Feature: per-filament `filament_minimal_purge_on_chute` (mm, default 0).
Scope: silent wrong-value / wrong-path failures from how this change wires into
existing code. `.github/` ignored.

Reads added:
- `GCode.cpp:880` — `append_tcr` (Type1 wipe-tower path),
  `full_config.filament_minimal_purge_on_chute.get_at(new_filament_id)`
- `GCode.cpp:7845` — `set_extruder` (no-wipe-tower / Type2-emitter path),
  `m_config.filament_minimal_purge_on_chute.get_at(new_filament_id)`

`get_at(i)` semantics (`Config.hpp:624-628`): returns `values[i]` if
`i < size()`, else **silently** returns `values.front()` (no error, no log).

---

## Finding 1 — Type2 wipe tower silently ignores the value (path divergence)

- **Severity:** High
- **Location:** dispatch `GCode.cpp:1500-1540` (`tool_change`) and `1497-1507`
  (`prime`) route Type2 through `append_tcr2`; clamp lives only in `append_tcr`
  (`GCode.cpp:883-885`) and `set_extruder` (`GCode.cpp:7847-7850`). Type selector
  `Print.hpp:1072`, `GCode.cpp:2035-2040`. Default `wipe_tower_type` = **Type2**
  (`PrintConfig.cpp:5997`).
- **Masked failure:** A user sets `filament_minimal_purge_on_chute > 0`, the slice
  succeeds, but on any printer whose effective wipe-tower type is **Type2** the
  value has **zero effect** — `append_tcr2` never reads
  `filament_minimal_purge_on_chute` and never floors the purge. No warning, no log,
  no UI cue. The poop the user was trying to enlarge stays exactly as small as
  before.
- **Why:** The established sibling option `filament_minimal_purge_on_wipe_tower` is
  enforced on **both** tower implementations — Type1 via the flush matrix
  (`Print.cpp:3452/3459`) and Type2 via `WipeTower2.cpp:2198` /
  `WipeTower2.cpp:2302`. The new option is enforced on only one branch
  (`append_tcr`) plus the no-tower `set_extruder`. `append_tcr2` / `WipeTower2`
  got no equivalent. BBL printers are pinned to Type1 (`Print.hpp:1072`) so BBL is
  covered, but the clamp is brand-agnostic while its only emitter is Type1-only —
  so the value silently applies for Type1 and silently vanishes for Type2.
- **Fix:** Either (a) mirror the floor into `WipeTower2`'s purge-volume computation
  (alongside `filament_minimal_purge_on_wipe_tower` at `WipeTower2.cpp:2198/2302`)
  and/or `append_tcr2`, so the option behaves identically across tower types; or
  (b) if the feature is genuinely chute-only and chute purge is a Type1/BBL concept,
  gate the option's *visibility and applicability* explicitly and document that it
  is a no-op under Type2 — but a silent no-op is the defect, so a real fix is (a).

---

## Finding 2 — Value set under BBL silently "follows" the filament to non-BBL printers (GUI/exec mismatch)

- **Severity:** High
- **Location:** UI gate `Tab.cpp:4360-4362` (`toggle_option(..., is_BBL_printer)`);
  `toggle_option` body `Tab.cpp:1433-1440` (only enables/disables the widget — does
  **not** clear the stored value); clamp is brand-agnostic at `GCode.cpp:883-885`
  and `GCode.cpp:7847-7850` (no `is_BBL_printer` check).
- **Masked failure:** The option is editable only when a BBL printer is selected,
  but the value is stored in the **filament** preset, not the printer. Switch that
  filament preset onto a non-BBL printer (or import a 3MF / filament preset authored
  on BBL) and the value is still present in the assembled config. The execution-side
  clamp never checks brand, so:
  - non-BBL **Type1** tower or **no** wipe tower → the floor **silently takes
    effect** on a machine where the user could never see or edit the setting;
  - non-BBL **Type2** tower (the default) → the floor **silently does nothing**
    (Finding 1).
  Same stored number, opposite outcomes, zero UI feedback either way.
- **Why:** Visibility (`is_BBL_printer`) and applicability (brand-agnostic clamp on
  a Type1-only emitter) are decided by two independent conditions that don't agree.
  Hiding a field does not neutralize its stored value.
- **Fix:** Make the execution gate match the UI gate — apply the chute floor only
  when the printer is the kind the UI exposed it for (BBL / chute-equipped), or make
  it tower-type-correct per Finding 1. If the value is meant to be universal, then
  show the field universally instead of `is_BBL_printer`. The visibility predicate
  and the apply predicate must be the same predicate.

---

## Finding 3 — `min_chute_purge` floor applied to non-tool-change TCRs, contradicting the stated intent

- **Severity:** Medium
- **Location:** `GCode.cpp:882-885`. Comment (lines 876-878) states "Only real
  filament changes are padded; finish-layer / priming / same-tool results keep the
  original behaviour."
- **Masked failure:** The `is_real_toolchange` guard is consulted **only** in the
  `tcr.purge_volume < EPSILON` branch (line 884). In the `tcr.purge_volume >= EPSILON`
  branch (line 885) the floor is unconditional:
  `std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})`. A TCR that is
  *not* a real tool change but still carries `purge_volume >= EPSILON` — e.g. a
  finish-layer block (`construct_block_tcr`, `WipeTower.cpp:1271-1290`, sets
  `is_tool_change=false` yet `purge_volume = purge_volume`) or a merged TCR
  (`merge_tcr`, `WipeTower.cpp:2663-2705`, `out.purge_volume += second.purge_volume`)
  — will get its purge **silently inflated** to `min_chute_purge` when the option is
  set, even though the comment promises such cases keep original behaviour. Effect:
  extra/larger flush emitted at finish-layer or same-tool boundaries the author
  believed were excluded.
- **Why:** The ternary structure gates only the zero branch. The non-zero branch was
  meant to keep `min_chute_purge` for real tool changes but inadvertently applies it
  to every `purge_volume >= EPSILON` TCR regardless of `is_real_toolchange`.
- **Fix:** Fold `is_real_toolchange` into the non-zero branch too, e.g.
  `float floor = (is_real_toolchange ? min_chute_purge : 0.f);`
  `purge_volume = (tcr.purge_volume < EPSILON) ? ((is_real_toolchange && floor > EPSILON) ? std::max(floor, g_min_purge_volume) : 0.f) : std::max({tcr.purge_volume, g_min_purge_volume, floor});`
  so non-tool-change TCRs truly keep original behaviour. (Only observable when the
  option is non-zero; harmless at default 0.)

---

## Finding 4 — `flush_count = std::max(1, …)` changes behaviour at default 0 on the no-tower path

- **Severity:** Medium
- **Location:** `GCode.cpp:7934-7947` (`set_extruder`). Contrast `GCode.cpp:969`
  (`append_tcr`) where the same `std::max(1, …)` is a no-op because purge there is
  always floored to `g_min_purge_volume` = 100 (line 884/885).
- **Masked failure:** This `std::max(1, …)` is **not** gated by the new option, so it
  affects every existing user including those at default 0 / non-BBL. In
  `set_extruder`, `wipe_volume` comes straight from the flush matrix (lines
  7815-7829) and is **not** floored to `g_min_purge_volume`. A small flush volume in
  the open interval `(0, ~67.5)` mm³ rounds to 0:
  - **Old:** `flush_count = 0` → first loop (7937) doesn't run → every
    `flush_length_1..N` placeholder is set to **0** by the second loop (7943-7947).
  - **New:** `flush_count = 1` → `flush_length_1 = flush_unit = wipe_length / 1 =
    wipe_length` (the small but non-zero purge).
  So a `change_filament_gcode` template that consumes the per-segment
  `flush_length_N` placeholders now emits a real flush where it previously emitted
  zero — a silent behaviour change for existing no-wipe-tower multi-extruder users at
  default settings. (The aggregate `flush_length` placeholder at 7932 is unchanged;
  the divergence is in the split placeholders.)
- **Why:** The commit message frames this as fixing "a latent divide-by-zero," but
  in the old code `flush_count == 0` made `flush_unit` (= inf) **unused** because the
  emitting loop never ran, so there was no observable NaN/inf — only all-zero split
  placeholders. Forcing `>= 1` is defensible as a fix, but it is not behaviour-neutral
  at default 0 as the "default-0 is silently identical" expectation assumes.
- **Fix:** Acknowledge this as an intentional behaviour change in the commit message,
  or preserve the old split distribution for `wipe_volume < g_purge_volume_one_time`
  by keeping `flush_count` at the rounded value and guarding the division
  (`flush_unit = flush_count ? wipe_length / flush_count : 0.f`) so existing
  small-flush templates are unaffected, while still removing the div-by-zero risk.

---

## Index-validity checks (cleared — no new silent fallback introduced)

These were specifically hunted and found **safe**:

- **`get_at(new_filament_id)` index dimension.** `new_filament_id` is the filament
  (tool) index, the exact dimension used by every co-located read in both blocks
  (`filament_diameter`, `nozzle_temperature`, `retraction_length`,
  `filament_max_volumetric_speed`, all `.get_at(new_filament_id)`). If the index were
  ever out of range, those pre-existing reads would silently `front()` too — the new
  read inherits the same invariant and is no worse. Not a new defect.
- **Initial-extruder `set_extruder(initial_extruder_id, 0.)` at `GCode.cpp:3235`.**
  Reaches the `else` branch (`GCode.cpp:7835-7841`) with `wipe_volume = 0.f` because
  `m_writer.filament()==nullptr && m_start_gcode_filament==-1`; the clamp at 7847
  requires `wipe_volume > EPSILON`, so it is skipped. No spurious priming purge.
- **By-object reprime `set_extruder(initial_extruder_id, …, true)` at
  `GCode.cpp:3319`.** `wipe_volume` is real flush-matrix volume here; clamp applies
  only when `wipe_volume > EPSILON`, i.e. genuine tool changes — consistent with the
  feature intent. Index is `initial_extruder_id` = a valid filament id.
- **Vector sizing / front()-fallback in normal slicing.** Because the Preset.cpp
  whitelist change adds `filament_minimal_purge_on_chute` to
  `s_Preset_filament_options`, the resize loop at `Preset.cpp:462-476` pads the
  vector to the filament count `n` using the **default (0)** — not slot-0's value.
  So the vector is correctly sized at slice time, `get_at` never falls back to
  `front()`, and there is no cross-filament value bleed. The whitelist inclusion is
  correct and load-bearing; omitting it would have caused exactly the silent
  front()-fallback we were hunting for. **The commit got this right.**
- **Legacy 3MF / preset without the key.** Absent key default-constructs to `{0.}`
  then resizes to `n` with default 0 → all-zero → identical to pre-feature behaviour.
  Default-0 protects the legacy-load path.

## Default-0 invariant summary

- `append_tcr` line 884 with `min_chute_purge==0`: `min_chute_purge > EPSILON` false
  → `0.f`. Identical to original `tcr.purge_volume < EPSILON ? 0`. ✓
- `append_tcr` line 885 with `min_chute_purge==0`:
  `std::max({tcr.purge_volume, g_min_purge_volume, 0})` ==
  `std::max(tcr.purge_volume, g_min_purge_volume)`. Identical. ✓
- `set_extruder` 7847 with `min_chute_purge==0`: guarded off. Identical. ✓
- `flush_count = std::max(1, …)`: **NOT identical** on the `set_extruder` path for
  `wipe_volume ∈ (0, ~67.5)` even at default 0 — see Finding 4. In `append_tcr` it
  is a no-op (purge floored to 100). 
