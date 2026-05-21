# Silent-failure hunter #1

Agent ID: ae7f9801f59d04347

## Findings

### Critical
- **C1: Stale `LayerTools::extruders` after only-this-key change.** `psWipeTower` invalidation should rebuild ToolOrdering — but assert that `something_overridable` is reset between slices and that no earlier consumer caches override decisions. **Fix:** verify `_make_wipe_tower` always rebuilds ToolOrdering from scratch; add assert.
  File: `src/libslic3r/PrintObject.cpp:1420-1424`

### High
- **H1: Silent nullptr from `get_layer_at_printz`** silently denies override → volume silently goes to prime tower. **Fix:** `BOOST_LOG_TRIVIAL(debug)` line, consider fallback to `get_first_layer_bellow_printz`.
- **H2: Multi-region with mismatched `bottom_shell_layers`** — same as R3 M1. Warn at `Print::validate()` time when `min_layer < max(bottom_shell_layers)` across regions.
- **H3: Negative `object_local_idx` silently denied.** User has no signal that layer renumbering occurred (PrintObjectSlice.cpp:772). **Fix:** explicit early-return + `BOOST_LOG_TRIVIAL(error)` + debug assert.

### Medium
- **M1: Value 0 as "disabled" — UX overload.** Tooltip must explicitly say "0 = disabled (no restriction)."
- **M2: Old-3MF preset upgrade silently defaults to 0.** Behavior-preserving so OK, but document in tooltip.
- **M3: Toggle leaves stale value when parent disabled.** Disabling `flush_into_infill` greys the field; re-enabling silently restores prior threshold. **Fix:** consider resetting to 0 or surfacing a "restored prior value" hint.
- **M4: Non-zero residual `volume_to_wipe`** at line 1729. The new gate increases residual; narrower prime towers may overflow without warning. **Fix:** one-shot `BOOST_LOG_TRIVIAL(warning)` when residual > tower capacity.

### Low
- **L1: G-code preview parity** — libvgcode reads produced G-code, so preview is correct by construction. No issue.
- **L2: `ConfigOptionInt::value` default** is 0 by virtue of static-init; behavior-preserving for unset profile keys.
