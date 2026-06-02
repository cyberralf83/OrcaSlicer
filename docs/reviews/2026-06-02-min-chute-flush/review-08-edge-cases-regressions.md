# Review 08 — Edge Cases & Regression Risk

Feature: per-filament `filament_minimal_purge_on_chute` (mm, default 0 = off). Clamp lives in
`GCode.cpp::WipeTowerIntegration::append_tcr` (~873-886); floors the purge volume, converting
mm → mm³ via `filament_area`; `flush_count` now guarded with `max(1, …)` (~969).

Scope of this lens: backward compatibility (default 0), index bounds at the clamp, divide-by-zero,
printer-variant coverage (BBL/non-BBL/Type1/Type2/wipe-tower-disabled), and feature interactions
(flush-into-infill, contact/soluble interface, priming, merged TCRs, very large values).

Files inspected: `src/libslic3r/GCode.cpp` (append_tcr 712-1003, append_tcr2 1130+, set_extruder
7800-7939, tool_change 1509-1572, prime 1497-1507), `src/libslic3r/GCode/WipeTower.{hpp,cpp}`
(ToolChangeResult, construct_tcr, merge_tcr), `src/libslic3r/Config.hpp` (get_at), `src/libslic3r/
Print.{hpp,cpp}` (wipe_tower_type, invalidation), `src/libslic3r/PrintConfig.cpp` (defs/validation),
`src/libslic3r/Preset.cpp`, `src/slic3r/GUI/{Tab,Plater,ConfigManipulation}.cpp`, BBL profiles.

---

## Verdict

No Critical or High findings. The default-config G-code is provably byte-identical to before, the
index path is safe, there is no new divide-by-zero, and the feature reaches its intended target
(BBL printers are force-routed to the modified Type1 / `append_tcr` path). Only Low-severity
documentation / UX accuracy notes remain.

---

## Backward compatibility (default 0) — VERIFIED IDENTICAL

Walked the clamp with `filament_minimal_purge_on_chute = 0`:

- `min_chute_length = get_at(id) = 0` → `min_chute_purge = 0 * filament_area = 0`.
- Nonzero branch (`tcr.purge_volume >= EPSILON`):
  `std::max({purge, g_min_purge_volume(100), min_chute_purge(0)})` == `std::max(purge, 100)` ==
  the old `std::max(tcr.purge_volume, g_min_purge_volume)`. Identical.
- Zero branch (`tcr.purge_volume < EPSILON`):
  `(is_real_toolchange && min_chute_purge > EPSILON) ? … : 0.f` → second operand is false
  (`0 > 1e-4` is false), so result is `0.f`. Identical to the old literal `0`.

`flush_count` change (`max(1, min(4, round(purge/135)))`): the `max(1, …)` only differs from the
old expression when the old result was 0, i.e. when `purge_volume < ~67.5`. In the nonzero branch
purge is always floored to ≥100, so old count was already ≥1 there — unaffected. The only case the
guard changes is the **zero branch** (`purge_volume == 0`), where old `flush_count = 0` and new
`flush_count = 1`. But in that case `purge_length = 0`, so the single new segment is
`flush_unit = 0/1 = 0`, and `flush_length_1` is set to `0.f` — exactly the value the old code wrote
into `flush_length_1` via the trailing zero-fill loop (978-982). `flush_count` itself is **not** a
placeholder (only `flush_length`, `flush_length_1..4`, `first/second_flush_volume` are emitted, all
derived from `purge_length`), so no template can observe the count difference. Net emitted G-code in
default config is byte-identical. Confirmed not a regression.

## new_filament_id bounds at the clamp — SAFE

- `append_tcr` guard (714) only throws when `new_filament_id != -1 && != tcr.new_tool`; it lets
  `new_filament_id == -1` through.
- `ConfigOptionVector::get_at(size_t i)` (Config.hpp:624) does
  `(i < values.size()) ? values[i] : values.front()`. `int(-1)` converts to `SIZE_MAX`, which is
  `>= size`, so it returns `values.front()` — no UB, no throw, no OOB for a non-empty vector.
- The new `get_at(new_filament_id)` call therefore behaves exactly like the pre-existing
  `filament_diameter.get_at(new_filament_id)` on the very next line (and the many other existing
  `get_at(new_filament_id)` calls in this block). It adds no new bounds risk.
- Empty-vector case is only an `assert` (no-op in release) → OOB `front()`. Pre-existing and shared
  by all sibling `get_at` calls; not introduced or worsened here.

## filament_diameter 0 / tiny → divide-by-zero — NOT REACHABLE / NOT WORSENED

`PrintConfig.cpp:10291` rejects any `filament_diameter < 1` at config validation, so
`filament_area >= π/4 ≈ 0.785` always. `purge_length = purge_volume / filament_area` (pre-existing)
cannot divide by zero, and the new `min_chute_purge = min_chute_length * filament_area` cannot
produce Inf. No new risk.

## Printer variants — feature reaches the intended target; inert elsewhere by design

- **`Print::wipe_tower_type()` (Print.hpp:1072)** = `is_BBL_printer() ? Type1 : m_config…`. BBL is
  **always forced to Type1**, and Type1 is the path that uses the modified `append_tcr`. So the
  feature works for the intended hardware. (The system default `wipe_tower_type` is `Type2`, and BBL
  profiles do not set it — but the runtime override makes that irrelevant for BBL.)
