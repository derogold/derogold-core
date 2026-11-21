// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

/////////////////////////////////////////////
#include <walletbackend/WalletSynchronizer.h>
/////////////////////////////////////////////

#include <common/StringTools.h>
#include <config/Config.h>
#include <config/WalletConfig.h>
#include <crypto/crypto.h>
#include <future>
#include <iostream>
#include <logger/Logger.h>
#include <utilities/ThreadSafeDeque.h>
#include <utilities/ThreadSafeQueue.h>
#include <utilities/Utilities.h>
#include <walletbackend/Constants.h>

///////////////////////////////////
/* CONSTRUCTORS / DECONSTRUCTORS */
///////////////////////////////////

/* Default constructor */
WalletSynchronizer::WalletSynchronizer(): m_shouldStop(false), m_startTimestamp(0), m_startHeight(0)
{
    unsigned int threads = std::thread::hardware_concurrency();

    /* Number of concurrent threads supported.
       If the value is not well defined or not computable, returns 0. */
    if (threads == 0)
    {
        threads = 1;
    }

    m_threadCount = threads;
}

/* Parameterized constructor */
WalletSynchronizer::WalletSynchronizer(
    const std::shared_ptr<Nigel> daemon,
    const uint64_t startHeight,
    const uint64_t startTimestamp,
    const Crypto::SecretKey privateViewKey,
    const std::shared_ptr<EventHandler> eventHandler,
    unsigned int threadCount):

    m_daemon(daemon),
    m_shouldStop(false),
    m_startHeight(startHeight),
    m_startTimestamp(startTimestamp),
    m_privateViewKey(privateViewKey),
    m_eventHandler(eventHandler),
    m_blockDownloader(daemon, nullptr, startHeight, startTimestamp)
{
    if (threadCount == 0)
    {
        threadCount = 1;
    }

    m_threadCount = threadCount;
}

/* Move constructor */
WalletSynchronizer::WalletSynchronizer(WalletSynchronizer &&old)
{
    /* Call the move assignment operator */
    *this = std::move(old);
}

/* Move assignment operator */
WalletSynchronizer &WalletSynchronizer::operator=(WalletSynchronizer &&old)
{
    /* Stop any running threads */
    stop();

    m_syncThread = std::move(old.m_syncThread);

    m_startTimestamp = std::move(old.m_startTimestamp);
    m_startHeight = std::move(old.m_startHeight);

    m_privateViewKey = std::move(old.m_privateViewKey);

    m_eventHandler = std::move(old.m_eventHandler);

    m_daemon = std::move(old.m_daemon);

    m_blockDownloader = std::move(old.m_blockDownloader);

    m_subWallets = std::move(old.m_subWallets);

    m_blockProcessingQueue = std::move(old.m_blockProcessingQueue);

    m_processedBlocks = std::move(old.m_processedBlocks);

    m_threadCount = std::move(old.m_threadCount);

    m_forkCount = old.m_forkCount.load();
    m_lastForkHeight = old.m_lastForkHeight.load();
    m_lastForkDepth = old.m_lastForkDepth.load();

    return *this;
}

/* Deconstructor */
WalletSynchronizer::~WalletSynchronizer()
{
    stop();
}

/////////////////////
/* CLASS FUNCTIONS */
/////////////////////

