# Design: Hide seam at part interface (multi-material)

**Date:** 2026-06-01
**Branch:** nightly-builds-with-bc (fork: cyberralf83/OrcaSlicer)
**Status:** Reviewed by 4 agents; corrected. Ready to implement.

## 1. User intent

On a multi-material print made of **one object with multiple touching parts/volumes** (different
filaments), route each wall's seam into the **buried contact interface** between the parts so it is
hidden by the neighbouring part — with two adjustable offsets:

- **a bit off the side**: require a minimum *burial depth* inside the interface (default ~2 mm).
- **a bit off the bottom**: skip the first N layers near the plate (default 5), seaming normally there.

Selectable: global default + **object-level** override (consistent with `seam_position`; note this is
per-object, **not** per individual touching part). Out of scope for v1: separate objects placed
touching, and painted MMU face segmentation.

## 2. Approach (A): extend the existing embedded-seam logic

OrcaSlicer already half-implements this. In `src/libslic3r/GCode/SeamPlacer.cpp`:

- `calculate_overhangs_and_layer_embedding()` (line 1058) computes `embedded_distance` per perimeter
  point, **only when `regions_with_perimeter > 1`** (line 1077). Value (line 1096-1097):
  `embedded_distance = distance_from_lines<true>(point) + 0.65f * perimeter.flow_width`, where the
  distancer is built from `po->layers()[i]->lslices` (the **merged outer silhouette**, confirmed
  `Layer.cpp:44-48`; signed distance is **negative inside**, confirmed `AABBTreeLines.hpp`).
- `SeamComparator::is_first_better` (lines 770/773) and `is_first_not_much_worse` (lines 828/831)
  prefer points with `embedded_distance < -0.5f`.

Missing vs. the ask: on/off switch, configurable depth, bottom-layer skip, UI exposure — and the
preference must reach **all** seam modes (see §5).

## 3. Config (new keys in `PrintObjectConfig`)

Added to `PrintObjectConfig` (PrintConfig.hpp ~line 944, where `seam_position` lives). Registered in
PrintConfig.cpp next to `seam_position` (~5472), category "Quality".

| key | type | default | mode | sidetext | meaning |
|---|---|---|---|---|---|
| `seam_hide_at_interface` | `ConfigOptionBool` | `false` | `comAdvanced` | — | Master toggle. **false ⇒ exact upstream behavior.** |
| `seam_interface_depth` | `ConfigOptionFloat` (mm) | `2.0` | `comAdvanced` | `mm` | Minimum depth the seam must be buried inside the interface. |
| `seam_interface_skip_bottom_layers` | `ConfigOptionInt` | `5` | `comAdvanced` | `layers` | First N object layers fall back to normal seam. |

