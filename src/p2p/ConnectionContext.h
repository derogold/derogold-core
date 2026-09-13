// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "common/StringTools.h"
#include "crypto/hash.h"
#include "p2p/PendingLiteBlock.h"

#include <Uuid.h>
#include <chrono>
#include <list>
#include <optional>
#include <ostream>
#include <unordered_set>

namespace CryptoNote
{
    struct CryptoNoteConnectionContext
    {
        uint8_t version;
        Common::Uuid m_connection_id;
        uint32_t m_remote_ip = 0;
        uint32_t m_remote_port = 0;
        bool m_is_income = false;
        time_t m_started = 0;

        /* Steady, not high_resolution: this measures an interval, and
           high_resolution_clock is not guaranteed to be monotonic. */
        std::chrono::steady_clock::time_point m_sync_chunk_start_time {};

        /* How many blocks to ask this peer for next. Adapted from the
           throughput actually observed on this connection and clamped to
           [--sync-batch-min, --sync-batch-max]. */
        uint32_t m_sync_batch_size = 0;

        /* Rolling average of what a block from this peer costs, so a batch can
           be capped by bytes as well as by count. */
        uint64_t m_sync_avg_block_bytes = 0;

        float m_sync_blocks_per_second = 0.0f;

        uint64_t m_sync_blocks_received = 0;
        uint64_t m_sync_bytes_received = 0;

        /* Consecutive failed chunks. Past --sync-peer-failure-threshold the
           peer is dropped as a sync source. */
        uint32_t m_sync_failures = 0;

        enum state
        {
            state_before_handshake = 0, // default state
            state_synchronizing,
            state_idle,
            state_normal,
            state_sync_required,
            state_pool_sync_required,
            state_shutdown
        };

        state m_state = state_before_handshake;
        std::optional<PendingLiteBlock> m_pending_lite_block;
        std::list<Crypto::Hash> m_needed_objects;
        std::unordered_set<Crypto::Hash> m_requested_objects;
        uint32_t m_remote_blockchain_height = 0;
        uint32_t m_last_response_height = 0;
    };

    inline std::string get_protocol_state_string(CryptoNoteConnectionContext::state s)
    {
        switch (s)
        {
            case CryptoNoteConnectionContext::state_before_handshake:
                return "state_before_handshake";
            case CryptoNoteConnectionContext::state_synchronizing:
                return "state_synchronizing";
            case CryptoNoteConnectionContext::state_idle:
                return "state_idle";
            case CryptoNoteConnectionContext::state_normal:
                return "state_normal";
            case CryptoNoteConnectionContext::state_sync_required:
                return "state_sync_required";
            case CryptoNoteConnectionContext::state_pool_sync_required:
                return "state_pool_sync_required";
            case CryptoNoteConnectionContext::state_shutdown:
                return "state_shutdown";
            default:
                return "unknown";
        }
    }

} // namespace CryptoNote

namespace std
{
    inline std::ostream &operator<<(std::ostream &s, const CryptoNote::CryptoNoteConnectionContext &context)
    {
        return s << "[" << Common::ipAddressToString(context.m_remote_ip) << ":" << context.m_remote_port
                 << (context.m_is_income ? " INC" : " OUT") << "] ";
    }
} // namespace std
