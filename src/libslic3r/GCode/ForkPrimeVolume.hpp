// Fork-only (cyberralf83/OrcaSlicer) — FORK(bbl-prime-volume).
// See docs/superpowers/specs/2026-10-01-bbl-prime-volume-regression-design.md.
//
// Upstream 407c78fb30 (PR #14800) switched the Type1 tower's extruder/filament-change prime from the
// process-level "Prime volume" (prime_volume) to the per-filament filament_prime_volume, which has no
// GUI field, for every printer. That undid the intent of da2934d02a and the v2.4.x behaviour. The fork
// restores prime_volume on Bambu Lab printers without a multi-nozzle extruder (everything except the
// H2C carousel); the carousel keeps upstream's per-filament behaviour, which PR #14800 was built for.
// The slicing path (Print::_make_wipe_tower), the pre-slice estimate (estimate_wipe_tower_footprint)
// and the GUI row visibility (ConfigManipulation) all decide through this one rule.
#pragma once

#include <algorithm>
#include <limits>
#include <vector>

namespace Slic3r {

// True when an extruder carries a nozzle cluster (extruder_max_nozzle_count entry > 1). Nil entries
// of the nullable option (INT_MAX) count as single-nozzle — the same rule Tab.cpp uses for the
// purge-mode selector.
inline bool fork_has_multi_nozzle_extruder(const std::vector<int> &extruder_max_nozzle_count)
{
    return std::any_of(extruder_max_nozzle_count.begin(), extruder_max_nozzle_count.end(),
                       [](int v) { return v > 1 && v != std::numeric_limits<int>::max(); });
}

// True when the Type1 tower's extruder/filament-change prime should come from prime_volume.
// Non-Bambu Type1 printers (Qidi) are left on upstream behaviour so upstream's estimate test stays green.
inline bool fork_bbl_prime_volume_applies(bool is_bbl_printer, const std::vector<int> &extruder_max_nozzle_count)
{
    return is_bbl_printer && !fork_has_multi_nozzle_extruder(extruder_max_nozzle_count);
}

} // namespace Slic3r
