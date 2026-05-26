# SF2 — Silent Failures (data/state/slicing side)

Reviewer scope: `WipingExtrusions::is_overriddable` gate, config invalidation, preset
serialization/load, `flush_into_objects` interaction, ByObject sequence interaction,
`Layer::id()` / `raft_layers` arithmetic.

Files reviewed:
- `src/libslic3r/GCode/ToolOrdering.cpp` (lines 1560-1594, 1617-1819)
- `src/libslic3r/GCode/ToolOrdering.hpp` (lines 47-95, 170-178)
- `src/libslic3r/PrintConfig.cpp` (lines 6899-6938, 63-84)
- `src/libslic3r/PrintConfig.hpp` (lines 1003-1007)
- `src/libslic3r/Print.cpp` (lines 280-419, 2370-2390, 3090-3102, 3190-3460)
- `src/libslic3r/PrintObject.cpp` (lines 780-820, 1002, 1419-1478, 3489-3500)
- `src/libslic3r/Preset.cpp` (lines 1100-1130, 220-276)
- `src/slic3r/GUI/ConfigManipulation.cpp` (lines 875-925)
- `docs/superpowers/specs/2026-05-21-flush-into-infill-min-layer-design.md`

---

## CRITICAL

### C1. `flush_into_objects=true` silently bypasses the new min_layer gate for infill
- **Location**: `src/libslic3r/GCode/ToolOrdering.cpp:1561-1594`
- **Issue**: `is_overriddable` runs the `if (object.config().flush_into_objects) return true;`
  early-return at line 1566-1567 **before any role discrimination**. So when both
  `flush_into_objects=true` AND `flush_into_infill=true` AND `flush_into_infill_min_layer>0`
  are configured on the same object, every extrusion (perimeter AND infill) is overridable
  on every layer; the gate at lines 1576-1591 is never reached. The user's bottom-shell
  purge protection is silently dropped.
- **Spec contradiction**: `docs/.../2026-05-21-flush-into-infill-min-layer-design.md` line
  154 explicitly promises: `"flush_into_objects = true, flush_into_infill = true,
  flush_into_infill_min_layer = 5. Perimeter purging continues on layers 1-4 (gate doesn't
  apply); infill purging starts at layer 5."` The implementation does NOT honor this — infill
  starts at layer 1.
- **Hidden errors**: silent bottom-shell colour bleed-through on transparent / light-coloured
  prints when `flush_into_objects` is also enabled (a perfectly legitimate combination — both
  are advertised as orthogonal in the tooltips).
- **User impact**: User sets `flush_into_infill_min_layer=5` expecting protection. They also
  enable `flush_into_objects` (a separate, intentional choice). Bottom layers show mixed-colour
  purge inside infill regions. No warning, no log entry, no UI hint that the value was
  effectively ignored.
- **Recommendation**: Either (a) tighten the role guard inside the `flush_into_objects`
  branch so infill-role entities still flow through the gate, or (b) explicitly document the
  interaction in the tooltip ("`flush_into_objects` overrides this gate") and acknowledge the
  spec deviation. Option (a) is the correctness-preserving fix:
  ```cpp
  if (object.config().flush_into_objects && eec.role() != erInternalInfill)
      return true;  // perimeters always overridable; infill falls through to gate
  ```

### C2. `m_layer_tools` re-pointing relies on a side-effect of every `wiping_extrusions()` call
- **Location**: `src/libslic3r/GCode/ToolOrdering.hpp:170-173` (getter) and
  `ToolOrdering.cpp:1541,1552,1563,1578,1581,1619,1658,1767,1773` (every read of
  `m_layer_tools`).
