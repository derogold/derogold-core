// Copyright (c) 2018-2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "crypto/crypto.h"

#include <cstdint>
#include <cstring>

namespace CryptoNote::TransactionPoolPolicy
{
    inline bool hasHigherPriority(
        const uint64_t leftFee,
        const uint64_t leftReceiveTime,
        const Crypto::Hash &leftHash,
        const uint64_t rightFee,
        const uint64_t rightReceiveTime,
        const Crypto::Hash &rightHash)
    {
        if (leftFee != rightFee)
        {
            return leftFee > rightFee;
        }

        if (leftReceiveTime != rightReceiveTime)
        {
            return leftReceiveTime < rightReceiveTime;
        }

        return std::memcmp(&leftHash, &rightHash, sizeof(Crypto::Hash)) < 0;
    }

    inline bool isExpired(const uint64_t currentTime, const uint64_t receiveTime, const uint64_t timeout)
    {
        return currentTime >= receiveTime && currentTime - receiveTime >= timeout;
    }

    inline bool isRecentlyDeleted(const uint64_t currentTime, const uint64_t deletedTime, const uint64_t timeout)
    {
        /* Treat a backwards clock adjustment conservatively: do not permit a
           just-deleted transaction back into the pool immediately. */
        return currentTime < deletedTime || currentTime - deletedTime < timeout;
    }
} // namespace CryptoNote::TransactionPoolPolicy
