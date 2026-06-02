# Silent-failure audit — `filament_minimal_purge_on_chute` rescope (commit 4673720c01)

Lens: silent failures only. Scope per request; `.github/` excluded.
Reviewed against the rescoped tree (HEAD) and the intermediate committed state `eef00f7032`.

Summary verdict: **No silent failures introduced by the rescope.** The two design-intended
"silently inert" gates (non-BBL printer, non-real-toolchange) are genuinely matched by the GUI
visibility gate and derive from the same source of truth, so they are documented/expected
inertness rather than masked failures. One pre-existing upstream latent issue is noted for
completeness but is explicitly NOT a regression of this change.

---

## F1 — `is_BBL_Printer()` null-`m_curr_print` path cannot trigger in any real export path

- Severity: **Low** (informational — no silent failure in practice)
- Location: `src/libslic3r/GCode.cpp:2028-2033` (`is_BBL_Printer`), gate use at `:883`,
  `m_curr_print` set at `:2047` (`do_export`), declared `:640` of `GCode.hpp` (default `nullptr`).
- Masked failure mode (hypothetical): if `append_tcr` could run with `m_curr_print == nullptr`,
  `is_BBL_Printer()` returns `false`, `apply_chute_min` is forced `false`, and the floor is
  silently disabled for a BBL user with no warning.
- Why it does NOT fire:
  - `m_curr_print` is assigned exactly once, at the very top of `GCode::do_export`
    (`GCode.cpp:2047`, `m_curr_print = print;`), before `this->_do_export(...)` is invoked
    (`:2106`). `append_tcr` runs deep inside `_do_export` → `process_layers` →
    `WipeTowerIntegration::append_tcr` (`:712`, `:1555`/`:1565`), all strictly after the
    assignment.
  - `m_curr_print` is never reset to `nullptr` anywhere (grep confirms the only assignment is
    `:2047`). The only external caller of `do_export` is `Print::export_gcode`
    (`Print.cpp:2617`, `gcode.do_export(this, ...)`), which passes a non-null `Print*`.
  - There is no alternate entry into `_do_export`/`process_layers` that bypasses `do_export`
    (`_do_export` is private, called only from `:2106`).
  - The same `is_BBL_Printer()` helper is already relied on across `_do_export`
    (`:2210, :4574, :5078, :5101, :5332, :5473`) with no null guard, i.e. the codebase already
    treats `m_curr_print` as established by the time layer processing runs.
- Conclusion: the `return false` fallback in `is_BBL_Printer()` is unreachable from `append_tcr`
  during a real slice/export, so it cannot silently disable the feature for a BBL user.
- Fix: none required. Optional hardening only — if defensiveness is desired, add a debug
  `assert(m_curr_print)` at the top of `append_tcr`'s BBL block so any future refactor that
  moves the `m_curr_print` assignment is caught in debug builds rather than silently degrading
  the floor. Not necessary for correctness today.

## F2 — GUI visibility gate and execution gate MATCH (BBL-only); persisted preset value on a non-BBL printer is intended inertness

- Severity: **Low** (by design; no mismatch found)
- Locations:
  - GUI gate: `src/slic3r/GUI/Tab.cpp:4358-4360`
    `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer);`
    where `is_BBL_printer = wxGetApp().preset_bundle->is_bbl_vendor()` (`Tab.cpp:4271`).
  - Execution gate (GUI slice): `src/slic3r/GUI/BackgroundSlicingProcess.cpp:199` and `:683`
    `m_fff_print->is_BBL_printer() = preset_bundle.is_bbl_vendor();`
  - Execution gate (CLI): `src/OrcaSlicer.cpp:5999`
    `(dynamic_cast<Print*>(print))->is_BBL_printer() = is_bbl_vendor_preset;` (derived from the
    `Bambu Lab` printer-model prefix at `:5989/:5994/:5996`).
  - Consumed at `GCode.cpp:883` via `gcodegen.is_BBL_Printer()` →
    `Print::is_BBL_printer()` (`Print.hpp:1070-1071`, backed by `m_isBBLPrinter`).