void WalletSynchronizer::mainLoop()
{
    auto lastCheckedLockedTransactions = std::chrono::system_clock::now();

    /* Tracks how many blocks have been pushed to m_blockProcessingQueue but
       not yet committed via completeBlockProcessing. fetchBlocks() does NOT
       remove blocks from the internal store, so we must guard against pushing
       the same blocks again on the next iteration. */
    size_t pendingBlocks = 0;

    while (!m_shouldStop)
    {
        /* Only fetch a new batch when the previous one is fully committed.
           The downloader prefetches into its own buffer independently, so
           the next batch is usually ready immediately. */
        if (pendingBlocks == 0)
        {
            const auto blocks = m_blockDownloader.fetchBlocks(Constants::BLOCK_PROCESSING_CHUNK);

            if (!blocks.empty())
            {
                pendingBlocks = blocks.size();
                m_blockProcessingQueue.push_back_n(blocks.begin(), blocks.end());
                m_haveBlocksToProcess.notify_all();
            }
        }

        if (pendingBlocks > 0)
        {
            /* Wait for ALL pending blocks to arrive in m_processedBlocks before
               draining. dropBlock() calls pop_front() on m_storedBlocks, which
               assumes blocks are dropped in strict arrival order. Draining only
               when the full batch is present lets the priority queue re-order any
               out-of-order worker results before we commit them.
               Workers call notify_all() immediately after each push, so we wake
               up as soon as the last one arrives. The 100ms timeout guards against
               missed notifications. */
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_haveProcessedBlocksToHandle.wait_for(lock, std::chrono::milliseconds(100), [&] {
                    return m_shouldStop || m_processedBlocks.size() >= pendingBlocks;
                });
            }

            /* Drain the full batch in arrival order via the priority queue. */
            if (!m_shouldStop && m_processedBlocks.size() >= pendingBlocks)
            {
                while (!m_shouldStop && pendingBlocks > 0)
                {
                    const auto [block, ourInputs, arrivalIndex] = m_processedBlocks.top_and_remove();

                    if (m_shouldStop)
                    {
                        break;
                    }

                    completeBlockProcessing(block, ourInputs);
                    pendingBlocks--;
                }
            }
        }
        else if (getCurrentScanHeight() >= m_daemon->localDaemonBlockCount())
        {
            /* Fully synced — check mempool periodically and sleep. */
            const auto now = std::chrono::system_clock::now();
            const auto timeDiff = now - lastCheckedLockedTransactions;

            /* Not a viewwallet and haven't checked transactions in last 15 secs */
            if (!m_subWallets->isViewWallet() && timeDiff > std::chrono::seconds(15))
            {
                checkLockedTransactions();
                lastCheckedLockedTransactions = now;
            }

            Utilities::sleepUnlessStopping(std::chrono::seconds(1), m_shouldStop);
        }
        else
        {
            /* Downloader hasn't produced blocks yet — brief pause. */
            Utilities::sleepUnlessStopping(std::chrono::milliseconds(100), m_shouldStop);
        }

        if (m_shouldStop)
        {
            break;
        }
    }
}

