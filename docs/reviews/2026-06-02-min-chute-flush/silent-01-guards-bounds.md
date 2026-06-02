# Silent-failure review — `filament_minimal_purge_on_chute` (guards / bounds / data-loss)

Scope: uncommitted working-tree diff only. Lens: silent failures around guards, bounds,
serialization data-loss, and degenerate output. `.github/**` ignored per instructions.

Feature under review: new per-filament `ConfigOptionFloats filament_minimal_purge_on_chute`
(mm, default 0). Clamp added in `GCode.cpp WipeTowerIntegration::append_tcr` (~line 873-885),
flush-count guard at ~line 969. Registered in `PrintConfig.cpp/hpp`, `Preset.cpp`,
`Print.cpp` (invalidation), `Plater.cpp` (config-change), `Tab.cpp` (UI + toggle).

---

## FINDING 1 — Option is silently inert on the DEFAULT (Type2) wipe-tower path

- Severity: **CRITICAL** (silent no-effect / data-loss-of-intent)
- Location:
  - emitter that was patched: `src/libslic3r/GCode.cpp:712` `WipeTowerIntegration::append_tcr`
    (clamp at `:883-885`)
  - emitter that was NOT patched: `src/libslic3r/GCode.cpp:1130` `append_tcr2`
  - dispatch: `src/libslic3r/GCode.cpp:1516` (`if wipe_tower_type()==Type2 → append_tcr2`,
    `else → append_tcr` at `:1555`/`:1565`); `prime()` at `:1503` also uses `append_tcr2`
  - default: `src/libslic3r/PrintConfig.cpp:5996` `wipe_tower_type` default = `WipeTowerType::Type2`
  - fallback default: `src/libslic3r/GCode.cpp:2039` `wipe_tower_type()` returns `Type2` when no print

- Masked failure: The clamp lives only in `append_tcr`, which runs **only in the
  `else` branch — i.e. when `wipe_tower_type() != Type2`**. The shipped default is `Type2`,
  whose toolchange G-code is produced by `append_tcr2`. `append_tcr2` contains **no purge/flush
  math at all** (verified: zero references to `purge_volume`, `purge_length`, `flush_length`,
  `g_min_purge_volume`, `flush_count` in lines 1130-1470 — it builds the toolchange via
  `set_extruder()` and the `tcr_rotated_gcode` placeholder). So with the out-of-box configuration,
  setting `filament_minimal_purge_on_chute` to any value produces **identical G-code** to leaving
  it 0. Worse, the tooltip explicitly tells the user this is for "Bambu Lab" printers, and the
  `wipe_tower_type` tooltip says **"Type 1 is recommended for Bambu"** — but Bambu profiles ship
  the global default Type2 unless overridden, so the very users the feature targets are the most
  likely to be on the path where it does nothing.

- Why nobody would notice: No error, no warning, no log. The option shows in the UI, accepts
  values, saves/loads fine, and invalidates/re-slices on change — it just changes nothing in the
  emitted file. The user concludes "I set 30 mm and the poop is still too small," with no signal
  that the option is structurally bypassed for their wipe-tower type. The sibling
  `filament_minimal_purge_on_wipe_tower` does NOT have this problem because it is consumed inside
  `WipeTower2.cpp` (`:2198`, `:2302`) where the Type2 purge volumes are actually computed.

- Fix: Apply the same clamp inside the Type2 emitter — either in `WipeTower2.cpp` where chute/flush
  volume is determined for Type2 toolchanges (mirroring how `filament_minimal_purge_on_wipe_tower`
  is folded in at `WipeTower2.cpp:2198`/`:2302`), or in `append_tcr2` before its toolchange G-code
  is generated. At minimum, gate the UI/tooltip on the active wipe-tower type and surface a visible
  hint ("only effective with Wipe tower type = Type 1") so a Type2 user is told the option is inert
  rather than silently ignored. The current `Tab.cpp` toggle keys on `is_BBL_printer`, NOT on
  `wipe_tower_type`, so it does not catch this case.

