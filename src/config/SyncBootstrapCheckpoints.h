// Copyright (c) 2018-2024, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstdint>
#include <cstddef>

namespace CryptoNote
{
    /**
     * Bootstrap checkpoint for --sync-from-height.
     *
     * These records let a fresh node start syncing from a recent block without
     * downloading the full 350 GB+ chain from genesis.  Each entry is extracted
     * from a fully-synced node with the `export_bootstrap_state <height>` daemon
     * command and then committed here.
     *
     * Required fields
     * ---------------
     *   height               – Block height of the anchor (must also appear in
     *                          CryptoNoteCheckpoints.h so the hash is trusted).
     *   blockHash            – 64-hex block hash (verified against checkpoints).
     *   alreadyGeneratedCoins – Total coin emission (atomic units) through this
     *                           block, inclusive.
     *   cumulativeDifficulty – Sum of block difficulties from genesis through
     *                          this block, inclusive.
     *   alreadyGeneratedTransactions – Total tx count through this block.
     *   timestamp            – Unix timestamp of this block as recorded on-chain.
     *                          Used to seed synthetic pre-anchor block timestamps
     *                          so that the first real block after the anchor passes
     *                          the median-timestamp validation.  Set to 0 if
     *                          unknown (falls back to wall-clock; safe due to the
     *                          sync-floor timestamp bypass in Core.cpp).
     *
     * How to add a new entry
     * ----------------------
     * 1.  Start a fully-synced derogoldd node.
     * 2.  Run the daemon console command:
     *         export_bootstrap_state <height>
     *     where <height> is one of the heights listed in CryptoNoteCheckpoints.h.
     * 3.  Copy the printed values into a new BootstrapCheckpoint entry below.
     * 4.  Rebuild and distribute.
     */
    struct BootstrapCheckpoint
    {
        uint32_t    height;
        const char *blockHash;
        uint64_t    alreadyGeneratedCoins;
        uint64_t    cumulativeDifficulty;
        uint64_t    alreadyGeneratedTransactions;
        uint64_t    timestamp;       // 0 = unknown, falls back to wall-clock
        uint64_t    windowCumulDiff; // cumulDiff[height] - cumulDiff[height-60]
                                     // 0 = unknown, falls back to historical average
                                     // Get with: export_bootstrap_state <height>
    };

    /**
     * Hard-coded bootstrap anchors.
     *
     * The values below are PLACEHOLDERS – replace them with output from
     * `export_bootstrap_state <height>` on a fully-synced node before shipping.
     *
     * Keep entries sorted ascending by height.
     */
    inline constexpr BootstrapCheckpoint SYNC_BOOTSTRAP_CHECKPOINTS[] = {
        /* height 2 700 000 – fill in with: export_bootstrap_state 2700000 */
        {
            2700000,
            "088438ac255023638252a926c0ff4177a90224a90453d5764902d1f844e3394c",
            UINT64_C(1829293564005886),
            UINT64_C(89906083563503),
            UINT64_C(31263053),
            UINT64_C(0),                // timestamp        – fill in from export_bootstrap_state
            UINT64_C(502363727),        // windowCumulDiff  – fill in from export_bootstrap_state
        },
    };

    inline constexpr size_t SYNC_BOOTSTRAP_CHECKPOINTS_COUNT =
        sizeof(SYNC_BOOTSTRAP_CHECKPOINTS) / sizeof(SYNC_BOOTSTRAP_CHECKPOINTS[0]);

} // namespace CryptoNote