void WalletSynchronizer::blockProcessingThread()
{
    /* Take the max chunk size, split by the threads, divided by 2. So in
       theory, each thread processes 2 chunks. This is to decrease locking,
       while also trying to stop slower threads from delaying the system. */
    size_t chunkSize = Constants::BLOCK_PROCESSING_CHUNK / m_threadCount / 2;

    if (chunkSize == 0)
    {
        chunkSize = 1;
    }

    /* No point splitting into chunks if we're only using 1 thread */
    if (m_threadCount == 1)
    {
        chunkSize = Constants::BLOCK_PROCESSING_CHUNK;
    }

    std::vector<SemiProcessedBlock> processedBlocks;

    while (!m_shouldStop)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            /* Wait for blocks to be available.
               This is a timed wait on purpose. Producers push to the queue and
               call notify without holding this mutex, so a notification that
               lands between our predicate check and the wait is lost. With an
               untimed wait that lost wakeup parked the worker forever, and the
               main loop then spun waiting for a batch that was never processed,
               stalling sync until the wallet was reopened. Waking periodically
               costs nothing and makes a missed notification self-correcting. */
            m_haveBlocksToProcess.wait_for(lock, std::chrono::milliseconds(100), [&] {
                if (m_shouldStop)
                {
                    return true;
                }

                return m_blockProcessingQueue.size() > 0;
            });

            if (m_shouldStop)
            {
                return;
            }
        }

        auto chunk = m_blockProcessingQueue.front_n_and_remove(chunkSize);

        /* Process blocks while we've got more to process */
        while (!chunk.empty() && !m_shouldStop)
        {
            for (const auto &[block, arrivalIndex] : chunk)
            {
                Logger::logger.log(
                    "Processing block " + std::to_string(block.blockHeight), Logger::DEBUG, {Logger::SYNC});

                auto ourInputs = processBlockOutputs(block);

                /* Skip global index lookups for blocks below the prune floor.
                   The daemon has deleted the raw transaction data for these
                   heights, so /get_o_indexes will fail.  The prunedItems from
                   the daemon still let us detect incoming outputs (balance),
                   but we cannot resolve global indexes for spending until the
                   wallet re-syncs against a non-pruned node. */
                const uint64_t pruneFloor = m_blockDownloader.getPruneFloor();
                const bool isPrunedBlock = (pruneFloor > 0 && block.blockHeight < pruneFloor);

                std::unordered_map<Crypto::Hash, std::vector<uint64_t>> globalIndexes;

                for (auto &[publicKey, input] : ourInputs)
                {
                    if (!m_subWallets->isViewWallet() && !input.globalOutputIndex)
                    {
                        /* Pruned blocks: daemon can't provide global indexes.
                           Leave as nullopt — getSpendableInputs will filter
                           these out until re-synced on a non-pruned node. */
                        if (isPrunedBlock)
                        {
                            continue;
                        }

                        if (globalIndexes.empty())
                        {
                            globalIndexes = getGlobalIndexes(block.blockHeight);
                        }

                        auto it = globalIndexes.find(input.parentTransactionHash);

                        /* Daemon returns indexes for hashes in a range. If we don't
                           find our hash, either the chain has forked, or the daemon
                           is faulty.

                           Also need to check there are enough indexes for the one we want */
                        constexpr size_t MAX_GLOBAL_INDEX_ATTEMPTS = 6;

                        size_t attempts = 0;

                        while ((it == globalIndexes.end() || it->second.size() <= input.transactionIndex)
                               && attempts < MAX_GLOBAL_INDEX_ATTEMPTS)
                        {
                            if (m_shouldStop)
                            {
                                return;
                            }

                            attempts++;

                            Logger::logger.log(
                                "Warning: Failed to get correct global indexes from daemon (attempt "
                                    + std::to_string(attempts) + " of "
                                    + std::to_string(MAX_GLOBAL_INDEX_ATTEMPTS) + ")."
                                    "\nThe daemon may have gone offline or the chain may have just forked.",
                                Logger::FATAL,
                                {Logger::SYNC, Logger::DAEMON});

                            Utilities::sleepUnlessStopping(std::chrono::seconds(5), m_shouldStop);

                            if (m_shouldStop)
                            {
                                return;
                            }

                            globalIndexes = getGlobalIndexes(block.blockHeight);

                            it = globalIndexes.find(input.parentTransactionHash);
                        }

                        /* Give up rather than retrying forever. This loop used to
                           spin until shutdown, which is exactly what happens after
                           a reorg: our transaction is no longer on the chain, so
                           the daemon will never return an index for it, and the
                           batch never completed — sync stopped dead until the
                           wallet was reopened.

                           Leaving the index unresolved lets the block commit. The
                           input is then filtered out of spendable inputs, and if
                           this really was a fork the daemon will resend this height
                           and the fork path will roll the block back and rescan it. */
                        if (it == globalIndexes.end() || it->second.size() <= input.transactionIndex)
                        {
                            Logger::logger.log(
                                "Giving up on global indexes for transaction "
                                    + Common::podToHex(input.parentTransactionHash) + " in block "
                                    + std::to_string(block.blockHeight)
                                    + ". This input cannot be spent until the wallet is rescanned.",
                                Logger::FATAL,
                                {Logger::SYNC, Logger::DAEMON});

                            continue;
                        }

                        input.globalOutputIndex = it->second[input.transactionIndex];
                    }
                }

                processedBlocks.push_back({block, ourInputs, arrivalIndex});
            }

            chunk = m_blockProcessingQueue.front_n_and_remove(chunkSize);
        }

        /* Push our processed blocks */
        if (!processedBlocks.empty())
        {
            /* Store this chunks worth of blocks */
            m_processedBlocks.push_n(processedBlocks.begin(), processedBlocks.end());

            /* Notify the parent thread we've pushed data to the queue */
            m_haveProcessedBlocksToHandle.notify_all();

            /* Empty the processed blocks */
            processedBlocks.clear();
        }

        /* Then go back to waiting for more data */
    }
}

