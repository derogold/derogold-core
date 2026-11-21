// Copyright (c) 2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstdint>

namespace CryptoNote::ForkChoice
{
    /* Preserve DeroGold's existing consensus rule: a candidate chain replaces
       the current main chain only when it carries strictly more cumulative
       difficulty. Equal-work branches remain first-seen until a future,
       explicitly activated deterministic tie-break is agreed. */
    inline bool shouldSwitchToCandidate(const uint64_t mainWork, const uint64_t candidateWork)
    {
        return candidateWork > mainWork;
    }
} // namespace CryptoNote::ForkChoice