- **Issue**: `WipingExtrusions::m_layer_tools` is a raw pointer set lazily inside the
  `LayerTools::wiping_extrusions()` accessor — every single read of `m_wiping_extrusions`
  goes through that accessor, which silently re-points `m_layer_tools = this`. If any
  future refactor caches the `WipingExtrusions&` reference (e.g. `auto& we =
  layer_tools.wiping_extrusions();` once per layer, then iterates), or if a different code
  path constructs a `WipingExtrusions` directly without going through `wiping_extrusions()`,
  `m_layer_tools` will be `nullptr` and the new gate's `m_layer_tools->print_z` (line 1578)
  will UB-segfault. The gate added by this commit increases the surface area that relies on
  this fragile lazy-init invariant.
- **Hidden errors**: null-pointer dereference at line 1578 (`m_layer_tools->print_z`) if the
  invariant breaks. No assert, no log.
- **User impact**: a crash for which the stack trace would point at the new gate code, not
  at the calling pattern that violated the invariant — a debugging nightmare.
- **Recommendation**: Add `assert(m_layer_tools != nullptr);` at the top of `is_overriddable`
  (and ideally at the top of every other `WipingExtrusions` method that dereferences it).
  Better yet, fold `set_layer_tools_ptr` into the `WipingExtrusions` constructor and store a
  reference.

---

## HIGH

### H1. `flush_into_objects` is missing from `Print::invalidate_state_by_config_options`
- **Location**: `src/libslic3r/Print.cpp:340-345` includes `flush_into_infill`,
  `flush_into_infill_min_layer`, `flush_into_support` but NOT `flush_into_objects`.
- **Issue**: The spec acknowledges this on line 96: `"flush_into_objects is intentionally
  absent from Print.cpp:341 — pre-existing asymmetry that we do not fix here."` But this is a
  pre-existing silent failure that the new feature exposes. If a user toggles
  `flush_into_objects` on/off at the **print preset** level (not per-object), wipe tower /
  G-code state will not be re-evaluated. Stale wipe-tower geometry will be used.
- **Hidden errors**: stale wipe tower data, wrong tool-change purges.
- **User impact**: enabling `flush_into_objects` in the print preset and re-slicing produces
  output identical to the previous slice. User assumes the option is broken or sees only the
  per-object override take effect.
- **Recommendation**: Add `flush_into_objects` to the list at Print.cpp:341 — the spec
  acknowledges the asymmetry but doesn't justify it. The PrintObject.cpp:1422 path is for
  per-object overrides; the Print.cpp path is for print-preset-level changes.

### H2. Negative / corrupted `flush_into_infill_min_layer` silently treated as "disabled"
- **Location**: `src/libslic3r/GCode/ToolOrdering.cpp:1577` — `if (min_layer > 0)`.
- **Issue**: A negative value (loaded from a corrupted 3MF, a manually edited config, or
  forward-compat substitution from a future Orca version) silently maps to "disabled" (gate
  not enforced). The design spec line 129 acknowledges this as intentional: `"Treated as
  disabled by the min_layer > 0 short-circuit."` But there is no log entry indicating the
  value was out of range.
- **Hidden errors**: User exports a project file from a friend's older Orca where the value
  got corrupted, opens it on a new Orca, the gate they expect is silently absent.
- **User impact**: surprising behaviour with no diagnostic.
- **Recommendation**: When `min_layer < 0`, log `BOOST_LOG_TRIVIAL(warning)` once
  per-PrintObject. The existing `min=0` in PrintConfig.cpp:6920 should clamp the GUI but
  doesn't catch all paths (raw 3MF, CLI `--load`).

### H3. `slicing_parameters().raft_layers()` reads without checking `m_slicing_params.valid`
- **Location**: `src/libslic3r/GCode/ToolOrdering.cpp:1581` calls
  `object.slicing_parameters().raft_layers()`.
- **Issue**: `PrintObject::slicing_parameters() const` (Print.hpp:419) returns `m_slicing_params`
  directly with no validity check. The flag `m_slicing_params.valid` exists and is reset to
  false on multiple invalidation paths (PrintObject.cpp:1455, 1459, 1476). If `_make_wipe_tower`
  runs while `m_slicing_params.valid == false` (e.g. a race during background slicing
  cancellation, or a future code path that doesn't go through `update_slicing_parameters()`
  first), `raft_layers()` returns stale data and the gate is computed against the wrong
  baseline. Silent miscount.