std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>>
    WalletSynchronizer::processBlockOutputs(const WalletTypes::WalletBlockInfo &block) const
{
    std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> inputs;

    if (!Config::config.wallet.skipCoinbaseTransactions && block.coinbaseTransaction)
    {
        const auto newInputs = processTransactionOutputs(*(block.coinbaseTransaction), block.blockHeight);

        inputs.insert(inputs.end(), newInputs.begin(), newInputs.end());
    }

    for (const auto &tx : block.transactions)
    {
        const auto newInputs = processTransactionOutputs(tx, block.blockHeight);

        inputs.insert(inputs.end(), newInputs.begin(), newInputs.end());
    }

    return inputs;
}

void WalletSynchronizer::completeBlockProcessing(
    const WalletTypes::WalletBlockInfo &block,
    const std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> &ourInputs)
{
    const uint64_t walletHeight = m_blockDownloader.getHeight();

    /* NOTE: there used to be a guard here that skipped any re-sent block below
       the prune floor instead of resolving it as a fork. It was wrong twice
       over: it took the fork path away from real reorgs whose replacement
       blocks fell below the floor, leaving orphaned transactions in the wallet,
       and its skip branch still dropped the block, which walked the wallet
       height backwards so the following block was reprocessed and its inputs
       stored a second time. Re-processing a block the wallet already has is
       already handled correctly and idempotently by the fork path below. */

    /* Chain forked, invalidate previous transactions */
    if (walletHeight >= block.blockHeight && block.blockHeight != 0)
    {
        const uint64_t depth = walletHeight - block.blockHeight + 1;

        /* The daemon re-sending a block the wallet already holds, unchanged,
           is not a reorg. It still goes through the removal below, which is
           what makes reprocessing it idempotent, but it is not worth warning
           about and must not be counted as a chain reorganisation - that count
           exists to tell somebody their confirmed transactions were withdrawn,
           and here nothing was. */
        const auto existing = m_blockDownloader.getHashAtHeight(block.blockHeight);

        const bool sameBlock = existing && *existing == block.blockHash;

        if (sameBlock)
        {
            Logger::logger.log(
                "Daemon re-sent block " + std::to_string(block.blockHeight) + ", reprocessing it",
                Logger::DEBUG,
                {Logger::SYNC});
        }
        else
        {
            /* A warning, not information. Every transaction from here up is
               about to be removed, some of which the wallet has already
               reported as confirmed, and the depth is the part worth seeing: a
               block or two is routine, thousands means something is wrong with
               where the wallet is being told to resume from. */
            Logger::logger.log(
                "Blockchain forked, resolving " + std::to_string(depth) + " block"
                    + (depth == 1 ? "" : "s") + " (old height: " + std::to_string(walletHeight)
                    + ", new height: " + std::to_string(block.blockHeight) + ")",
                Logger::WARNING,
                {Logger::SYNC});

            m_forkCount++;
            m_lastForkHeight = block.blockHeight;
            m_lastForkDepth = depth;

            /* A replacement block should attach to the block below it, which
               is one the wallet keeps. If it says otherwise, the daemon is
               rebuilding this wallet from a chain it has no part of - worth
               saying out loud, because the wallet is about to discard
               confirmed transactions on that say-so.

               Reported rather than refused: the wallet cannot tell a hostile
               daemon from a legitimately very deep reorg, and refusing to
               follow would wedge sync in a case where following is correct.
               Absent means the daemon did not send one, never a mismatch. */
            if (block.blockPrevHash && block.blockHeight > 0)
            {
                const auto parent = m_blockDownloader.getHashAtHeight(block.blockHeight - 1);

                if (parent && !(*parent == *block.blockPrevHash))
                {
                    std::stringstream stream;

                    stream << "Block " << block.blockHeight << " does not attach to the chain this wallet holds: "
                           << "it follows " << *block.blockPrevHash << ", but this wallet has " << *parent
                           << " at height " << block.blockHeight - 1
                           << ". The daemon is serving a different chain.";

                    Logger::logger.log(stream.str(), Logger::WARNING, {Logger::SYNC, Logger::DAEMON});
                }
            }
        }

        removeForkedTransactions(block.blockHeight);

        /* The blocks the wallet knew from here up are on a branch that no
           longer exists. Left in place they are offered to the daemon as
           resume points it cannot find, and they sit in front of the ones it
           can, so each following request starts further back than it needs
           to. */
        m_blockDownloader.forgetBlocksFrom(block.blockHeight);
    }

    /* Prune old inputs that are out of our 'confirmation' window */
    if (block.blockHeight % Constants::PRUNE_SPENT_INPUTS_INTERVAL == 0
        && block.blockHeight > Constants::PRUNE_SPENT_INPUTS_INTERVAL)
    {
        m_subWallets->pruneSpentInputs(block.blockHeight - Constants::PRUNE_SPENT_INPUTS_INTERVAL);
    }

    BlockScanTmpInfo blockScanInfo = processBlockTransactions(block, ourInputs);

    for (const auto &tx : blockScanInfo.transactionsToAdd)
    {
        std::stringstream stream;

        stream << "Adding transaction: " << tx.hash;

        Logger::logger.log(stream.str(), Logger::INFO, {Logger::SYNC, Logger::TRANSACTIONS});

        m_subWallets->addTransaction(tx);
        m_eventHandler->onTransaction.fire(tx);
    }

    for (const auto &[publicKey, input] : blockScanInfo.inputsToAdd)
    {
        std::stringstream stream;

        stream << "Adding input: " << input.key;

        Logger::logger.log(stream.str(), Logger::INFO, {Logger::SYNC});

        m_subWallets->storeTransactionInput(publicKey, input);
    }

    /* The input has been spent, discard the key image so we
       don't double spend it */
    for (const auto &[publicKey, keyImage] : blockScanInfo.keyImagesToMarkSpent)
    {
        std::stringstream stream;

        stream << "Marking key image: " << keyImage << " as spent";

        Logger::logger.log(stream.str(), Logger::INFO, {Logger::SYNC});

        m_subWallets->markInputAsSpent(keyImage, publicKey, block.blockHeight);
    }

    /* Make sure to do this at the end, once the transactions are fully
       processed! Otherwise, we could miss a transaction depending upon
       when we save */
    m_blockDownloader.dropBlock(block.blockHeight, block.blockHash);

    if (block.blockHeight >= m_daemon->networkBlockCount())
    {
        m_eventHandler->onSynced.fire(block.blockHeight);
    }

    Logger::logger.log("Finished processing block " + std::to_string(block.blockHeight), Logger::DEBUG, {Logger::SYNC});
}

