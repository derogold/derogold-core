// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "CryptoTypes.h"
#include "JsonHelper.h"
#include "json.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

using nlohmann::json;

class SynchronizationStatus
{
  public:
    /* A block the wallet has processed. The height travels with the hash
       because every use of these needs it: thinning them into a locator,
       throwing away the ones a reorg orphaned, and answering whether the block
       a daemon just sent is one we already have. */
    struct KnownBlock
    {
        uint64_t height = 0;

        Crypto::Hash hash;

        /* Wallets written before heights were recorded load their hashes with
           this set. Such an entry is still a perfectly good locator entry -
           the daemon only has to recognise the hash - but it cannot take part
           in anything that reasons about height. They age out as the wallet
           syncs. */
        bool heightKnown = false;
    };

    /////////////////////////////
    /* Public member functions */
    /////////////////////////////

    SynchronizationStatus() = default;

    /* The mutex below makes this class neither copyable nor movable by
       default, but BlockDownloader move-assigns one of these on reset. These
       transfer the data and leave each object's own mutex alone, which is the
       correct thing to do: a lock protects the object it lives in, not the
       value being moved. */
    SynchronizationStatus(SynchronizationStatus &&other)
    {
        *this = std::move(other);
    }

    SynchronizationStatus &operator=(SynchronizationStatus &&other)
    {
        if (this != &other)
        {
            std::scoped_lock lock(m_mutex, other.m_mutex);

            m_blockHashCheckpoints = std::move(other.m_blockHashCheckpoints);
            m_lastKnownBlockHashes = std::move(other.m_lastKnownBlockHashes);
            m_lastKnownBlockHeight = other.m_lastKnownBlockHeight;
            m_lastSavedCheckpointAt = other.m_lastSavedCheckpointAt;
        }

        return *this;
    }

    void storeBlockHash(const Crypto::Hash hash, const uint64_t blockHeight);

    /* Forgets every block at or above this height. Called when a reorg is
       resolved: those blocks are on a branch that no longer exists, and
       leaving them in makes the wallet offer a daemon resume points that
       cannot be found and reason about heights it no longer holds. */
    void forgetBlocksFrom(const uint64_t height);

    /* What the wallet knows at this height, if it still knows anything. Lets a
       block arriving from a daemon be recognised as one already held rather
       than taken for a reorg. */
    std::optional<Crypto::Hash> getHashAtHeight(const uint64_t height) const;

    /* Where the wallet is, for a daemon to resume from: the newest blocks one
       after another, then at doubling gaps, then the infrequent checkpoints.
       Newest first throughout, because the daemon resumes from the first entry
       it recognises and the newest usable one is the least work. */
    std::vector<Crypto::Hash> getLocator() const;

    /* Converts the class to a json object */
    void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const;

    /* Initializes the class from a json string */
    void fromJSON(const JSONObject &j);

    uint64_t getHeight() const;

  private:
    //////////////////////////////
    /* Private member variables */
    //////////////////////////////

    /* A store of blocks kept every BLOCK_HASH_CHECKPOINTS_INTERVAL blocks or
       so, for resuming after a fork deeper than the recent window reaches.
       Newest first, and capped - it used to grow by one entry every 5000
       blocks with nothing ever removing them, and every one of them was sent
       on every sync request. */
    std::deque<KnownBlock> m_blockHashCheckpoints;

    /* The most recently processed blocks, newest first. Kept densely, one per
       block, and thinned only when a locator is built. */
    std::deque<KnownBlock> m_lastKnownBlockHashes;

    /* The last block height we are aware of */
    uint64_t m_lastKnownBlockHeight = 0;

    /* The last height we saved a block checkpoint at. Can't do every 5k
       since we skip blocks with coinbase tx scanning of. */
    uint64_t m_lastSavedCheckpointAt = 0;

    /* Guards everything above. The sync thread writes here via storeBlockHash
       while the download thread reads the locator and the height to build its
       next request, and the wallet serialises the same fields on save. */
    mutable std::mutex m_mutex;
};
