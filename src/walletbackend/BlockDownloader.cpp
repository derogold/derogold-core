// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

//////////////////////////////////////////
#include <walletbackend/BlockDownloader.h>
//////////////////////////////////////////

#include <config/Config.h>
#include <config/WalletConfig.h>
#include <logger/Logger.h>
#include <utilities/FormatTools.h>
#include <utilities/Utilities.h>
#include <walletbackend/Constants.h>

/* Constructor */
BlockDownloader::BlockDownloader(
    const std::shared_ptr<Nigel> daemon,
    const std::shared_ptr<SubWallets> subWallets,
    const uint64_t startHeight,
    const uint64_t startTimestamp):

    m_daemon(daemon),
    m_subWallets(subWallets),
    m_startHeight(startHeight),
    m_startTimestamp(startTimestamp)
{
}

/* Move constructor */
BlockDownloader::BlockDownloader(BlockDownloader &&old)
{
    /* Call the move assignment operator */
    *this = std::move(old);
}

/* Move assignment operator */
BlockDownloader &BlockDownloader::operator=(BlockDownloader &&old)
{
    /* Stop any running threads */
    stop();

    m_storedBlocks = std::move(old.m_storedBlocks);

    m_daemon = std::move(old.m_daemon);

    m_startTimestamp = std::move(old.m_startTimestamp);
    m_startHeight = std::move(old.m_startHeight);

    m_synchronizationStatus = std::move(old.m_synchronizationStatus);

    m_consumedData = std::move(old.m_consumedData.load());

    m_shouldStop = std::move(old.m_shouldStop.load());

    /* Carry the prune floor across. Leaving it behind meant a downloader that
       replaced a pruned-node one kept the old floor forever, so re-syncing
       against a full node still skipped global index lookups and left inputs
       unspendable until the process was restarted. */
    m_pruneFloor = old.m_pruneFloor.load();

    return *this;
}

/* Destructor */
BlockDownloader::~BlockDownloader()
{
    stop();
}

void BlockDownloader::start()
{
    m_shouldStop = false;
    m_storedBlocks.start();
    m_downloadThread = std::thread(&BlockDownloader::downloader, this);
}

void BlockDownloader::stop()
{
    m_shouldStop = true;
    m_consumedData = true;
    m_shouldTryFetch.notify_one();
    m_storedBlocks.stop();

    if (m_downloadThread.joinable())
    {
        m_downloadThread.join();
    }
}

uint64_t BlockDownloader::getHeight() const
{
    return m_synchronizationStatus.getHeight();
}

uint64_t BlockDownloader::getPruneFloor() const
{
    return m_pruneFloor.load();
}

void BlockDownloader::clearPruneFloor()
{
    m_pruneFloor.store(0);
}

void BlockDownloader::downloader()
{
    while (!m_shouldStop)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            /* Timed wait: stop() and the consumer both signal without holding
               this mutex, so a notification arriving between the predicate check
               and the wait is lost. An untimed wait turned that into a download
               thread that never woke, which hung the join in stop() and with it
               every save and the wallet's own shutdown. */
            m_shouldTryFetch.wait_for(lock, std::chrono::milliseconds(100), [&] {
                if (m_shouldStop)
                {
                    return true;
                }

                return m_consumedData.load();
            });
        }

        if (m_shouldStop)
        {
            break;
        }

        while (shouldFetchMoreBlocks() && !m_shouldStop)
        {
            const bool blocksDownloaded = downloadBlocks();

            if (!blocksDownloaded)
            {
                /* A second is the right pause for "nothing new yet". When the
                   daemon has told us why it will not serve this wallet at all,
                   asking again every second answers nothing and just loads a
                   node that has already given its answer. */
                const auto pause = m_daemon->syncError().empty() ? std::chrono::seconds(1) : std::chrono::seconds(15);

                Utilities::sleepUnlessStopping(pause, m_shouldStop);
                break;
            }
        }

        m_consumedData = false;
    }
}

bool BlockDownloader::shouldFetchMoreBlocks() const
{
    /* Take the block by reference. Taking it by value copied every stored block
       in full on each call, and this runs in the download loop. */
    size_t ramUsage = m_storedBlocks.memoryUsage([](const auto &block) { return std::get<0>(block).memoryUsage(); });

    if (ramUsage + WalletConfig::maxBodyResponseSize < WalletConfig::blockStoreMemoryLimit)
    {
        std::stringstream stream;

        stream << "Approximate ram usage of stored blocks: " << Utilities::prettyPrintBytes(ramUsage)
               << ", fetching more.";

        Logger::logger.log(stream.str(), Logger::DEBUG, {Logger::SYNC});

        return true;
    }

    return false;
}