BlockScanTmpInfo WalletSynchronizer::processBlockTransactions(
    const WalletTypes::WalletBlockInfo &block,
    const std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> &inputs) const
{
    BlockScanTmpInfo txData;

    if (!Config::config.wallet.skipCoinbaseTransactions)
    {
        const auto tx = processCoinbaseTransaction(block, inputs);

        if (tx)
        {
            txData.transactionsToAdd.push_back(*tx);
        }
    }

    for (const auto &rawTX : block.transactions)
    {
        const auto [tx, keyImagesToMarkSpent] = processTransaction(block, inputs, rawTX);

        if (tx)
        {
            txData.transactionsToAdd.push_back(*tx);

            txData.keyImagesToMarkSpent.insert(
                txData.keyImagesToMarkSpent.end(), keyImagesToMarkSpent.begin(), keyImagesToMarkSpent.end());
        }
    }

    txData.inputsToAdd = inputs;

    return txData;
}

std::optional<WalletTypes::Transaction> WalletSynchronizer::processCoinbaseTransaction(
    const WalletTypes::WalletBlockInfo &block,
    const std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> &inputs) const
{
    const auto tx = *(block.coinbaseTransaction);

    std::unordered_map<Crypto::PublicKey, int64_t> transfers;

    std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> relevantInputs;

    std::copy_if(inputs.begin(), inputs.end(), std::back_inserter(relevantInputs), [&](const auto input) {
        return std::get<1>(input).parentTransactionHash == tx.hash;
    });

    for (const auto &[publicSpendKey, input] : relevantInputs)
    {
        transfers[publicSpendKey] += input.amount;
    }

    if (!transfers.empty())
    {
        const uint64_t fee = 0;
        const bool isCoinbaseTransaction = true;
        const std::string paymentID;

        return WalletTypes::Transaction(
            transfers,
            tx.hash,
            fee,
            block.blockTimestamp,
            block.blockHeight,
            paymentID,
            tx.unlockTime,
            isCoinbaseTransaction);
    }

    return std::nullopt;
}