(Renamed `seam_interface_margin` → `seam_interface_depth` per review H2: it's a burial *depth*, not a
crease-relative offset. Tooltip must say "approximate minimum depth buried inside the contact
interface", and warn that values larger than the actual interface depth fall back to a normal seam,
and that a skip ≥ the object's layer count disables it for that object.)

## 4. Behavior / data flow (the corrected mechanism)

The key change vs. the original proposal: **move the "is this point buried enough?" decision off the
comparator and onto the candidate**, computed once where both the config and the per-point
`flow_width` are in scope. This makes every comparator path (Aligned, Nearest, Random, Rear) respect
the same decision and makes the depth exact.

1. **`SeamCandidate`** (SeamPlacer.hpp ~line 58): add member `bool embedded_enough = false;`
   (initialise in the ctor alongside `embedded_distance(0.0f)`).

2. **`calculate_overhangs_and_layer_embedding(po)`** (SeamPlacer.cpp ~1058, has `po`): read
   `const PrintObjectConfig& cfg = po->config();`. Keep computing `embedded_distance` exactly as
   today when `regions_with_perimeter > 1`. Then set, per point:
   - if `!cfg.seam_hide_at_interface`:
     `embedded_enough = should_compute_layer_embedding && (embedded_distance < -0.5f);`  ← upstream-identical
   - else:
     `embedded_enough = should_compute_layer_embedding`
        `&& (layer_idx >= cfg.seam_interface_skip_bottom_layers)`
        `&& (embedded_distance < -(cfg.seam_interface_depth) + 0.65f * perimeter.flow_width);`
   The `+0.65f*flow_width` term cancels the same offset baked into `embedded_distance` (R4 fixed,
   per-point). On skipped bottom layers / single-region layers, `embedded_enough` stays `false` ⇒
   normal seam.

3. **`SeamComparator`** (SeamPlacer.cpp ~742): replace the four `embedded_distance < -0.5f` /
   `> -0.5f` comparisons at lines **770, 773, 828, 831** with the boolean:
   - `if (a.embedded_enough && !b.embedded_enough) return true;`
   - `if (b.embedded_enough && !a.embedded_enough) return false;`
   No threshold field on the comparator; **no plumbing into the Nearest/Random comparators needed** —
   they read `embedded_enough` straight off the candidate. (Fixes the Critical Nearest/Random gap and
   the comparator-ordering Low at once.) Leave the Enforced/Blocked early-returns (809-815) untouched.

4. **Invalidation** (PrintObject.cpp ~1374): add the three keys to the existing `opt_key ==
   "seam_position" || …` group whose action is `invalidate_step(psGCodeExport)` (line 1421). The
   SeamPlacer runs during G-code export (`GCode.cpp:3205`), so `psGCodeExport` is correct; do **not**
   touch posSlice/posPerimeters. (Omitting them would hit the `else` → `invalidate_all_steps` —
   safe but a needless full re-slice.)

5. **GUI:**
   - Layout: `Tab.cpp` ~2333 — `append_single_option_line` the three keys after `seam_position`.
   - Enable/disable: `ConfigManipulation.cpp` (`update_print_fff_config` / `update_print_other_options`,
     near the existing seam `toggle_line`s ~610-636 / ~967-979) —
     `toggle_line("seam_interface_depth", have_perimeters && opt_bool("seam_hide_at_interface"));`
     and likewise for the skip key.
   - Per-object override: add the three keys to `SettingsFactory::OBJECT_CATEGORY_SETTINGS["Quality"]`
     in `GUI_Factories.cpp` ~line 86 (the **live** map — NOT the commented block at ~199-211).

No change to `place_seam()`, visibility/overhang scoring, or `align_seam_points` (it consumes the
same comparator/booleans, so the stem still stacks straight up the interface).

## 5. Seam-mode behavior (was the Critical bug)

Because `embedded_enough` lives on the candidate:
- **Aligned / Aligned-back / Rear** (default is Aligned): pick pass + alignment use it. Stem hides
  and stacks. ✅
- **Nearest**: `pick_nearest_seam_point_index` builds its own `SeamComparator{spNearest}` (line 933)
  but `is_first_better` checks `embedded_enough` *before* the nozzle-distance penalty (line 770 vs
  783), so the buried interface still wins. The feature now works in Nearest too. ✅
- **Random**: `pick_random_seam_point` (line 946) — `embedded_enough` is honored in
  `is_first_not_much_worse` before the `spRandom` short-circuit; hiding is respected. ✅

## 6. Edge cases & interactions

- **Single-region layer** (`regions ≤ 1`): `embedded_enough=false` ⇒ normal seam. ✅
- **Painted enforcers/blockers**: `type` discrimination precedes embedded check (line 760) ⇒ painting
  still wins. ✅
- **Spiral/vase**: `place_seam` skipped (`GCode.cpp:5749`) ⇒ inert. ✅
- **Scarf joint**: applied after seam selection ⇒ composes. ✅
- **Multi-region, same filament (modifier meshes)**: the gate is `regions>1`, not filament-distinct,
  so a same-filament modifier boundary also counts as an "interface". Documented limitation (matches
  upstream's existing embedded behavior); not gating on filament in v1.
- **Raft**: `layer_idx` indexes `po->layers()` (raft excluded) — skip counts object layers above the
  raft. Acceptable; note in tooltip.

## 7. Known silent-failure surfaces (documented; loud where cheap)

- **Depth deeper than the interface** → no point qualifies → silent fallback to a normal (possibly
  visible) seam. v1: tooltip warning. Optional future enhancement: count perimeters where hiding was
  requested but no candidate qualified across mid-height layers and emit a slicing warning.
- **skip ≥ object layer count** (short parts; default 5 disables the feature on ≤5-layer coupons) →
  silently inert. v1: tooltip warning.
- **Default-off regression risk**: the four `-0.5f` sites must all become the boolean and
  `embedded_enough` must reduce to `embedded_distance < -0.5f` when off. Covered by the regression
  test in §8.

## 8. Testing

- **Regression (highest value):** with `seam_hide_at_interface=false`, seam indices/positions must be
  identical to pre-change on a two-region model. Guards the refactor of the shared comparator path.
- **Unit:** test `embedded_enough` logic directly — false on layers `< skip`, false when
  `regions ≤ 1`, true for a point with `embedded_distance` below the depth threshold above the skip;
  and the comparator boolean branch (a buried candidate beats a non-buried one regardless of
  visibility).
- **Harness note (review M2):** `tests/fff_print` has no existing seam tests and `init_print` does
  not assign per-volume extruders, so a full "two-filament two-volume, assert seam X" integration
  test needs new harness support (hand-built `ModelVolume`s with per-volume extruder config + a
  G-code seam-position parser). If that's too heavy for v1, rely on the unit + regression tests plus
  manual G-code-preview verification, and say so — do not claim integration coverage we don't have.
- **Manual:** slice a two-colour block; confirm in preview the stem stacks on the join above layer 5
  and uses a normal seam below it; confirm toggling off reverts.

## 9. Review status

4-agent review consensus folded in: Nearest/Random gap (Critical, all 4) → fixed via per-candidate
`embedded_enough`; per-object list line 86 not 201 (all 4); `psGCodeExport` invalidation (all 4);
ConfigManipulation toggling (3); per-point flow-width offset (all 4) → cancelled at compute time;
depth-vs-crease wording (2) → renamed to `seam_interface_depth`. Single-agent items verified and
adopted: `comAdvanced`+sidetext, object-vs-part wording, harness limits, short-object/too-deep
documentation.