- **Hidden errors**: gate offsets by ±N layers if a raft was added/removed and the print is
  re-evaluated through an unusual path.
- **User impact**: hard-to-reproduce off-by-N gating; user reports "sometimes the bottom
  shell still shows colour bleed."
- **Recommendation**: Add an assert at line 1581: `assert(object.slicing_parameters_valid())`
  (introducing a getter if needed). Or fail closed and warn if `!valid`.

### H4. `raft_layers > this_layer->id()` log message is one-shot per call, not deduplicated
- **Location**: `src/libslic3r/GCode/ToolOrdering.cpp:1583-1586`.
- **Issue**: The "impossible under normal flow" warning fires for every extrusion entity
  on every offending layer of every object instance. In a large multi-object plate this can
  emit thousands of identical warning lines. The warning text is correct; the volume is
  not. Worse, the function denies the override but does NOT propagate the invariant
  violation up — the slice continues silently producing wipe-tower-only purge for those
  layers. The user gets no UI indication that something is structurally wrong.
- **Hidden errors**: real bugs in `Layer::id()` / `raft_layers()` consistency stay buried in
  log spam; user sees only "the wipe tower is using more material than expected."
- **User impact**: silent slice degradation; debug logs flooded.
- **Recommendation**: Deduplicate (one warning per print, e.g. via a `std::once_flag` on
  `WipingExtrusions` or `ToolOrdering`). Consider surfacing as a `print->active_step_add_warning`
  rather than a debug log so the user actually sees it.

---

## MEDIUM

### M1. `print_sequence == ByObject` UI gate vs. slicer reality
- **Location**: `src/slic3r/GUI/ConfigManipulation.cpp:917-921` (UI gate) vs.
  `src/libslic3r/Print.cpp:3090-3102` (`has_wipe_tower()`).
- **Issue**: The UI hides `flush_into_infill_min_layer` when `print_sequence == ByObject`,
  on the assumption (spec line 136) that "the wipe tower is disabled in this mode (Print.cpp:1443
  `extruders.size() > 1` check)." That check is **inside `#if 0`** at Print.cpp:1441-1465 —
  it is dead code. `has_wipe_tower()` does NOT consult `print_sequence`. So in ByObject mode
  with `enable_prime_tower=true` and multiple filaments, the wipe tower IS built (via
  `_make_wipe_tower`), `ToolOrdering(*this, ...)` IS constructed with non-null
  `m_print_config_ptr`, and the gate IS active. A user toggling print_sequence in the UI
  cannot see or edit the value, but the slicer still honours whatever the value was set to
  previously. Silent persistence.
- **Hidden errors**: a user sets `flush_into_infill_min_layer=20` in ByLayer mode, then
  switches to ByObject. UI hides the field; user assumes it no longer applies. Wipe tower
  still gates infill purging at layer 20.
- **User impact**: confusing behaviour that contradicts the UI affordance.
- **Recommendation**: Either (a) make `has_wipe_tower()` honour ByObject + multi-extruder
  by returning false, restoring the spec's claimed invariant, or (b) keep the slicer
  honouring the value and remove the misleading UI gate.

### M2. Layer-id<raft "fails closed" — but might be silently masking a real invariant break
- **Location**: `src/libslic3r/GCode/ToolOrdering.cpp:1582-1587`.
- **Issue**: If `Layer::id() < raft_layers` ever occurs, the gate denies the override and
  the slice proceeds. If this triggers because a downstream refactor changes the
  layer-numbering convention (e.g. raft layers stop being included in the id offset), the
  gate's "fail closed" behaviour silently switches `flush_into_infill_min_layer` from
  "skip first N layers" to "skip almost all layers" — the print succeeds but the user
  loses most of their planned infill purging without any user-visible signal.