std::tuple<std::optional<WalletTypes::Transaction>, std::vector<std::tuple<Crypto::PublicKey, Crypto::KeyImage>>>
    WalletSynchronizer::processTransaction(
        const WalletTypes::WalletBlockInfo &block,
        const std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> &inputs,
        const WalletTypes::RawTransaction &tx) const
{
    std::unordered_map<Crypto::PublicKey, int64_t> transfers;

    std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> relevantInputs;

    std::copy_if(inputs.begin(), inputs.end(), std::back_inserter(relevantInputs), [&](const auto input) {
        return std::get<1>(input).parentTransactionHash == tx.hash;
    });

    for (const auto &[publicSpendKey, input] : relevantInputs)
    {
        transfers[publicSpendKey] += input.amount;
    }

    std::vector<std::tuple<Crypto::PublicKey, Crypto::KeyImage>> spentKeyImages;

    for (const auto &input : tx.keyInputs)
    {
        const auto [found, publicSpendKey] = m_subWallets->getKeyImageOwner(input.keyImage);

        if (found)
        {
            transfers[publicSpendKey] -= input.amount;
            spentKeyImages.emplace_back(publicSpendKey, input.keyImage);
        }
    }

    if (!transfers.empty())
    {
        uint64_t fee = 0;

        for (const auto &input : tx.keyInputs)
        {
            fee += input.amount;
        }

        for (const auto &output : tx.keyOutputs)
        {
            fee -= output.amount;
        }

        const bool isCoinbaseTransaction = false;

        const auto newTX = WalletTypes::Transaction(
            transfers,
            tx.hash,
            fee,
            block.blockTimestamp,
            block.blockHeight,
            tx.paymentID,
            tx.unlockTime,
            isCoinbaseTransaction);

        return {newTX, spentKeyImages};
    }

    return {std::nullopt, {}};
}

std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> WalletSynchronizer::processTransactionOutputs(
    const WalletTypes::RawCoinbaseTransaction &rawTX,
    const uint64_t blockHeight) const
{
    std::vector<std::tuple<Crypto::PublicKey, WalletTypes::TransactionInput>> inputs;

    Crypto::KeyDerivation derivation;

    Crypto::generate_key_derivation(rawTX.transactionPublicKey, m_privateViewKey, derivation);

    const std::vector<Crypto::PublicKey> spendKeys = m_subWallets->m_publicSpendKeys;

    uint64_t outputIndex = 0;

    for (const auto &output : rawTX.keyOutputs)
    {
        Crypto::PublicKey derivedSpendKey;

        Crypto::underive_public_key(derivation, outputIndex, output.key, derivedSpendKey);

        /* See if the derived spend key matches any of our spend keys */
        const auto ourSpendKey = std::find(spendKeys.begin(), spendKeys.end(), derivedSpendKey);

        /* If it does, the transaction belongs to us */
        if (ourSpendKey != spendKeys.end())
        {
            /* We need to fill in the key image of the transaction input -
               we'll let the subwallet do this since we need the private spend
               key. We use the key images to detect outgoing transactions,
               and we use the transaction inputs to make transactions ourself */
            const Crypto::KeyImage keyImage =
                m_subWallets->getTxInputKeyImage(derivedSpendKey, derivation, outputIndex);

            const uint64_t spendHeight = 0;

            const WalletTypes::TransactionInput input({keyImage,
                                                       output.amount,
                                                       blockHeight,
                                                       rawTX.transactionPublicKey,
                                                       outputIndex,
                                                       output.globalOutputIndex,
                                                       output.key,
                                                       spendHeight,
                                                       rawTX.unlockTime,
                                                       rawTX.hash});

            inputs.emplace_back(derivedSpendKey, input);
        }

        outputIndex++;
    }

    return inputs;
}

