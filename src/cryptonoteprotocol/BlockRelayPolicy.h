// Copyright (c) 2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "cryptonotecore/AddBlockErrors.h"

#include <system_error>

namespace CryptoNote::BlockRelayPolicy
{
    inline bool shouldRelay(const std::error_code &result)
    {
        return result == error::AddBlockErrorCode::ADDED_TO_MAIN
               || result == error::AddBlockErrorCode::ADDED_TO_ALTERNATIVE
               || result == error::AddBlockErrorCode::ADDED_TO_ALTERNATIVE_AND_SWITCHED;
    }
} // namespace CryptoNote::BlockRelayPolicy