void BlockDownloader::forgetBlocksFrom(const uint64_t height)
{
    m_synchronizationStatus.forgetBlocksFrom(height);
}

std::optional<Crypto::Hash> BlockDownloader::getHashAtHeight(const uint64_t height) const
{
    return m_synchronizationStatus.getHashAtHeight(height);
}

void BlockDownloader::dropBlock(const uint64_t blockHeight, const Crypto::Hash blockHash)
{
    m_storedBlocks.pop_front();
    m_synchronizationStatus.storeBlockHash(blockHash, blockHeight);

    /* Indicate to the downloader that it should try and download more */
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consumedData = true;
    m_shouldTryFetch.notify_one();
}

std::vector<std::tuple<WalletTypes::WalletBlockInfo, uint32_t>> BlockDownloader::fetchBlocks(const size_t blockCount)
{
    /* Attempt to fetch more blocks if we've run out */
    if (m_storedBlocks.size() == 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_consumedData = true;
        m_shouldTryFetch.notify_one();

        return {};
    }

    const auto blocks = m_storedBlocks.front_n(blockCount);

    Logger::logger.log(
        "Fetched " + std::to_string(blocks.size()) + " blocks from internal store", Logger::DEBUG, {Logger::SYNC});

    return blocks;
}

std::vector<Crypto::Hash> BlockDownloader::getStoredBlockCheckpoints() const
{
    const auto blocks = m_storedBlocks.back_n(Constants::LAST_KNOWN_BLOCK_HASHES_SIZE);

    std::vector<Crypto::Hash> result;

    result.resize(blocks.size());

    std::transform(
        blocks.begin(), blocks.end(), result.begin(), [](const auto block) { return std::get<0>(block).blockHash; });

    return result;
}

std::vector<Crypto::Hash> BlockDownloader::getBlockCheckpoints() const
{
    /* Newest first, so the daemon resumes from the first entry it recognises
       and that is the least work for both ends.

       Blocks already downloaded but not yet processed come first: they are the
       furthest ahead the wallet has got. Then where the wallet has actually
       processed to, dense near the tip and thinning with depth, and then the
       infrequent checkpoints for a fork past all of that.

       This used to pad the unprocessed hashes up to fifty with processed ones
       and stop, so a wallet with a full download buffer offered nothing at all
       about where it had really processed to. Both are worth saying, and
       saying both costs a couple of dozen hashes. */
    std::vector<Crypto::Hash> result = getStoredBlockCheckpoints();

    const auto locator = m_synchronizationStatus.getLocator();

    std::copy(locator.begin(), locator.end(), std::back_inserter(result));

    return result;
}

