# Review 03 — Consistency, Faithfulness & Regression Risk

**Commit under review:** `3f701d6143` — "Address beam-density review: clarify tooltips, restore bidirectional guard"
**Lens:** Did the commit change ONLY what it claims? Compile sanity, scope, cross-file consistency, regression risk vs. prior commits.
**Files:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`, `src/libslic3r/PrintConfig.cpp`
**Date:** 2026-06-01

## Verdict

**CLEAN.** The commit does exactly what its message claims and nothing more. Scope is tight (2 files, 4 hunks), no stray edits, no whitespace damage, no accidental logic change. The restored guard compiles (`bidirectional` is an in-scope `const bool` member), is provably output-neutral, and re-aligns HEAD with the original implementation `c751da3686`. Tooltips, the hpp doc comment, and the ConfigManipulation.cpp comment are mutually consistent after this commit. No findings at High or above.

---

## Scope / faithfulness verification

`git show --numstat 3f701d6143` reports exactly two files: `InterlockingGenerator.cpp` (+12/-10) and `PrintConfig.cpp` (+4/-3). The diff contains four hunks total:

1. cpp `generateInterlockingStructure` — comment reword (gap-zone behavior).
2. cpp `applyMicrostructureToOutlines` — comment reword + the one logic line `if (bidirectional)`.
3. cpp `filterCellsForAxis` — single-word comment fix "fingers" -> "beams".
4. PrintConfig.cpp — two tooltip rewordings (group_count, gap).

The only non-comment change in the entire commit is the single guard line. This matches the message bullet-for-bullet. `git show --check` reports no trailing-whitespace / indentation damage.

---

## Findings

### 1. Restored `if (bidirectional)` guard is output-neutral and re-aligns with the original — INFO (no severity)

**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:340-341`

**Why (good):**
- `bidirectional` is a `const bool` data member (declared `InterlockingGenerator.hpp:194`, initialized `:96`), so it is in scope inside the const member function `applyMicrostructureToOutlines`. The guard compiles.
- The guard is identical to the original commit `c751da3686` (which had `if (bidirectional) filtered_type1_storage = filterCellsForAxis(cells, 1);`). The unconditional form was introduced later by `1d831afa88` ("validator hardening") with the "removes a latent empty-set trap" comment and carried through the prior fix `e57ed0375a`. This commit restores the original, so HEAD is now consistent with `c751da3686`.
- **Output-neutral proof:** `filtered_type1` is consulted at exactly one site, line 366, guarded by `layer_type == 1`. In unidirectional mode, lines 360-361 (`if (!bidirectional && layer_type == 1) continue;`) skip every type-1 layer *before* line 366 is reached, so the empty `filtered_type1` set is never queried. (Grep confirms only refs are 343/344 alias creation and 364/366 reads.) The second region loop at 386-410 does not reference the filtered sets. Therefore no slicing-behavior change — matching the commit's "No slicing-behavior change" claim.
- The "empty-set trap" worry from `1d831afa88` is unfounded for the same reason: the empty set is unreachable in unidirectional mode, so populating it was never required for correctness — only the guard ordering (skip-before-check) matters, and that ordering is intact.

**Consistency with HEAD elsewhere:** The `if (bidirectional)` semantics here now match the two other unidirectional skips in the file (`:360` in the cell loop and `:394` in the region loop), all using the same "type-1 / odd layers are skipped when not bidirectional" rule.

**Fix:** None needed.

### 2. Comment fix in `generateInterlockingStructure` is more accurate than the original — INFO (no severity)

**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:205-212`

**Why (good):** The old wording said gap-zone cells "fail both per-axis checks," which was imprecise — each beam layer only checks one axis (line 364 for type-0, line 366 for type-1), never both. The new wording ("a cell in a gap zone fails the density check for whichever layer-type is being placed") matches the actual per-layer-type single-axis check. No code changed; reasoning is now correct. Brace/paren balance of the file is intact (65/65 braces, 338/338 parens).

**Fix:** None needed.

### 3. "fingers" -> "beams" comment fix is consistent with the rest of the codebase — INFO (no severity)

**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:420`

**Why (good):** The "finger" terminology and the finger->cell `/2` scaling were removed in the prior fix `e57ed0375a` (which dropped `cell_M = (beam_group_count + 1) / 2`). This stray "fingers" word in the `filterCellsForAxis` comment was the last remnant. After this commit, the file, the hpp doc (`InterlockingGenerator.hpp:162-171` uses "beams" and "cell units = one tooth of each material"), the tooltips, and `ConfigManipulation.cpp:545-550` all consistently use "beams" / "cell units." No "finger", "snap", or "next even" leftovers remain in any of the three files (grep confirms). Cross-file terminology is fully in sync.

**Fix:** None needed.

### 4. New tooltips agree with hpp doc and ConfigManipulation.cpp — INFO (no severity)

**File:** `src/libslic3r/PrintConfig.cpp:4250-4252, 4263`

**Why (good):**
- The new "(of each filament)" qualifier matches the hpp doc's "one cell = one interlocking tooth of each material" (`InterlockingGenerator.hpp:171`) and ConfigManipulation.cpp's "one cell = one tooth of each material" (`:549`). All three now describe counts per-filament/per-material consistently.
- The new "very short boundaries may keep every beam" sentence in the group_count tooltip faithfully describes the actual `filterSegment` behavior: `if (count <= 2 * M) keep all` (the short-segment branch), also documented in hpp (`:168-170` "5-zone pattern ... beams at each end (anchored)") and in the code comment at `:425-429`.
- Both tooltips retain the "Both this and beam gap/group count must be greater than 0 to enable density control" line, which matches the runtime gate `density_enabled = beam_group_count > 0 && beam_gap > 0` (cpp `:336`) and the GUI XOR warning at `ConfigManipulation.cpp:554`. No drift between tooltip promise and enforced behavior.

**Fix:** None needed.

---

## Cross-commit consistency (vs. e57ed0375a)

No inconsistency introduced. The prior commit `e57ed0375a` did not touch the `density_enabled` block (its hunks were the `generateInterlockingStructure` comment and the `filterCellsForAxis` body). The unconditional `filtered_type1` line it inherited came from `1d831afa88`. This commit's guard restoration is the natural completion of `e57ed0375a`'s axis-correctness work and does not contradict it: `e57ed0375a` left the type-1 set unconditionally computed (a redundant pass in unidirectional mode), and this commit removes that redundancy without changing output. A reader moving from `e57ed0375a` to `3f701d6143` will find the comment at `:331-335` updated to explain exactly why the guard is safe, so there is no trap.

---

## Compile sanity summary

- `bidirectional` in scope: yes (`const bool` member, hpp:194).
- Brace balance: 65 open / 65 close across the file.
- Paren balance: 338 / 338.
- Both edited regions remain well-formed (the guard adds an `if` with a single guarded statement, no new block).
- No new includes needed; `filterCellsForAxis` already declared/used.

No Critical, High, Medium, or Low findings. Commit is faithful, scoped, and regression-free.
