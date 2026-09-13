// Copyright (c) 2018-2021, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <algorithm>
#include <cstdint>

namespace Sweep
{
    /* One transaction of a send that is split over several transactions */
    struct Chunk
    {
        /* Whether anything can be sent at all right now */
        bool possible = false;

        /* What the destination receives */
        uint64_t amount = 0;

        /* What leaves the wallet - the amount plus the network and node fee */
        uint64_t walletCost = 0;
    };

    /* Work out the next transaction of a send that may need to be split over
       several transactions.

       remaining     - how much of the send is left to do. With feeFromAmount
                       this is measured wallet side, so it includes the fees
                       still to be paid; otherwise it is what the destination
                       has still to receive.
       spendable     - the balance we can build a transaction from right now.
       costPerTx     - the network fee plus the node fee, paid once per
                       transaction.
       minimumSend   - the smallest amount worth sending.
       divider       - how many pieces to split the remainder into. The caller
                       doubles this each time a transaction turns out to have
                       too many inputs to fit in a block.
       feeFromAmount - take the fees out of the amount rather than adding them
                       on top. This is what a sweep does, so the wallet never
                       has to keep a balance back to pay for the sweep.

       Returns a chunk with possible = false when what is left cannot cover
       the fees and still send something worth sending. */
    inline Chunk calculateChunk(
        const uint64_t remaining,
        const uint64_t spendable,
        const uint64_t costPerTx,
        const uint64_t minimumSend,
        const uint64_t divider,
        const bool feeFromAmount)
    {
        Chunk chunk;

        if (divider == 0)
        {
            return chunk;
        }

        if (feeFromAmount)
        {
            /* We can't sweep more than we can spend */
            const uint64_t walletCost = std::min(remaining, spendable) / divider;

            /* Doesn't cover the fees, or what is left after them isn't worth
               sending */
            if (walletCost <= costPerTx || walletCost - costPerTx < minimumSend)
            {
                return chunk;
            }

            chunk.possible = true;
            chunk.walletCost = walletCost;
            chunk.amount = walletCost - costPerTx;

            return chunk;
        }

        /* The fees are paid on top of the amount, so we need to keep enough
           back to pay them */
        if (spendable <= costPerTx)
        {
            return chunk;
        }

        const uint64_t amount = std::min(remaining, spendable - costPerTx) / divider;

        if (amount < minimumSend)
        {
            return chunk;
        }

        chunk.possible = true;
        chunk.amount = amount;
        chunk.walletCost = amount + costPerTx;

        return chunk;
    }
} // namespace Sweep