- **Type2 path (`append_tcr2`, 1130+)** uses `gcodegen.set_extruder(...)` and has **no**
  purge/flush/`change_filament_gcode` block of its own here, and the real flush math for Type2 lives
  in `set_extruder` (7800-7939) which does **not** consult `filament_minimal_purge_on_chute`. So on
  non-BBL Type2 setups the option silently does nothing. This is acceptable and consistent with the
  UI: `Tab.cpp::toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` only enables it
  for BBL, and the tooltip says it is BBL/chute-only. No misbehavior, just inert where advertised.
- **Wipe tower disabled / no tool changes**: `append_tcr` is only invoked from
  `WipeTowerIntegration::tool_change`, which only runs with a wipe tower present. No effect, correct.
- **Priming**: `prime()` (1497) only calls `append_tcr2` and only for Type2; on BBL/Type1 priming
  never reaches the modified `append_tcr`. The comment's claim about "priming … keep the original
  behaviour" is therefore vacuously true for the target hardware (see Low-2).

## Interactions — sound

- **Merged TCRs (`merge_tcr`, WipeTower.cpp:2660)**: `out.is_tool_change`, `out.initial_tool`
  (=first.initial_tool), `out.new_tool` (=second.new_tool) and `out.purge_volume`
  (=first+second) are all set consistently. `is_real_toolchange = is_tool_change && initial!=new`
  correctly fires when a finish-layer is merged with a genuine change, and correctly does **not**
  fire for a same-tool merge. Good.
- **`is_contact` / soluble interface**: the chute floor applies regardless of `is_contact`. For a
  genuine interface tool change (initial != new) padding the chute poop is reasonable; no conflict
  with the interface temperature logic that runs in the same block.
- **flush_into_infill**: `filament_minimal_purge_on_wipe_tower` governs the infill-diversion volume
  in Print.cpp (3452/3459); the new chute option is applied later, only at the final G-code emission
  for the chute poop. Complementary, not conflicting.
- **`tcr.initial_tool` / `tcr.new_tool` are uninitialized `int` in the struct** (WipeTower.hpp:100-103,
  no default member init). The new comparison reads them, but it is short-circuited behind
  `tcr.is_tool_change`, and every producer that sets `is_tool_change=true` also sets both fields
  (construct_tcr 1250-1262, merge_tcr 2684-2698). So the read is always well-defined. No UB.

## Very large user values — no overflow, bounded segments

`min_chute_length = 1000 mm` → `min_chute_purge ≈ 2405 mm³` (1.75 mm filament). `flush_count` is
capped by `g_max_flush_count = 4`, so the volume is spread over at most 4 flush segments
(`flush_unit = purge_length / flush_count`). No integer/float overflow; floats handle this range
trivially. The waste is large but that is explicit user intent. No clamp needed for safety; see
Low-3 for an optional usability note.

---

## Findings

### Low-1 — UX: adjacent "minimal purge" options use different units (mm³ vs mm)
- File: `src/libslic3r/PrintConfig.cpp:2717` (sibling, `mm³`) vs `:2731` (new, `mm`); placed
  adjacent in the same optgroup at `src/slic3r/GUI/Tab.cpp:4150-4151`.
- Issue: `filament_minimal_purge_on_wipe_tower` is a **volume** (mm³) and the new
  `filament_minimal_purge_on_chute` is a **length** (mm). Two adjacent rows labeled
  "Minimal purge on wipe tower" / "Minimal purge on chute" with different units may confuse users.
- Why: minor; both sidetexts are correct for their respective math, and the new option genuinely is
  a filament length consumed by the `flush_length_*` placeholders.
- Fix (optional): mention "(filament length)" in the chute label/tooltip, or express the chute
  option in mm³ for symmetry with the wipe-tower sibling.

### Low-2 — Comment inaccurate about "priming" on the target path
- File: `src/libslic3r/GCode.cpp:876-878`.
- Issue: the comment says priming results "keep the original behaviour so we don't emit spurious
  purge," implying priming flows through this clamp. On BBL/Type1 (the only hardware that reaches
  `append_tcr`), priming is handled exclusively by `append_tcr2`/Type2 (`prime()`, line 1500), so a
  priming TCR never reaches this code at all.
- Why: documentation-only; no runtime effect. The guard is still correct.
- Fix: drop "priming" from the comment, or note that priming reaches this clamp only on the Type2
  `append_tcr2` path (which this change does not touch).

### Low-3 — No upper bound / sanity hint on the option (optional)
- File: `src/libslic3r/PrintConfig.cpp:2722-2734` (`def->min = 0`, no `max`).
- Issue: very large values silently produce large chute waste, distributed over at most
  `g_max_flush_count = 4` segments. No correctness problem, but a stray large value (e.g. unit
  confusion with the mm³ sibling) wastes filament with no guardrail.
- Why: low impact; matches the unbounded sibling option's convention.
- Fix (optional): add a soft `def->max` (e.g. a few hundred mm) or a tooltip note on typical range
  (a few mm to ~tens of mm) to reduce foot-guns.

---

## Pre-existing issues noted (NOT introduced by this change; informational)

- `set_extruder` (GCode.cpp:7926-7927): `flush_count = min(4, round(wipe_volume/135))` with
  `flush_unit = wipe_length / flush_count` — if `wipe_volume < ~67.5`, `flush_count = 0` and
  `wipe_length / 0` yields inf/nan. The author added a `max(1, …)` guard in `append_tcr` (the stated
  rationale) but did **not** mirror it here. This is the Type2/`append_tcr2` path and is pre-existing
  behavior; out of scope for this feature, but worth a follow-up if Type2 purges are ever observed
  to be small.
- Empty-vector `get_at` is an `assert`-only guard (Config.hpp:626); release builds would OOB-read.
  Pre-existing and shared by all `get_at(new_filament_id)` calls in `append_tcr`.