/* When we get the global indexes, we pass in a range of blocks, to obscure
   which transactions we are interested in - the ones that belong to us.
   To do this, we get the global indexes for all transactions in a range.

   For example, if we want the global indexes for a transaction in block
   17, we get all the indexes from block 10 to block 20. */
std::unordered_map<Crypto::Hash, std::vector<uint64_t>>
    WalletSynchronizer::getGlobalIndexes(const uint64_t blockHeight) const
{
    uint64_t startHeight = Utilities::getLowerBound(blockHeight, Constants::GLOBAL_INDEXES_OBSCURITY);

    uint64_t endHeight = Utilities::getUpperBound(blockHeight, Constants::GLOBAL_INDEXES_OBSCURITY);

    const auto [success, indexes] = m_daemon->getGlobalIndexesForRange(startHeight, endHeight);

    if (!success)
    {
        return {};
    }

    return indexes;
}

void WalletSynchronizer::checkLockedTransactions()
{
    /* Get the hashes of any locked tx's we have */
    const auto lockedTxHashes = m_subWallets->getLockedTransactionsHashes();

    if (lockedTxHashes.size() != 0)
    {
        Logger::logger.log("Checking locked transactions", Logger::DEBUG, {Logger::TRANSACTIONS});

        /* Transactions that are in the pool - we'll query these again
           next time to see if they have moved */
        std::unordered_set<Crypto::Hash> transactionsInPool;

        /* Transactions that are in a block - don't need to do anything,
           when we get to the block they will be processed and unlocked. */
        std::unordered_set<Crypto::Hash> transactionsInBlock;

        /* Transactions that the daemon doesn't know about - returned to
           our wallet for timeout or other reason */
        std::unordered_set<Crypto::Hash> cancelledTransactions;

        /* Get the status of the locked transactions */
        bool success = m_daemon->getTransactionsStatus(
            lockedTxHashes, transactionsInPool, transactionsInBlock, cancelledTransactions);

        /* Couldn't get info from the daemon, try again later */
        if (!success)
        {
            Logger::logger.log(
                "Failed to get locked transaction information from daemon",
                Logger::WARNING,
                {Logger::TRANSACTIONS, Logger::DAEMON});

            return;
        }

        /* If some transactions have been cancelled, remove them, and their
           inputs */
        if (cancelledTransactions.size() != 0)
        {
            m_subWallets->removeCancelledTransactions(cancelledTransactions);
        }
    }
}

/* Launch the worker thread in the background. It's safest to do this in a
   seperate function, so everything in the constructor gets initialized,
   and if we do any inheritance, things don't go awry. */
void WalletSynchronizer::start()
{
    Logger::logger.log("Starting sync process", Logger::DEBUG, {Logger::SYNC});

    /* Reinit any vars which may have changed if we previously called stop() */
    m_shouldStop = false;

    if (m_daemon == nullptr)
    {
        throw std::runtime_error("Daemon has not been initialized!");
    }

    m_blockDownloader.start();
    m_blockProcessingQueue.start();
    m_processedBlocks.start();

    m_syncThread = std::thread(&WalletSynchronizer::mainLoop, this);

    m_syncThreads.clear();

    for (unsigned int i = 0; i < m_threadCount; i++)
    {
        m_syncThreads.push_back(std::thread(&WalletSynchronizer::blockProcessingThread, this));
    }
}

