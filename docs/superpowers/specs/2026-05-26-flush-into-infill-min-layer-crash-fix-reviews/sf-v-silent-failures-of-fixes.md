# SF-V — Silent Failures Introduced by the Fixes

Scope: hunt for *new* silent failures introduced by the five fixes
(ToolOrdering gate restructure, `sp.valid` check, log demotion, tooltip removal,
back-reference comment). Out of scope: re-litigating the original issues those
fixes targeted.

Repo: `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty`, branch
`nightly-builds-with-bc`. Files touched:
- `src/libslic3r/GCode/ToolOrdering.cpp` (fixes 1, 2, 3)
- `src/libslic3r/PrintConfig.cpp` (fix 4)
- `src/slic3r/GUI/GUI_Factories.cpp` (fix 5, docs only)

---

## C — Critical (new silent failures)

**None.** No fix introduces a hard, never-surfaces silent failure for a
realistic user-facing scenario.

---

## H — High (likely to bite real users)

### H1. Fix 3: log demotion silently hides a real "gate is wrong, but I gave you a number" miscount

`src/libslic3r/GCode/ToolOrdering.cpp:1592-1594` — the raft-vs-`Layer::id()`
diagnostic was previously `BOOST_LOG_TRIVIAL(warning)` and is now
`BOOST_LOG_TRIVIAL(debug)`.

The trigger condition `this_layer->id() < raft_layers` *should* be impossible
once SF2 H3 is closed by Fix 2 (because the only way it could fire was reading
a stale `m_slicing_params.raft_layers()` before slicing finalized). But the
diagnostic was kept on purpose as a tripwire — that's the whole point of
keeping a "this should never happen" log. By moving it to `debug`, we have
silently committed to the assertion that **this branch can never fire in any
release-build scenario**.

If the assumption is wrong — e.g. raft layers get re-calculated mid-flight, or
a future refactor lets `update_slicing_parameters()` set
`raft_layers` AFTER `posSlice` creates `Layer`s with stale ids — the user gets
a silently denied override on bottom layers (color bleed into the first
bottom-shell layer when they set `min_layer = 1`) and **nobody sees the warning**
because release builds default to info-or-higher Boost.Log severity.

**User impact:** future regression in slicing-step ordering produces incorrect
bottom-layer color, with no log entry, no Sentry-equivalent, no user-visible
sign. Pure silent failure.

**Recommendation:** make this a `print->active_step_add_warning(WarningLevel::NON_CRITICAL, ..., psWipeTower)`
once per print, OR keep it at `BOOST_LOG_TRIVIAL(warning)` since it's
explicitly an "impossible state detected" message and firing once per print
won't spam logs. The current `debug` level means the tripwire is functionally
disabled in production.

**Suggested replacement:**
```cpp
if (this_layer->id() < raft_layers) {
    // This should be unreachable now that we gate on sp.valid above. If it
    // ever fires, slicing-step ordering has regressed — surface it.
    BOOST_LOG_TRIVIAL(warning) << "flush_into_infill_min_layer: Layer::id() ("
        << this_layer->id() << ") < raft_layers (" << raft_layers
        << ") at print_z=" << m_layer_tools->print_z << "; denying override.";
    return false;
}
```

---

### H2. Fix 4: tooltip removal makes the ByObject silent-skip *more* invisible to the user

`src/libslic3r/PrintConfig.cpp:6914-6917` — the sentence "Has no effect when By
object print sequence is active with multiple extruders" was removed.

The UI behavior in ByObject mode is at
`src/slic3r/GUI/ConfigManipulation.cpp:917-921`:

```cpp
toggle_line("flush_into_infill_min_layer",
            !is_global_config
            && have_prime_tower
            && config->opt_bool("flush_into_infill")
            && config->opt_enum<PrintSequence>("print_sequence") != PrintSequence::ByObject);
```

The line is **hidden** when ByObject is active. A stored non-zero
`flush_into_infill_min_layer` value persists on the object and is still applied
by the gate in `ToolOrdering::is_overriddable` — but the user cannot see the
value, cannot change it, and the tooltip no longer hints that this might
happen.

