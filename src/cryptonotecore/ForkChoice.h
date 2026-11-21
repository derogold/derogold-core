// Copyright (c) 2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "config/CryptoNoteConfig.h"
#include "crypto/hash.h"

#include <cstdint>
#include <cstring>

namespace CryptoNote::ForkChoice
{
    inline int compareHashes(const Crypto::Hash &left, const Crypto::Hash &right)
    {
        return std::memcmp(&left, &right, sizeof(Crypto::Hash));
    }

    /* At fork 11, equal-work branches converge on the branch whose first
       block after the common ancestor has the lexicographically lower hash.
       Comparing the first divergent block keeps the preference stable as both
       branches grow and avoids tip-hash reorg ping-pong. Historical forks keep
       the original strict-greater-work, first-seen rule. */
    inline bool shouldSwitchToCandidate(
        const uint64_t mainWork,
        const uint64_t candidateWork,
        const uint32_t forkHeight,
        const Crypto::Hash &mainFirstDivergentHash,
        const Crypto::Hash &candidateFirstDivergentHash)
    {
        if (candidateWork != mainWork)
        {
            return candidateWork > mainWork;
        }

        return forkHeight >= parameters::DETERMINISTIC_FORK_CHOICE_HEIGHT
            && compareHashes(candidateFirstDivergentHash, mainFirstDivergentHash) < 0;
    }
} // namespace CryptoNote::ForkChoice
