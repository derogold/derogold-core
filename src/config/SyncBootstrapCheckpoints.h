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
        uint64_t    timestamp;            // real block timestamp; 0 = fall back to wall-clock
        uint64_t    windowCumulDiff;      // cumulDiff[height] - cumulDiff[height-60]
        uint64_t    anchorPrevBlockDiff;  // cumulDiff[height] - cumulDiff[height-1]
                                          // = exact prev_D for LWMA; 0 = use windowCumulDiff/60
        // Exact timestamps for heights [height-60 .. height] (index 0 = height-60, 60 = height).
        // Used to seed synthetic pre-anchor blocks with historically correct solve-times so that
        // L = sum(ST[i]*i) matches the real network's value.  All zeros = use 300-second fallback.
        uint64_t    lwmaTimestamps[61];
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
            UINT64_C(1702454231),       // timestamp
            UINT64_C(502363727),        // windowCumulDiff
            UINT64_C(7276188),                // anchorPrevBlockDiff – fill in from export_bootstrap_state
            {
                UINT64_C(1702435339), UINT64_C(1702435602), UINT64_C(1702435611), UINT64_C(1702436648), UINT64_C(1702436789), UINT64_C(1702436961), 
                UINT64_C(1702437257), UINT64_C(1702437338), UINT64_C(1702437491), UINT64_C(1702437709), UINT64_C(1702437719), UINT64_C(1702438420), 
                UINT64_C(1702438555), UINT64_C(1702438709), UINT64_C(1702439171), UINT64_C(1702439531), UINT64_C(1702439621), UINT64_C(1702439779), 
                UINT64_C(1702439824), UINT64_C(1702439905), UINT64_C(1702440029), UINT64_C(1702440301), UINT64_C(1702440345), UINT64_C(1702440364), 
                UINT64_C(1702440708), UINT64_C(1702440850), UINT64_C(1702441072), UINT64_C(1702441183), UINT64_C(1702441364), UINT64_C(1702441427), 
                UINT64_C(1702442082), UINT64_C(1702442144), UINT64_C(1702442525), UINT64_C(1702442769), UINT64_C(1702443502), UINT64_C(1702443691), 
                UINT64_C(1702444550), UINT64_C(1702445209), UINT64_C(1702445215), UINT64_C(1702445341), UINT64_C(1702445522), UINT64_C(1702446467), 
                UINT64_C(1702446509), UINT64_C(1702446543), UINT64_C(1702447376), UINT64_C(1702447602), UINT64_C(1702447620), UINT64_C(1702448948), 
                UINT64_C(1702449109), UINT64_C(1702449129), UINT64_C(1702449890), UINT64_C(1702449897), UINT64_C(1702450489), UINT64_C(1702450507), 
                UINT64_C(1702450686), UINT64_C(1702450856), UINT64_C(1702450952), UINT64_C(1702453484), UINT64_C(1702453860), UINT64_C(1702454021), 
                UINT64_C(1702454231)
       	  },                         // lwmaTimestamps[61]  – fill in from export_bootstrap_state
        },
    };

    inline constexpr size_t SYNC_BOOTSTRAP_CHECKPOINTS_COUNT =
        sizeof(SYNC_BOOTSTRAP_CHECKPOINTS) / sizeof(SYNC_BOOTSTRAP_CHECKPOINTS[0]);

} // namespace CryptoNote