void WalletSynchronizer::stop()
{
    Logger::logger.log("Stopping sync process", Logger::DEBUG, {Logger::SYNC});

    /* Tell the threads to stop */
    m_shouldStop = true;

    /* Tell the block downloader to stop and wait for it */
    m_blockDownloader.stop();
    m_blockProcessingQueue.stop();
    m_processedBlocks.stop();

    m_haveBlocksToProcess.notify_all();
    m_haveProcessedBlocksToHandle.notify_all();

    m_blockProcessingQueue.clear();
    m_processedBlocks.clear();

    /* Wait for the block downloader thread to finish (if applicable) */
    if (m_syncThread.joinable())
    {
        m_syncThread.join();
    }

    /* Wait for each child thread to finish */
    for (auto &thread : m_syncThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void WalletSynchronizer::reset(uint64_t startHeight)
{
    /* Reset start height / timestamp */
    m_startHeight = startHeight;
    m_startTimestamp = 0;

    /* Discard downloaded blocks and sync status */
    m_blockDownloader = BlockDownloader(m_daemon, m_subWallets, m_startHeight, m_startTimestamp);

    /* Need to call start in your calling code - We don't call it here so
       you can schedule the start correctly */
}

/* Remove any transactions at this height or above, they were on a forked
   chain */
void WalletSynchronizer::removeForkedTransactions(const uint64_t forkHeight)
{
    m_subWallets->removeForkedTransactions(forkHeight);
}

void WalletSynchronizer::initializeAfterLoad(
    const std::shared_ptr<Nigel> daemon,
    const std::shared_ptr<EventHandler> eventHandler,
    unsigned int threadCount)
{
    m_daemon = daemon;
    m_eventHandler = eventHandler;
    m_blockDownloader.initializeAfterLoad(m_daemon);

    if (threadCount == 0)
    {
        threadCount = 1;
    }

    m_threadCount = threadCount;
}

uint64_t WalletSynchronizer::getCurrentScanHeight() const
{
    return m_blockDownloader.getHeight();
}

std::tuple<uint64_t, uint64_t, uint64_t> WalletSynchronizer::getForkInfo() const
{
    return {m_forkCount.load(), m_lastForkHeight.load(), m_lastForkDepth.load()};
}

uint64_t WalletSynchronizer::getPruneFloor() const
{
    return m_blockDownloader.getPruneFloor();
}

void WalletSynchronizer::swapNode(const std::shared_ptr<Nigel> daemon)
{
    m_daemon = daemon;

    /* The new node has its own pruning state, so forget what the previous one
       reported rather than applying its floor to a node that may hold the full
       chain. */
    m_blockDownloader.clearPruneFloor();
}

void WalletSynchronizer::fromJSON(const JSONObject &j)
{
    m_startTimestamp = getUint64FromJSON(j, "startTimestamp");
    m_startHeight = getUint64FromJSON(j, "startHeight");
    m_privateViewKey.fromString(getStringFromJSON(j, "privateViewKey"));

    m_blockDownloader.fromJSON(getObjectFromJSON(j, "transactionSynchronizerStatus"), m_startHeight, m_startTimestamp);
}

void WalletSynchronizer::toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
{
    writer.StartObject();

    writer.Key("transactionSynchronizerStatus");
    m_blockDownloader.toJSON(writer);

    writer.Key("startTimestamp");
    writer.Uint64(m_startTimestamp);

    writer.Key("startHeight");
    writer.Uint64(m_startHeight);

    writer.Key("privateViewKey");
    m_privateViewKey.toJSON(writer);

    writer.EndObject();
}

void WalletSynchronizer::setSubWallets(const std::shared_ptr<SubWallets> subWallets)
{
    m_subWallets = subWallets;
    m_blockDownloader.setSubWallets(m_subWallets);
}