- Analysis:
  - Both the visibility gate and the execution gate are derived from the *same* BBL-vendor
    determination (`is_bbl_vendor()` in GUI; the `Bambu Lab` model prefix in CLI, which is the
    CLI's equivalent of the same fact). There is no observed state where the option is
    **visible+editable** but silently does nothing, nor **hidden** but silently acting.
  - Carried-preset scenario (explicitly requested): a BBL filament preset with a non-zero
    `filament_minimal_purge_on_chute` is dragged onto a non-BBL printer. The value DOES persist
    (it is whitelisted in `Preset.cpp` `s_Preset_filament_options`, per `eef00f7032`). On the
    non-BBL printer the UI hides the field (`toggle_option(..., is_BBL_printer==false)`) AND
    execution skips the floor (`is_BBL_Printer()==false` → `apply_chute_min==false`). So the
    persisted value is simultaneously hidden and inert — consistent, not a mismatch. The value
    re-activates if the preset is later used on a BBL printer. This is the same semantics as the
    sibling `filament_minimal_purge_on_wipe_tower` (inverse gate, `Tab.cpp:4356-4358`).
  - This inertness is silent (no toast/log telling the user "your chute setting is ignored on
    this non-BBL printer"), but it is *intended* and matches established sibling-option behavior;
    it is not a masked failure. Surfacing it would be a UX nicety, not a correctness fix.
- Fix: none required for correctness. Optional: the tooltip already states "Only effective on
  printers that eject purge through a chute ... (e.g. Bambu Lab)", which is the appropriate and
  sufficient disclosure.

## F3 — `get_at(new_filament_id)` remains bounds-safe in `append_tcr` post-rescope

- Severity: **Low** (no issue)
- Locations: `GCode.cpp:880` (`filament_minimal_purge_on_chute.get_at(new_filament_id)`),
  `:879` (`filament_diameter.get_at`), `:892` (`filament_max_volumetric_speed.get_at`);
  implementation `Config.hpp:624-628`.
- Analysis: `ConfigOptionVector::get_at` clamps any out-of-range index to `values.front()`
  (`return (i < size()) ? values[i] : values.front();`). The only UB hazard is an *empty* vector
  (`values.front()` on empty + a debug `assert(!values.empty())`). But:
  - `filament_minimal_purge_on_chute` has a non-empty default (`{0.}`, `PrintConfig.cpp`), is
    whitelisted, and is invalidation-tracked (`Print.cpp`), so it is materialized like the other
    per-filament floats.
  - `new_filament_id` is the identical index used by every neighbouring `get_at` in this block
    (`filament_diameter`, `filament_max_volumetric_speed`, `retraction_length`, …). The chute
    option introduces no new index and no new empty-vector risk beyond what already governs the
    surrounding upstream code.
- Conclusion: no new bounds/silent-read hazard. Fix: none.

## F4 — Reverting the `set_extruder` clamp is byte-identical to upstream; not a silent behavior change for shipped users

- Severity: **Low** (noted, expected)
- Locations: removed block formerly at `GCode.cpp` `set_extruder` (~`:7840`); reverted
  `flush_count = std::min(...)` at `:7926` and at `append_tcr` `:969`.
- Analysis:
  - Diff vs the upstream merge base `2de405b0b8` confirms both `flush_count` lines are now
    byte-identical to upstream (`std::min(g_max_flush_count, (int)std::round(... / 135))`),
    and the `set_extruder` chute mirror is fully removed. The Type2/no-wipe-tower path therefore
    matches upstream exactly again.
  - The only state that ever observed the intermediate `set_extruder` clamp / `max(1, ...)`
    guard was commit `eef00f7032`, which per the project notes is unpushed/unshipped. So the
    revert is not a silent behavior change for any released build — there is no installed base
    relying on the intermediate behavior. (Flagged per the brief; benign.)
- Fix: none.

## F5 — Latent `flush_count == 0` float division is PRE-EXISTING upstream, NOT reintroduced by the chute floor

- Severity: **Low** (pre-existing upstream; the rescope neither fixes nor worsens the feature's
  own active path)
- Locations: `GCode.cpp:969-970` (`append_tcr`) and `:7926-7927` (`set_extruder`):
  `int flush_count = std::min(g_max_flush_count, (int)std::round(purge_volume / 135));`
  `float flush_unit = purge_length / flush_count;`
- Analysis:
  - When `purge_volume == 0` (the `tcr.purge_volume < EPSILON` branch with `apply_chute_min`
    false), `round(0/135) == 0` → `flush_count == 0` → `flush_unit = purge_length / 0`. Both
    operands are `float`/`int`→`float`, so this is IEEE float division → `0.0f / 0 = NaN`, not a
    SIGFPE crash. The `for (; flush_idx < 0; ...)` loop body never runs, so the NaN `flush_unit`
    is never written into any `flush_length_N` key; the trailing loop fills all slots with `0.f`.
    Output is effectively unaffected. This is exactly upstream's long-standing behavior.
  - Critically, the chute feature's *active* path can NOT drive `flush_count` to 0: whenever
    `apply_chute_min` is true, `purge_volume = std::max(min_chute_purge, g_min_purge_volume)`
    (zero branch) or `std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})`
    (non-zero branch), so `purge_volume >= g_min_purge_volume == 100`, and
    `round(100/135) == round(0.74) == 1`, i.e. `flush_count >= 1`. The intermediate commit's
    `max(1, ...)` guard was therefore unnecessary for the feature itself and its removal does not
    expose the feature to division by zero.
  - The masked-failure framing ("user sets a bigger poop but gets nothing") does NOT apply: on
    every path where the user's value matters (`apply_chute_min` true), `flush_count >= 1` and
    `purge_volume >= max(user_value, 100)`. The floor can only raise, never lower, the purge.
- Why it is still worth a line: the `0.0/0 = NaN` is genuinely a latent silent oddity in
  upstream. But it is out of scope to "fix" here — doing so would re-diverge from upstream, which
  the fork charter explicitly forbids (keep the diff minimal; byte-identical at default). Leaving
  it identical to upstream is the correct call.
- Fix: none in this fork. (If ever upstreamed, guard with `flush_count = std::max(1, ...)` or
  skip the division when `flush_count == 0`.)

## F6 — Intended inertness on same-tool / non-real toolchange has no diagnostic (by design)

- Severity: **Low**
- Location: `GCode.cpp:882-887` (`is_real_toolchange` / `apply_chute_min` gate).
- Masked behavior: a BBL user who sets `filament_minimal_purge_on_chute` will see no effect on
  results that are not "real" tool changes — priming, finish-layer, or a planned change where
  `tcr.initial_tool == tcr.new_tool`. There is no log/toast explaining the no-op.
- Why it is acceptable: this mirrors the long-standing `purge_volume` semantics (the original
  code already special-cased `tcr.purge_volume < EPSILON`). The gate is conservative and
  documented in the in-code comment and the option tooltip ("A built-in minimum of about 40 mm
  (100 mm³) already applies, so values below that have little effect"). It avoids emitting
  spurious purge on non-changes — the safer failure direction.
- Fix: none required. The existing comment + tooltip are adequate disclosure for an advanced
  (`comAdvanced`) option.

---

## Files reviewed
- `src/libslic3r/GCode.cpp` — `append_tcr` (`:712`, `:856-983`), `is_BBL_Printer` (`:2028`),
  `do_export` / `m_curr_print` (`:2042-2106`), `set_extruder` (`:7900-7940`).
- `src/libslic3r/GCode.hpp` — `m_curr_print` declaration (`:640`).
- `src/libslic3r/Print.hpp` — `is_BBL_printer()` / `m_isBBLPrinter` (`:1070-1072`, `:1143`).
- `src/libslic3r/Config.hpp` — `ConfigOptionVector::get_at` (`:624`).
- `src/slic3r/GUI/Tab.cpp` — `TabFilament::toggle_options` gate (`:4269-4360`).
- `src/slic3r/GUI/BackgroundSlicingProcess.cpp` — execution BBL flag (`:199`, `:683`).
- `src/OrcaSlicer.cpp` — CLI BBL flag (`:5999`).
- `git show 2de405b0b8` (upstream merge base) — confirmed `flush_count` lines byte-identical.