The original tooltip was misleading (R7/SF2 M1) because in ByObject mode
`_make_wipe_tower()` is also skipped — `Print.cpp:2377` only initializes
`m_tool_ordering` for non-ByObject, and `mark_wiping_extrusions()` is only
called from inside `_make_wipe_tower()`. So in practice the gate value
**genuinely has no effect** in ByObject mode (the entire purge-into-infill
path is bypassed), and the tooltip line that said "no effect when ByObject is
active" was *factually correct in current upstream*, not "misleading".

By removing it, the fix substitutes one form of confusion (the wording about
"with multiple extruders") for a worse silent failure mode:

- User in non-ByObject mode sets `min_layer = 5`, slicing works.
- User switches to ByObject. The field disappears from the UI.
- User has no way to know their saved value is still attached to the object
  and whether it does anything.
- User switches back to non-ByObject months later. The value silently
  reactivates with no notification.

**User impact:** mild — the gate is genuinely a no-op in ByObject so nothing
slices wrong. But "value silently persists across mode toggles, hidden from
UI, no tooltip mentions it" is a textbook silent-failure-of-UX pattern.

**Recommendation:** restore an accurate version of the tooltip line:
"This setting is hidden and has no effect while By-object print sequence is
active; the stored value is retained and resumes effect if you switch back to
By-layer." This was the correct fix for SF2 M1, not deletion.

---

## M — Medium (real, but low blast radius)

### M1. Fix 2: `sp.valid=false` denies override silently, no log

`src/libslic3r/GCode/ToolOrdering.cpp:1587-1589`:

```cpp
const SlicingParameters& sp = object.slicing_parameters();
if (!sp.valid)
    return false; // slicing parameters not finalized — fail closed rather than gate against stale data
```

**The good:** fails closed (denying the override is the safe default — it
just forces more purge onto the wipe tower).

**The bad:** there is no log at all. If `sp.valid=false` ever fires on a
realistic codepath — *not* a race, but a misordered step — the symptom is
"wipe tower grew, bottom shell looks fine, user is happy" until they compare
gcode and notice the gate was ignored for a whole print. No log, no warning,
no breadcrumb.

Verified via `git grep`: `m_slicing_params.valid` is set false in three
places in `PrintObject.cpp` (1455, 1459, 1476) on `posSlice` invalidation,
`posSupportMaterial` invalidation, and `invalidate_all_steps`. It is set
`true` exactly once: `Slicing.cpp:228` (inside
`SlicingParameters::create_from_config`), via `update_slicing_parameters()`
which is called from `PrintApply.cpp:1766` (during `Print::apply()`).

`mark_wiping_extrusions` runs inside `_make_wipe_tower()`, which runs during
`psWipeTower` — long after `Print::apply()` has finalized all
`slicing_parameters`. So `sp.valid` should always be true when
`is_overriddable` is called. Fix 2's check is a belt-and-braces guard, not a
real codepath.

**User impact:** today, zero. If anyone ever rearranges step ordering or adds
a code path that calls `is_overriddable` outside `psWipeTower`, it silently
denies overrides forever.

**Recommendation:** at minimum log once per print:

```cpp
if (!sp.valid) {
    BOOST_LOG_TRIVIAL(warning) << "flush_into_infill_min_layer: slicing_parameters not valid for object "
        << object.id().id << " at print_z=" << m_layer_tools->print_z
        << "; denying override.";
    return false;
}
```

This is the same shape as H1 — a tripwire that should never fire, but if it
does, you want to know.

---

### M2. Fix 1: gate restructure narrowly preserves but doesn't actively *check* the upstream wipe-into-infill-only branch

`ToolOrdering.cpp:1569-1576` replaces the prior:
```cpp
if (object.config().flush_into_objects)
    return true;
if (!object.config().flush_into_infill || eec.role() != erInternalInfill)
    return false;
```
with:
```cpp
if (object.config().flush_into_objects && eec.role() != erInternalInfill)
    return true;
if (eec.role() != erInternalInfill)
    return false;
if (!object.config().flush_into_infill && !object.config().flush_into_objects)
    return false;
```

