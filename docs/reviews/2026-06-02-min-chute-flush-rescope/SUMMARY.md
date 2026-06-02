# Min-chute-flush RESCOPE review — consolidated findings (2026-06-02)

4 agents (3 review + 1 silent-failure) reviewed the rescope (commit `4673720c01`) and the final feature state. Raw output: `review-01..03`, `silent-01`.

## Verdict: CLEAN — no actionable findings
No Critical / High / Medium issues from any agent. All four independently confirmed the rescope is correct and that the feature has **zero footprint at the default (option = 0)**.

Confirmed by ≥2 agents:
- **Default byte-identical to upstream** — at option 0, `min_chute_purge == 0` ⇒ `apply_chute_min == false` ⇒ the `purge_volume` ternary reduces to the exact upstream expression; `flush_count` reverted to upstream `std::min(...)`.
- **`set_extruder` fully reverted** — isolated body diff vs upstream base is empty; no orphaned `filament_minimal_purge_on_chute` reference; `append_tcr2`/Type2 carry no clamp.
- **GUI gate == execution gate** — both derive from `is_bbl_vendor()` / `is_BBL_printer()`; no hidden-but-active or visible-but-inert mismatch. A BBL filament preset carried to a non-BBL printer keeps its value but is both hidden and inert (consistent).
- **`is_BBL_Printer()` null fallback unreachable** from `append_tcr` — `m_curr_print` is set at the top of `do_export` before `append_tcr` runs.
- **No harmful div-by-zero** — when the floor is active `purge_volume ≥ 100 mm³` ⇒ `flush_count ≥ 1`; the residual `(0,67.5) mm³` NaN window is pre-existing upstream and unreachable via this feature.
- **Compile-safe** — `gcodegen.is_BBL_Printer()` callable, `std::max({...})` init-list deduces to float, casts/index safe, `is_real_toolchange` still consumed (no unused-var).
- **Tooltip accurate** — "~40 mm (100 mm³)" matches `g_min_purge_volume`; "only effective … Bambu Lab" is now true given the `is_BBL_Printer()` gate.

LOW notes (all non-actionable): `is_BBL_Printer()` null fallback (unreachable), intended inertness on priming/finish-layer has no diagnostic (by design, disclosed in tooltip/comment), pre-existing upstream NaN window, `get_at` front() fallback (same as all neighbors).

## Actions
- Wave 12 (fix multi-agent): **none** — no multi-agent issues.
- Wave 13 (verify+fix single-agent): **none** — no single-agent issues above LOW; LOW items verified as non-issues.

The feature is considered done pending compilation (CI). Final source commits: `eef00f7032` (feature) + `4673720c01` (rescope to BBL/append_tcr).
