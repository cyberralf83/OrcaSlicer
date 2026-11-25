#pragma once
// FORK(gap-fill-override): fork-owned header (cyberralf83/OrcaSlicer), not present upstream.
//
// Upstream's "Apply gap fill" (gap_fill_target) is an object level option with three choices.
// gap_fill_target_override is a region level option, so it can be set on a part, a modifier or a
// layer range, and it adds "top only" and "bottom only". Left on "inherit" it changes nothing.

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"

namespace Slic3r {

enum class ForkGapFillDecision : int {
    Inherit = 0, // No override: upstream's gap_fill_target decides.
    Allow,       // Gap fill this surface, whatever gap_fill_target says.
    Deny,        // No gap fill on this surface, whatever gap_fill_target says.
};

// What the override says about one surface. "Top and bottom" is the same rule as upstream's
// gftTopBottom (everything but internal solid infill), so the two settings agree on that choice.
inline ForkGapFillDecision fork_gap_fill_decision(ForkGapFillOverride override_target, SurfaceType surface_type)
{
    bool allow = false;
    switch (override_target) {
    case fgfoEverywhere: allow = true; break;
    case fgfoTopBottom:  allow = surface_type != stInternalSolid; break;
    case fgfoTop:        allow = surface_type == stTop; break;
    case fgfoBottom:     allow = surface_type == stBottom || surface_type == stBottomBridge; break;
    case fgfoNowhere:    allow = false; break;
    default:             return ForkGapFillDecision::Inherit;
    }
    return allow ? ForkGapFillDecision::Allow : ForkGapFillDecision::Deny;
}

} // namespace Slic3r
