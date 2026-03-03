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
        /* height 1 000 000 – fill in with: export_bootstrap_state 1000000 */
        {
            1000000,
            "4c97a2ff7a09aa38eb5ee2a49afa0d05b9071f470e3afc1a873a24607100ae77", // replace
            UINT64_C(963502374716270),   // alreadyGeneratedCoins  – replace
            UINT64_C(26545226639472),   // cumulativeDifficulty   – replace
            UINT64_C(3071175),   // alreadyGeneratedTransactions – replace
        },
        /* height 1 500 000 – fill in with: export_bootstrap_state 1500000 */
        {
            1500000,
            "93fd60fe4dd8eb7d65c3476de910eca36d73f970a746bfa691c43aefb1725daa", // replace
            UINT64_C(1228253003805553),
            UINT64_C(28983155871648),
            UINT64_C(18652499),
        },
        /* height 2 000 000 – fill in with: export_bootstrap_state 2000000 */
        {
            2000000,
            "d02e358e198630f9da636526521d06f1152989140b0bf615be50e7be7086c5bd", // replace
            UINT64_C(1484020623125170),
            UINT64_C(30581354740331),
            UINT64_C(24699942),
        },
        /* height 2 200 000 – fill in with: export_bootstrap_state 2200000 */
        {
            2500000,
            "29ee0c13fb06f6ca3e4433957bd5d0d2a837e2cd95d473e08633caf100cea64b", // replace
            UINT64_C(1731799467373277),
            UINT64_C(71990059621657),
            UINT64_C(30406065),
        },
        /* height 2 400 000 – fill in with: export_bootstrap_state 2400000 */
        {
            2700000,
            "088438ac255023638252a926c0ff4177a90224a90453d5764902d1f844e3394c", // replace
            UINT64_C(1829293564005886),
            UINT64_C(89906083563503),
            UINT64_C(31263053),
        },
    };

    inline constexpr size_t SYNC_BOOTSTRAP_CHECKPOINTS_COUNT =
        sizeof(SYNC_BOOTSTRAP_CHECKPOINTS) / sizeof(SYNC_BOOTSTRAP_CHECKPOINTS[0]);

} // namespace CryptoNote
