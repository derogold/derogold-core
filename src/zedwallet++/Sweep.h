// Copyright (c) 2018-2021, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <walletbackend/WalletBackend.h>

/* Sweep funds out of the wallet to another address. The fees come out of the
   amount being swept, so the wallet never has to keep a balance back to pay
   for the sweep. If sweepAll, everything that can be spent is swept, otherwise
   the user is asked how much to sweep. */
void sweep(const std::shared_ptr<WalletBackend> walletBackend, const bool sweepAll);

/* Send `total` to address, splitting it over as many transactions as it takes
   for each one to fit in a block.

   With feeFromAmount, `total` is what leaves the wallet, and the destination
   receives it minus the fee of each transaction - this is a sweep. Without it,
   `total` is what the destination receives, and the fees are paid on top.

   Returns whether the whole of `total` was sent. */
bool sendInChunks(
    const std::shared_ptr<WalletBackend> walletBackend,
    const std::string address,
    const std::string paymentID,
    const uint64_t total,
    const bool feeFromAmount);

bool confirmSweep(
    const std::shared_ptr<WalletBackend> walletBackend,
    const std::string address,
    const uint64_t amountToSweep,
    const std::string paymentID,
    const uint64_t nodeFee);
