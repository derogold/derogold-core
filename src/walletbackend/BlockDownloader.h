// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <WalletTypes.h>
#include <atomic>
#include <nigel/Nigel.h>
#include <subwallets/SubWallets.h>
#include <utilities/ThreadSafeDeque.h>
#include <vector>
#include <walletbackend/SynchronizationStatus.h>

class BlockDownloader
{
  public:
    BlockDownloader() {};

    /* Constructor */
    BlockDownloader(
        const std::shared_ptr<Nigel> daemon,
        const std::shared_ptr<SubWallets> subWallets,
        const uint64_t startHeight,
        const uint64_t startTimestamp);

    /* Move constructor */
    BlockDownloader(BlockDownloader &&old);

    /* Move assignment operator */
    BlockDownloader &operator=(BlockDownloader &&old);

    /* Destructor */
    ~BlockDownloader();

    /////////////////////////////
    /* Public member functions */
    /////////////////////////////

    /* Retrieve blockCount blocks from the internal store. does not remove
       them. Returns as many as possible if the amount requested is not
       available. May be empty (this is the norm when synced.) */
    std::vector<std::tuple<WalletTypes::WalletBlockInfo, uint32_t>> fetchBlocks(const size_t blockCount);

    /* Drops the oldest block from the internal queue */
    void dropBlock(const uint64_t blockHeight, const Crypto::Hash blockHash);

    /* Throws away what the wallet knew at and above this height, once a reorg
       has established that those blocks are gone. */
    void forgetBlocksFrom(const uint64_t height);

    /* What the wallet holds at this height, if it still holds anything. */
    std::optional<Crypto::Hash> getHashAtHeight(const uint64_t height) const;

    /* Start block downloading process */
    void start();

    /* Stop block downloading process */
    void stop();

    /* Returns height of processed blocks */
    uint64_t getHeight() const;

    /* Returns the prune floor reported by the daemon (0 if not pruned) */
    uint64_t getPruneFloor() const;

    /* Forget the prune floor. Call when the daemon we talk to changes, so the
       floor is re-learned from the new node instead of a stale one being
       applied to a node that may not be pruned at all. */
    void clearPruneFloor();

    void fromJSON(const JSONObject &j, const uint64_t startHeight, const uint64_t startTimestamp);

    void toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const;

    void setSubWallets(const std::shared_ptr<SubWallets> subWallets);

    void initializeAfterLoad(const std::shared_ptr<Nigel> daemon);

  private:
    //////////////////////////////
    /* Private member functions */
    //////////////////////////////

    /* Synchronizes pre-fetching blocks */
    void downloader();

    /* Determines if we should prefetch more blocks */
    bool shouldFetchMoreBlocks() const;

    /* Gets checkpoints of stored (not processed) blocks */
    std::vector<Crypto::Hash> getStoredBlockCheckpoints() const;

    /* Gets checkpoints of stored, processed, and infrequent checkpoints */
    std::vector<Crypto::Hash> getBlockCheckpoints() const;

    /* Downloads a set of blocks, if needed */
    bool downloadBlocks();

    //////////////////////////////
    /* Private member variables */
    //////////////////////////////

    /* Cached blocks */
    ThreadSafeDeque<std::tuple<WalletTypes::WalletBlockInfo, uint32_t>> m_storedBlocks;

    /* The daemon connection */
    std::shared_ptr<Nigel> m_daemon;

    /* Timestamp to begin syncing at */
    uint64_t m_startTimestamp;

    /* Height to begin syncing at */
    uint64_t m_startHeight;

    /* Prune floor reported by daemon (0 = daemon not pruned) */
    std::atomic<uint64_t> m_pruneFloor {0};

    /* Sync progress */
    SynchronizationStatus m_synchronizationStatus;

    std::shared_ptr<SubWallets> m_subWallets;

    /* For synchronizing block downloading */
    std::mutex m_mutex;

    /* Are we ready to go attempt to retrieve more data */
    std::atomic<bool> m_consumedData = true;

    /* Should we try and fetch more data (Used in conjunction with m_consumedData) */
    std::condition_variable m_shouldTryFetch;

    /* Should we stop downloading */
    std::atomic<bool> m_shouldStop = false;

    /* Thread that performs the actual downloading of blocks */
    std::thread m_downloadThread;

    uint32_t m_arrivalIndex = 0;
};