---

## FINDING 2 — `get_at(new_filament_id)` on an empty vector is undefined behavior in release builds

- Severity: **Medium** (pre-existing pattern; new call sites added by this diff)
- Location: `src/libslic3r/GCode.cpp:880`
  `full_config.filament_minimal_purge_on_chute.get_at(new_filament_id)`
  (also the pre-existing neighbors `filament_diameter.get_at` `:879`, etc.)
  Underlying impl: `src/libslic3r/Config.hpp:624-628`
  ```
  const T& get_at(size_t i) const {
      assert(! this->values.empty());                       // compiled out under NDEBUG
      return (i < this->values.size()) ? this->values[i] : this->values.front();
  }
  ```

- Masked failure: `get_at` does NOT bounds-check meaningfully: for an in-vector-but-out-of-range
  index it silently returns `values.front()` (so a value set for filament 0 is silently applied to
  filament N — wrong value, no error). For an **empty** vector, the only guard is `assert`, which is
  removed in `RelWithDebInfo`/release (NDEBUG). `values.front()` on an empty `std::vector` is UB —
  in practice reads past the buffer and feeds garbage into `min_chute_purge` → `purge_volume` →
  `flush_length` G-code, or crashes. No exception, no log.

- Why nobody would notice: In normal flows the per-filament vectors are normalized to the filament
  count, so the index is in range and the value is the user's. The "front() fallback returns wrong
  filament's value" case produces subtly wrong purge with no diagnostic. The empty-vector UB only
  triggers if normalization is bypassed (corrupt/partial preset, programmatic config edit), and then
  it is invisible until it manifests as garbage output or a hard-to-attribute crash.

- Note on normalization: this option is registered via `this->add(...)` as a `coFloats` and is
  added to `s_Preset_filament_options` (`Preset.cpp:1282`), so it is serialized and normalized along
  the same preset path as its sibling `filament_minimal_purge_on_wipe_tower`. It is NOT in the
  hardcoded `init_filament_option_keys()` list (`PrintConfig.cpp:7380`) — but neither is the sibling,
  so the resize path used by `set_num_filaments` (`:8811`) is not the one these options rely on; they
  inherit the sibling's (working) normalization. Hence in practice the index is in range. Risk is the
  same class as every existing `get_at` here, not newly introduced — flagged for completeness.

- Fix: Not blocking for this diff. If hardening is desired, read defensively, e.g.
  `const auto& v = full_config.filament_minimal_purge_on_chute.values;`
  `float min_chute_length = v.empty() ? 0.f : (float) v[std::min<size_t>(new_filament_id, v.size()-1)];`
  and assert `new_filament_id >= 0`.

---

## FINDING 3 — Division by `filament_area` → silent NaN/Inf into emitted flush G-code

- Severity: **Low** (pre-existing; guarded upstream by `validate()`; no NEW division added)
- Location: `src/libslic3r/GCode.cpp:879` `filament_area` (pre-existing),
  `:886` `purge_length = purge_volume / filament_area` (pre-existing),
  `:888`/`:890` feedrate divisions (pre-existing).
  The new line `:881 min_chute_purge = min_chute_length * filament_area` is a **multiplication**,
  so it introduces no new divide-by-zero.

- Masked failure: If `filament_diameter.get_at(new_filament_id)` is 0, `filament_area` is 0 and
  `purge_length`/feedrates become Inf/NaN, which would be written verbatim into the
  `flush_length`/`flush_length_N` and `*_e_feedrate` placeholders — corrupt G-code, no error.

- Why this is Low / why nobody hits it: `PrintConfig.cpp:10290-10295` (`validate()`) rejects any
  `filament_diameter < 1` with an `error_message`, so a 0/unset diameter is caught before export
  for normally-validated configs. `filament_area` is therefore non-zero in the slicing path. This
  is identical to the pre-existing risk on the unchanged lines; the new option does not widen it.

