# Plan: Seam burial ranking, and the CI token on disk (PROPOSED — not implemented)

Written 2026-08-11 against `e5099c9a93`. Nothing here has been applied. Proposals A and B are
alternatives to each other, not steps.

## Part 1 — Seam burial (`seam_hide_at_interface`)

### What is upstream's and what is this fork's

Upstream **already measures** how far each candidate seam point sits inside the merged layer
silhouette (`SeamCandidate::embedded_distance`, `SeamPlacer.cpp::calculate_overhangs_and_layer_embedding`)
and **already prefers** buried points in `SeamComparator` — its own comment names the case:
"perimeter points which are hidden inside the print (e.g. multimaterial join)".

Upstream does **not** expose any of it. Its seam options are `seam_position`, `seam_gap` and the six
`seam_slope_*` keys; none control burial. The preference is hardcoded at 0.5 mm, always on, no UI.

So the mechanism is upstream's; **the option is this fork's**. That is why the current design matters:
switching the fork's option on does not add a preference on top of upstream's — it **replaces** it.

### Behaviour today (this is the intended A/B toggle — no change required)

The predicate is `SeamPlacerImpl::seam_point_is_embedded_enough` (SeamPlacer.hpp), consumed at two
`SeamComparator` sites in SeamPlacer.cpp.

`embedded_distance` = signed distance to the merged silhouette **plus 0.65 × flow_width**; negative is
inside. That offset is why upstream's `-0.5` test is not a 0.5 mm requirement in practice:

| | test | effective requirement (0.42 mm extrusion width) |
|---|---|---|
| Feature OFF | `embedded_distance < -0.5` | point must be ≈ **0.77 mm** inside |
| Feature ON | `embedded_distance < -depth + 0.65·flow_width` | point must be **`seam_interface_depth`** inside (default 2.00 mm) |

Resulting behaviour by interface depth:

| Interface depth | Toggle OFF | Toggle ON |
|---|---|---|
| deeper than 2.00 mm | burial preferred | burial preferred |
| **0.77 – 2.00 mm** | burial preferred | **no preference** |
| shallower than 0.77 mm | no preference | no preference |
| **any depth, layers < `seam_interface_skip_bottom_layers`** | burial preferred | **no preference** |

In the two bold rows, enabling the feature produces a *weaker* preference than leaving it off: burial
stops acting as a tiebreaker and the seam is chosen on corner angle and visibility instead, which can
place it on an outside face. `seam_interface_depth`'s tooltip already documents the fallback.

This is inherent to an either/or switch and is accepted behaviour. 2 mm of burial is a lot to ask of a
thin colour boundary, so a real part can land in that band.

### Proposal A — grade burial instead of switching it

Replace the boolean with three grades so enabling the feature can only *add* a stronger preference.

- **2** — buried past `seam_interface_depth`, and at/after `seam_interface_skip_bottom_layers`
- **1** — buried past upstream's 0.5 mm rule (still available with the feature on)
- **0** — not buried

```cpp
// SeamPlacer.hpp — replaces seam_point_is_embedded_enough
inline uint8_t seam_point_burial_rank(const PrintObjectConfig &cfg, size_t layer_idx,
                                      bool should_compute_layer_embedding,
                                      float embedded_distance, float flow_width)
{
    if (!should_compute_layer_embedding) return 0;
    if (cfg.seam_hide_at_interface.value
        && layer_idx >= (size_t) std::max(0, cfg.seam_interface_skip_bottom_layers.value)
        && embedded_distance < -std::max(0.1f, (float) cfg.seam_interface_depth.value)
                                   + 0.65f * flow_width)
        return 2;
    if (embedded_distance < -0.5f) return 1;   // upstream rule, always available
    return 0;
}
```

```cpp
// SeamPlacer.cpp — BOTH comparator sites, replacing the two bool tests
if (a.burial_rank != b.burial_rank) return a.burial_rank > b.burial_rank;
```

With the feature off only grades 1 and 0 occur, which is byte-for-byte upstream behaviour. With it on,
the band above becomes grade 1 rather than nothing.

**Touches:** `SeamPlacer.hpp` (predicate; `SeamCandidate::embedded_enough` → `burial_rank`),
`SeamPlacer.cpp` (two comparator sites + the assignment in
`calculate_overhangs_and_layer_embedding`), `tests/libslic3r/test_config_fork.cpp` (three assertions
become grade checks).

**Costs / risks**
- Seam positions change on prints inside the band — that is the point, so a visual before/after on a
  real multimaterial model is the only meaningful verification.
- `test_config_fork.cpp` pins the current either/or semantics; update it deliberately, do not adjust
  until green.
- The two comparator sites are near-identical blocks ~58 lines apart. Fixing one and not the other is
  exactly the mistake that shipped in `92948a2ffd` (interlocking beam phase) and was caught in review.

### Proposal B — lower the default depth (cheap alternative)

The band is the gap between ≈0.77 mm and `seam_interface_depth`. Dropping the default from 2.0 mm to
~1.0 mm shrinks it to roughly a quarter of its width with no logic change and no effect on anyone who
already set their own depth. It narrows the problem, it does not remove it.

**Touches:** `PrintConfig.cpp`, the `seam_interface_depth` default only.

## Part 2 — The PAT written into `.git/config`

`build4mac.yml` / `build4mac_local.yml`, merge step:

```bash
git remote set-url origin "https://x-access-token:${PAT_TOKEN}@github.com/${REPO}"
```

`git remote set-url` writes that address into `<workspace>/.git/config` in cleartext, and nothing
removes it. On the self-hosted Mac the repo+workflow-scoped PAT therefore persists indefinitely.
(An earlier fix in `21740b9825` removed a *second* copy — the `credential.helper` line — but not this
one; the commit message overstated what it achieved.)

It is unnecessary: `actions/checkout` already authenticates with the same `PAT_TOKEN`
(`token: ${{ secrets.PAT_TOKEN }}`, `persist-credentials` defaults true), storing it as
`http.https://github.com/.extraheader` — and **its post-job step removes that**, visible as
"Post Checkout …" at the end of every run. The push authenticates on that credential alone.

```diff
 - name: Fetch and merge upstream nightly-builds
   run: |
-    git remote set-url origin "https://x-access-token:${PAT_TOKEN}@github.com/${REPO}"
     git config --local --unset-all credential.helper || true
+    # Belt and braces for a job killed before checkout's cleanup runs:
+    trap 'git config --local --unset-all http."https://github.com/".extraheader || true' EXIT
```

**Before trusting it**
- Re-run the merge simulation; the push path is all this affects and the simulation covers clean
  merge, dropped fork code, and conflict.
- On the runner after a build: `grep -c '@github.com' _work/OrcaSlicer/OrcaSlicer/.git/config` → 0.
- The token is still sent when fetching from upstream, because that is the same host (github.com).
  Standard for every Actions job; not third-party exposure.
- **This stops new copies; it does not remove the one already on the Mac.** Wipe it by hand, and
  rotating the PAT is the safer move.

**Side benefit:** `actions/checkout` compares the existing remote URL against the expected clean one
and re-clones when they differ, so removing the rewrite may also stop a full `fetch-depth: 0` + LFS
re-clone on every run.