- **Hidden errors**: silent slicer regression after future refactor; bad output that
  still prints.
- **User impact**: smaller, harder-to-diagnose problem than a crash.
- **Recommendation**: Already partially addressed by H4 (deduped warning). Additionally,
  consider an explicit `print->active_step_add_warning(PrintStateBase::WarningLevel::HIGH, ...)`
  so the user sees a visible warning in the slicer UI, not just in the log.

### M3. Old-3MF backward compatibility: key missing → silent default-to-0
- **Location**: `src/libslic3r/Preset.cpp:1127` (preset save list); `src/libslic3r/Config.cpp:573`
  (`set_deserialize_nothrow` adds missing keys to `unrecogized_keys`).
- **Issue**: Spec line 141 says: `"Old 3MF without the key: Loads as registered default
  (0 = disabled) via set_deserialize. Behaviour-preserving."` This is correct (the key is
  registered with `set_default_value(new ConfigOptionInt(0))`), but no UI substitution-warning
  is shown — the user has no way to know their project loaded with a default for a new
  feature. Similarly, a new 3MF opened in old Orca silently drops the key (spec line 142) —
  acknowledged but no migration helper. Pre-existing pattern, but the new option inherits it.
- **Hidden errors**: cross-version project sharing produces silently different output.
- **User impact**: shipping projects between team members on different Orca versions causes
  silent behaviour drift.
- **Recommendation**: Out of scope to fix in this commit; document explicitly in changelog
  / release notes that 3MFs from build X+ contain this key.

### M4. `filament_extruder_override_keys` correctly excludes the key — verify intent
- **Location**: `src/libslic3r/PrintConfig.cpp:63-84`.
- **Issue**: `flush_into_infill_min_layer` is a per-object setting (PrintObjectConfig), not a
  per-filament setting, so it correctly does NOT belong in `filament_extruder_override_keys`.
  But the list is also referenced in `extend_default_config_length` (Preset.cpp:253) which
  performs nil-to-default rewriting for vector-valued options. Since
  `flush_into_infill_min_layer` is `coInt` (scalar), this is fine. Worth a one-line code
  comment confirming the intentional omission so future maintainers don't reflexively add it.
- **Hidden errors**: none in current code.
- **Recommendation**: Add inline comment near the option definition: "Per-object
  scalar — not in filament_extruder_override_keys (filament-side vector overrides only)."

---

## LOW

### L1. PrintObject.cpp:1424-1425 invalidation chain is correct but could be more explicit
- `flush_into_infill_min_layer` correctly triggers `psWipeTower` + `psGCodeExport`
  invalidation. The new key was added alongside the existing three. No silent failure.

### L2. The gate's `EPSILON` tolerance matches existing callers
- `object.get_layer_at_printz(m_layer_tools->print_z, EPSILON)` at line 1578 uses the same
  EPSILON value as the existing callers at 1658 and 1773. Consistent.

### L3. Preset save list at Preset.cpp:1127 is exhaustive for the new key
- The `s_Preset_print_options` list (which is used for both save AND the canonical "all
  print-preset keys" enumeration) correctly includes `flush_into_infill_min_layer`. No
  silent drop on round-trip save/load.

---

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 2 |
| HIGH     | 4 |
| MEDIUM   | 4 |
| LOW      | 3 |
| Total    | 13 |

**Top silent-failure concern**: **C1 — `flush_into_objects=true` silently bypasses the
new gate for infill role**, directly contradicting documented spec test case 6. A user
who enables both `flush_into_objects` and `flush_into_infill_min_layer` (a legitimate
combination — both UI options remain editable side-by-side) gets exactly the bottom-shell
colour bleed they configured the new option to prevent, with no warning, no log entry, no
UI indication that their setting was overridden. The fix is a one-line role check
(`&& eec.role() != erInternalInfill`) in the `flush_into_objects` branch.