- Fix: No action required for this diff. (Broader hardening: guard `filament_area > EPSILON` before
  any division in this block — separate cleanup, out of scope.)

---

## FINDING 4 — `flush_count = max(1, …)` does not emit a degenerate non-zero flush; zero stays zero

- Severity: **Low / informational** (the guard is correct; verified no silent degenerate output)
- Location: `src/libslic3r/GCode.cpp:969-982`
  ```
  int   flush_count = std::max(1, std::min(g_max_flush_count, (int) std::round(purge_volume / g_purge_volume_one_time)));
  float flush_unit  = purge_length / flush_count;
  ```
  Constants: `g_min_purge_volume=100`, `g_purge_volume_one_time=135`, `g_max_flush_count=4`
  (`GCode.cpp:92-94`).

- Analysis of the bounds the prompt asked about:
  - Small forced purge (e.g. `min_chute_purge` = 30 mm³, real toolchange, `tcr.purge_volume<EPSILON`):
    `round(30/135)=0` → `flush_count = max(1,0) = 1`; `flush_unit = purge_length/1 = purge_length`
    (≈ the full small purge). **Not** a divide-by-zero, **not** a zero-length flush. Correct: this is
    exactly the "guarantee at least one poop" intent.
  - Zero purge (`option==0` AND `tcr.purge_volume<EPSILON`, or non-toolchange): `purge_volume=0` →
    `purge_length=0` → `flush_count = max(1,0)=1` → `flush_unit = 0/1 = 0`, so `flush_length_1=0` and
    the rest 0. This matches the *pre-change* zero-purge result (old code: `flush_count=0`, loop body
    never runs, every `flush_length_N` defaulted to 0 in the tail loop at `:978`). Functionally
    identical — no spurious non-zero flush is emitted for a genuinely-zero purge.
  - `flush_unit` can only become 0 when `purge_length` is already 0; `flush_count` is never 0, so
    there is no `x/0`.

- Masked failure: none introduced. Worth keeping in mind: a small chute value below the existing
  100 mm³ `g_min_purge_volume` floor has **no effect on the non-zero path** because the `std::max`
  at `:885` is dominated by `g_min_purge_volume`. The chute value only changes anything when it
  exceeds 100 mm³, OR when `tcr.purge_volume < EPSILON` on a real toolchange (the `:884` branch,
  which bypasses the 100 floor). This is benign but is a second "silently no visible effect for
  small inputs" surface — relevant only as UX, not data-loss.

- Fix: none required. Optionally document in the tooltip that values below the internal ~100 mm³
  floor only take effect when the normal flush has been fully diverted to infill.

---

## Serialization / data-loss checklist (verified clean)

- `Preset.cpp:1282` — added to `s_Preset_filament_options` (the canonical filament-preset
  save/load/inherit whitelist; `Preset::filament_options()` at `:1447` returns this list). Mirrors
  the sibling. **No save/load/inherit data-loss.**
- `Print.cpp:289` — added to `invalidate_state_by_config_options`; editing the value re-slices.
- `Plater.cpp:16692` — added to `on_config_change` so the wipe-tower preview/state updates.
- `PrintConfig.hpp:1456` + `PrintConfig.cpp:2722` — declared and registered with `min=0`, default
  `{0.}`. **Registered.**
- `Tab.cpp:4151` + `:4360` — UI line added; toggled on `is_BBL_printer`. NOTE: this toggle keys on
  printer brand, not on `wipe_tower_type` — see FINDING 1 (it does not hide the option when the
  active Type2 path makes it inert).

---

## Verdict

One **CRITICAL** silent failure: the option does nothing on the default `Type2` wipe-tower path
(the patch only touched `append_tcr`, not the Type2 emitter `append_tcr2`/`WipeTower2.cpp`), with no
user-visible signal. Remaining items are pre-existing `get_at`/division patterns (Medium/Low, not
widened by this diff) and a confirmed-correct flush-count guard. Serialization plumbing is complete.