bool BlockDownloader::downloadBlocks()
{
    const uint64_t localDaemonBlockCount = m_daemon->localDaemonBlockCount();

    const uint64_t walletBlockCount = m_synchronizationStatus.getHeight();

    if (localDaemonBlockCount < walletBlockCount)
    {
        return false;
    }

    const auto blockCheckpoints = getBlockCheckpoints();

    if (blockCheckpoints.size() > 0)
    {
        std::stringstream stream;

        stream << "First checkpoint: " << blockCheckpoints.front() << "\nLast checkpoint: " << blockCheckpoints.back();

        Logger::logger.log(stream.str(), Logger::DEBUG, {Logger::SYNC});
    }

    const auto [success, blocks, topBlock, pruneFloor] = m_daemon->getWalletSyncData(
        blockCheckpoints, m_startHeight, m_startTimestamp, Config::config.wallet.skipCoinbaseTransactions);

    /* Handle prune floor BEFORE the empty-blocks check.  When connecting to
       a pruned node, the daemon may return zero blocks but still report a
       prune floor.  If we don't advance m_startHeight here, we'll keep
       requesting the same pruned range and get stuck in an infinite loop. */
    if (success && pruneFloor > 0 && pruneFloor > m_startHeight)
    {
        m_pruneFloor.store(pruneFloor);

        const bool prunedItemsCovered = !blocks.empty() && blocks.front().blockHeight < pruneFloor;

        if (!prunedItemsCovered)
        {
            Logger::logger.log(
                "Daemon prune floor at height " + std::to_string(pruneFloor) +
                ". No wallet data for blocks " + std::to_string(m_startHeight) +
                " to " + std::to_string(pruneFloor - 1) + ".",
                Logger::WARNING, {Logger::SYNC, Logger::DAEMON});

            m_startHeight = pruneFloor;

            /* Re-request from the new start height on the next iteration
               rather than falling through with an empty block list. */
            if (blocks.empty())
            {
                return true;
            }
        }
    }

    /* Synced, store the top block so sync status displayes correctly if
       we are not scanning coinbase tx only blocks */
    /* We can have an issue where we download a block, say, block 1000,
       then because we have space for more blocks, we go to fetch more,
       and this time get none, because we're synced. We then store the
       topblock, which is also 1000, as having being processed, when in
       fact, we're still waiting for it to be processed. So, if we only store
       it if we have no blocks waiting to be processed, it fixes this issue */
    if (success && blocks.empty() && topBlock && m_storedBlocks.size() == 0)
    {
        m_synchronizationStatus.storeBlockHash(topBlock->hash, topBlock->height);

        /* Taking the top block is the wallet saying it has scanned as far as
           here, which answers the date it was going to start from: it starts
           from here. Leaving the timestamp set meant every later request still
           asked the daemon to place that date, and a daemon that could not
           place it answered nothing however many blocks had since been mined -
           the wallet sat at the tip calling itself fully synced, showing no
           transactions, and only a reset (which starts from a height) moved
           it. A height cannot fail to be placed. */
        if (m_startTimestamp != 0)
        {
            m_startTimestamp = 0;
            m_startHeight = topBlock->height;

            if (m_subWallets != nullptr)
            {
                m_subWallets->convertSyncTimestampToHeight(m_startTimestamp, m_startHeight);
            }
        }

        return false;
    }
    /* If we get no blocks, we are fully synced.
       (Or timed out/failed to get blocks)
       Sleep a bit so we don't spam the daemon. */
    else if (!success || blocks.empty())
    {
        /* We may have also failed because we requested
           more data than could be returned in a reasonable
           amount of time, so we'll back off a little bit */
        m_daemon->decreaseRequestedBlockCount();

        Logger::logger.log("Zero blocks received from daemon, possibly fully synced", Logger::DEBUG, {Logger::SYNC});

        return false;
    }

    /* If we received data back, we'll make sure we're back
       to running at full speed in case we backed off a little
       bit before */
    m_daemon->resetRequestedBlockCount();

    /* Timestamp is transient and can change - block height is constant. */
    if (m_startTimestamp != 0)
    {
        m_startTimestamp = 0;
        m_startHeight = blocks.front().blockHeight;

        m_subWallets->convertSyncTimestampToHeight(m_startTimestamp, m_startHeight);

        /* NOTE: this used to clamp m_startHeight up to the prune floor. That
           skipped the whole pruned range on a timestamp import: the daemon
           takes the greater of our start height and the checkpoint, so raising
           the start height to the floor after the first batch of pruned blocks
           meant every height between the import point and the floor was never
           requested, and no fork was detected because the wallet only ever
           moved forwards. The floor is honoured by the daemon, which serves the
           pruned range in order, so the wallet simply follows the blocks. */
    }

    std::stringstream stream;

    stream << "Downloaded " << blocks.size() << " blocks from daemon, [" << blocks.front().blockHeight << ", "
           << blocks.back().blockHeight << "]";

    Logger::logger.log(stream.str(), Logger::DEBUG, {Logger::SYNC});

    std::vector<std::tuple<WalletTypes::WalletBlockInfo, uint32_t>> blocksWithIndex;

    for (const auto &block : blocks)
    {
        blocksWithIndex.push_back({block, m_arrivalIndex++});
    }

    m_storedBlocks.push_back_n(blocksWithIndex.begin(), blocksWithIndex.end());

    return true;
}

void BlockDownloader::fromJSON(const JSONObject &j, const uint64_t startHeight, const uint64_t startTimestamp)
{
    m_synchronizationStatus.fromJSON(j);
    m_startHeight = startHeight;
    m_startTimestamp = startTimestamp;
}

void BlockDownloader::toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
{
    m_synchronizationStatus.toJSON(writer);
}

void BlockDownloader::setSubWallets(const std::shared_ptr<SubWallets> subWallets)
{
    m_subWallets = subWallets;
}

void BlockDownloader::initializeAfterLoad(const std::shared_ptr<Nigel> daemon)
{
    m_daemon = daemon;
}