I cross-checked this against `mark_wiping_extrusions` (lines 1670-1717):
- Line 1671 (`wipe_into_infill_only = !object.config().flush_into_objects && object.config().flush_into_infill`)
  is satisfied iff `flush_into_objects=false && flush_into_infill=true`. With
  the new code, `is_overriddable(infill, ...)` returns `true` (subject to
  gate), unchanged.
- Line 1697 (perimeter purge on dedicated-purge objects, gated by
  `object->config().flush_into_objects && is_infill_first == perimeters_done`):
  the entity is a perimeter (role != erInternalInfill) on an object with
  `flush_into_objects=true`. New code at line 1569 returns `true`. Unchanged.
- Mixed case: `flush_into_objects=true && flush_into_infill=false`. Old code:
  early-returned `true` for *all* roles (including infill). New code: returns
  `true` for perimeter (line 1569), then for infill skips to line 1575 (false
  || true → proceed) and applies the gate. **Behavioral change**: infill on a
  dedicated-purge object with `flush_into_infill=false` is now overridable
  (subject to gate) where previously it was unconditionally overridable.

This **is** the intended change (SF2 C1: "the min_layer gate must apply to
infill on dedicated-purge objects"). But the silent failure surface is: any
user who *deliberately* set `flush_into_objects=true, flush_into_infill=false`
with the intent "purge into anything on this object, including infill,
without min-layer gating" will now find their infill gated. There is no
migration log, no Sentry breadcrumb, no warning.

This is exactly the kind of "we changed semantics silently" hazard. The
old behavior was arguably a bug (the tooltip on `flush_into_infill` is "purge
extruded into top/bottom layer infill"), but no one announced this fix to
users — there is no release note string, no warning on first slice, nothing.

**User impact:** very small — any user in this niche state gets *slightly*
more purge on the wipe tower than before, never less. No print quality
regression. But they have no way to know their config semantics shifted.

**Recommendation:** add a release-notes line. No code change needed; this is
the trade-off for SF2 C1 being a real bug.

---

## L — Low

### L1. Fix 5: back-reference comment claims "throws ... uncaught and crashes the app"

`src/slic3r/GUI/GUI_Factories.cpp:1089-1094`:
> "Calling ConfigOption::getBool on a non-boolean ConfigOption" inside a
> wxEVT_UPDATE_UI handler — which propagates uncaught and crashes the app on
> any right-click that builds the object menu.

I did not verify whether wxWidgets actually propagates this exception uncaught
in release builds — wxEVT_UPDATE_UI handlers are sometimes guarded by
wxWidgets' top-level try/catch in app's `OnExceptionInMainLoop`. The comment
is doing useful work (it deters reordering), but the "crashes the app"
phrasing could be slightly wrong; the actual symptom may be "logs and
silently disables the menu entry" depending on the wxWidgets exception
policy.

**User impact:** none — this is a developer-facing comment.

**Recommendation:** consider softening to "throws inside a wxEVT_UPDATE_UI
handler; symptom is at minimum a broken Flush Options submenu and at worst a
hard crash depending on wxWidgets exception handling configuration." Not
worth blocking on.

---

## Summary

The five fixes mostly hold up. Two real new silent-failure surfaces:

1. **Fix 3 (log demotion) is a step backward**: a tripwire intentionally kept
   in the code (the raft-vs-id mismatch) is now invisible in release. If the
   underlying invariant ever regresses, nobody sees it. Restore to `warning`
   or escalate to `active_step_add_warning`.
2. **Fix 4 (tooltip removal) trades one form of confusion for a quieter
   form**. The original tooltip was actually correct; deleting it leaves
   users with a UI element that silently appears/disappears with no
   documentation of what happens to the stored value.

One process gap:

3. **Fix 1 (gate restructure) silently changes the behavior of
   `flush_into_objects=true, flush_into_infill=false`** for the infill role.
   Intentional and correct per SF2 C1, but not announced anywhere.

Fix 2 is a defensible belt-and-braces guard, but it suppresses a should-never-
fire branch with no log — same tripwire-removal hazard as Fix 3.

Fix 5 is fine (developer-facing) with a minor accuracy nit.

No critical regressions; two H-severity items worth fixing before merge.
