// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "BlockchainUtils.h"
#include "crypto/hash.h"

#include <common/CryptoNoteTools.h>
#include <common/StringTools.h>
#include <common/ShuffleGenerator.h>
#include <common/TransactionExtra.h>
#include <cryptonotecore/BlockchainStorage.h>
#include <cryptonotecore/CryptoNoteBasicImpl.h>
#include <cryptonotecore/DatabaseBlockchainCache.h>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <map>
#include <optional>
#include <set>

namespace CryptoNote
{
    namespace
    {
        const uint32_t ONE_DAY_SECONDS = 60 * 60 * 24;

        const CachedBlockInfo NULL_CACHED_BLOCK_INFO {Constants::NULL_HASH, 0, 0, 0, 0, 0};

        bool requestPackedOutputs(IBlockchainCache::Amount amount,
                                  Common::ArrayView<uint32_t> globalIndexes,
                                  IDataBase &database,
                                  std::vector<PackedOutIndex> &result)
        {
            BlockchainReadBatch readBatch;
            result.reserve(result.size() + globalIndexes.getSize());

            for (auto globalIndex : globalIndexes)
            {
                readBatch.requestKeyOutputGlobalIndexForAmount(amount, globalIndex);
            }

            auto dbResult = database.read(readBatch);
            if (dbResult)
            {
                return false;
            }

            try
            {
                auto readResult = readBatch.extractResult();
                const auto &packedOutsMap = readResult.getKeyOutputGlobalIndexesForAmounts();
                for (auto globalIndex : globalIndexes)
                {
                    result.push_back(packedOutsMap.at(std::make_pair(amount, globalIndex)));
                }
            }
            catch (std::exception &)
            {
                return false;
            }

            return true;
        }

        bool requestTransactionHashesForGlobalOutputIndexes(const std::vector<PackedOutIndex> &packedOuts,
                                                            IDataBase &database,
                                                            std::vector<Crypto::Hash> &transactionHashes)
        {
            BlockchainReadBatch readHashesBatch;

            std::set<uint32_t> blockIndexes;
            std::for_each(packedOuts.begin(),
                          packedOuts.end(),
                          [&blockIndexes](PackedOutIndex out) { blockIndexes.insert(out.blockIndex); });
            std::for_each(blockIndexes.begin(),
                          blockIndexes.end(),
                          [&readHashesBatch](uint32_t blockIndex)
                          { readHashesBatch.requestTransactionHashesByBlock(blockIndex); });

            auto dbResult = database.read(readHashesBatch);
            if (dbResult)
            {
                return false;
            }

            auto readResult = readHashesBatch.extractResult();
            const auto &transactionHashesMap = readResult.getTransactionHashesByBlocks();

            if (transactionHashesMap.size() != blockIndexes.size())
            {
                return false;
            }

            transactionHashes.reserve(transactionHashes.size() + packedOuts.size());
            for (const auto &output : packedOuts)
            {
                if (output.transactionIndex >= transactionHashesMap.at(output.blockIndex).size())
                {
                    return false;
                }

                transactionHashes.push_back(transactionHashesMap.at(output.blockIndex)[output.transactionIndex]);
            }

            return true;
        }

        bool requestCachedTransactionInfos(const std::vector<Crypto::Hash> &transactionHashes,
                                           IDataBase &database,
                                           std::vector<CachedTransactionInfo> &result)
        {
            result.reserve(result.size() + transactionHashes.size());

            BlockchainReadBatch transactionsBatch;
            std::for_each(transactionHashes.begin(),
                          transactionHashes.end(),
                          [&transactionsBatch](const Crypto::Hash &hash)
                          { transactionsBatch.requestCachedTransaction(hash); });
            auto dbResult = database.read(transactionsBatch);
            if (dbResult)
            {
                return false;
            }

            auto readResult = transactionsBatch.extractResult();
            const auto &transactions = readResult.getCachedTransactions();
            if (transactions.size() != transactionHashes.size())
            {
                return false;
            }

            for (const auto &hash : transactionHashes)
            {
                result.push_back(transactions.at(hash));
            }

            return true;
        }

        // returns CachedTransactionInfos in the same or as packedOuts are
        /*
        bool requestCachedTransactionInfos(const std::vector<PackedOutIndex>& packedOuts, IDataBase& database,
        std::vector<CachedTransactionInfo>& result) { std::vector<Crypto::Hash> transactionHashes; if
        (!requestTransactionHashesForGlobalOutputIndexes(packedOuts, database, transactionHashes)) { return false;
          }

          return requestCachedTransactionInfos(transactionHashes, database, result);
        }
        */

        bool requestExtendedTransactionInfos(const std::vector<Crypto::Hash> &transactionHashes,
                                             IDataBase &database,
                                             std::vector<ExtendedTransactionInfo> &result)
        {
            result.reserve(result.size() + transactionHashes.size());

            BlockchainReadBatch transactionsBatch;
            std::for_each(transactionHashes.begin(),
                          transactionHashes.end(),
                          [&transactionsBatch](const Crypto::Hash &hash)
                          { transactionsBatch.requestCachedTransaction(hash); });
            auto dbResult = database.read(transactionsBatch);
            if (dbResult)
            {
                return false;
            }

            auto readResult = transactionsBatch.extractResult();
            const auto &transactions = readResult.getCachedTransactions();

            std::unordered_set<Crypto::Hash> uniqueTransactionHashes(transactionHashes.begin(),
                                                                     transactionHashes.end());
            if (transactions.size() != uniqueTransactionHashes.size())
            {
                return false;
            }

            for (const auto &hash : transactionHashes)
            {
                result.push_back(transactions.at(hash));
            }

            return true;
        }

        // returns ExtendedTransactionInfos in the same order as packedOuts are
        bool requestExtendedTransactionInfos(const std::vector<PackedOutIndex> &packedOuts,
                                             IDataBase &database,
                                             std::vector<ExtendedTransactionInfo> &result)
        {
            std::vector<Crypto::Hash> transactionHashes;
            if (!requestTransactionHashesForGlobalOutputIndexes(packedOuts, database, transactionHashes))
            {
                return false;
            }

            return requestExtendedTransactionInfos(transactionHashes, database, result);
        }

        uint64_t roundToMidnight(uint64_t timestamp)
        {
            if (timestamp > static_cast<uint64_t>(std::numeric_limits<time_t>::max()))
            {
                throw std::runtime_error("Timestamp is too big");
            }

            return static_cast<uint64_t>((timestamp / ONE_DAY_SECONDS) * ONE_DAY_SECONDS);
        }

        std::pair<std::optional<uint32_t>, bool> requestClosestBlockIndexByTimestamp(uint64_t timestamp,
                                                                                       IDataBase &database)
        {
            std::pair<std::optional<uint32_t>, bool> result = {{}, false};

            BlockchainReadBatch readBatch;
            readBatch.requestClosestTimestampBlockIndex(timestamp);
            auto dbResult = database.read(readBatch);
            if (dbResult)
            {
                return result;
            }

            result.second = true;
            auto readResult = readBatch.extractResult();
            if (readResult.getClosestTimestampBlockIndex().count(timestamp))
            {
                result.first = readResult.getClosestTimestampBlockIndex().at(timestamp);
            }

            return result;
        }

        bool requestRawBlock(IDataBase &database, uint32_t blockIndex, RawBlock &block)
        {
            auto batch = BlockchainReadBatch().requestRawBlock(blockIndex);

            auto error = database.read(batch);
            if (error)
            {
                // may be throw in all similiar functions???
                return false;
            }

            auto result = batch.extractResult();
            if (result.getRawBlocks().count(blockIndex) == 0)
            {
                return false;
            }

            block = result.getRawBlocks().at(blockIndex);
            return true;
        }

        Transaction extractTransaction(const RawBlock &block, uint32_t transactionIndex)
        {
            assert(transactionIndex < block.transactions.size() + 1);

            if (transactionIndex != 0)
            {
                Transaction transaction;
                bool r = fromBinaryArray(transaction, block.transactions[transactionIndex - 1]);
                if (r)
                {
                }
                assert(r);

                return transaction;
            }

            BlockTemplate blockTemplate;
            bool r = fromBinaryArray(blockTemplate, block.block);
            if (r)
            {
            }
            assert(r);

            return blockTemplate.baseTransaction;
        }

        size_t requestPaymentIdTransactionsCount(IDataBase &database, const Crypto::Hash &paymentId)
        {
            auto batch = BlockchainReadBatch().requestTransactionCountByPaymentId(paymentId);
            auto error = database.read(batch);

            if (error)
            {
                throw std::system_error(error, "Error while reading transactions count by payment id");
            }

            auto result = batch.extractResult();
            if (result.getTransactionCountByPaymentIds().count(paymentId) == 0)
            {
                return 0;
            }

            return result.getTransactionCountByPaymentIds().at(paymentId);
        }

        bool requestPaymentId(IDataBase &database, const Crypto::Hash &transactionHash, Crypto::Hash &paymentId)
        {
            std::vector<CachedTransactionInfo> cachedTransactions;

            if (!requestCachedTransactionInfos({transactionHash}, database, cachedTransactions))
            {
                return false;
            }

            if (cachedTransactions.empty())
            {
                return false;
            }

            RawBlock block;
            if (!requestRawBlock(database, cachedTransactions[0].blockIndex, block))
            {
                return false;
            }

            Transaction transaction = extractTransaction(block, cachedTransactions[0].transactionIndex);
            return getPaymentIdFromTxExtra(transaction.extra, paymentId);
        }

        uint32_t requestKeyOutputGlobalIndexesCountForAmount(IBlockchainCache::Amount amount, IDataBase &database)
        {
            auto batch = BlockchainReadBatch().requestKeyOutputGlobalIndexesCountForAmount(amount);
            auto dbError = database.read(batch);
            if (dbError)
            {
                throw std::system_error(dbError, "Cannot perform requestKeyOutputGlobalIndexesCountForAmount query");
            }

            auto result = batch.extractResult();

            if (result.getKeyOutputGlobalIndexesCountForAmounts().count(amount) != 0)
            {
                return result.getKeyOutputGlobalIndexesCountForAmounts().at(amount);
            }
            else
            {
                return 0;
            }
        }

        /* A random access iterator over the key outputs for one amount, each
           read from the database on dereference. Written out by hand in place
           of boost::iterator_facade; std::lower_bound and std::distance below
           are the only things that consume it. */
        class DbOutputConstIterator
        {
        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type = PackedOutIndex;
            using difference_type = std::ptrdiff_t;
            using pointer = const PackedOutIndex *;
            using reference = const PackedOutIndex &;

            DbOutputConstIterator(
                std::function<PackedOutIndex(IBlockchainCache::Amount amount, uint32_t globalOutputIndex)> retriever_,
                IBlockchainCache::Amount amount_,
                uint32_t globalOutputIndex_) :
                retriever(retriever_),
                amount(amount_),
                globalOutputIndex(globalOutputIndex_)
            {
            }

            reference operator*() const
            {
                cachedValue = retriever(amount, globalOutputIndex);
                return cachedValue;
            }

            pointer operator->() const
            {
                return &**this;
            }

            reference operator[](difference_type n) const
            {
                return *(*this + n);
            }

            DbOutputConstIterator &operator++()
            {
                ++globalOutputIndex;
                return *this;
            }

            DbOutputConstIterator operator++(int)
            {
                DbOutputConstIterator before = *this;
                ++*this;
                return before;
            }

            DbOutputConstIterator &operator--()
            {
                --globalOutputIndex;
                return *this;
            }

            DbOutputConstIterator operator--(int)
            {
                DbOutputConstIterator before = *this;
                --*this;
                return before;
            }

            DbOutputConstIterator &operator+=(difference_type n)
            {
                assert(n >= -static_cast<difference_type>(globalOutputIndex));
                globalOutputIndex += static_cast<uint32_t>(n);
                return *this;
            }

            DbOutputConstIterator &operator-=(difference_type n)
            {
                return *this += -n;
            }

            friend DbOutputConstIterator operator+(DbOutputConstIterator it, difference_type n)
            {
                return it += n;
            }

            friend DbOutputConstIterator operator+(difference_type n, DbOutputConstIterator it)
            {
                return it += n;
            }

            friend DbOutputConstIterator operator-(DbOutputConstIterator it, difference_type n)
            {
                return it -= n;
            }

            friend difference_type operator-(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return static_cast<difference_type>(a.globalOutputIndex)
                     - static_cast<difference_type>(b.globalOutputIndex);
            }

            friend bool operator==(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return a.globalOutputIndex == b.globalOutputIndex;
            }

            friend bool operator!=(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return !(a == b);
            }

            friend bool operator<(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return a.globalOutputIndex < b.globalOutputIndex;
            }

            friend bool operator>(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return b < a;
            }

            friend bool operator<=(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return !(b < a);
            }

            friend bool operator>=(const DbOutputConstIterator &a, const DbOutputConstIterator &b)
            {
                return !(a < b);
            }

        private:
            std::function<PackedOutIndex(IBlockchainCache::Amount amount, uint32_t globalOutputIndex)> retriever;

            IBlockchainCache::Amount amount;

            uint32_t globalOutputIndex;

            mutable PackedOutIndex cachedValue;
        };

        PackedOutIndex retrieveKeyOutput(IBlockchainCache::Amount amount,
                                         uint32_t globalOutputIndex,
                                         IDataBase &database)
        {
            BlockchainReadBatch batch;
            auto dbError = database.read(batch.requestKeyOutputGlobalIndexForAmount(amount, globalOutputIndex));
            if (dbError)
            {
                throw std::system_error(dbError, "Error during retrieving key output by global output index");
            }

            auto result = batch.extractResult();

            try
            {
                return result.getKeyOutputGlobalIndexesForAmounts().at(std::make_pair(amount, globalOutputIndex));
            }
            catch (std::exception &)
            {
                assert(false);
                throw std::runtime_error("Couldn't find key output for amount " + std::to_string(amount)
                                         + " with global output index " + std::to_string(globalOutputIndex));
            }
        }

        std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> getMinGlobalIndexesByAmount(
            const std::map<IBlockchainCache::Amount, std::vector<IBlockchainCache::GlobalOutputIndex>> &outputIndexes)
        {
            std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> minIndexes;
            for (const auto &kv : outputIndexes)
            {
                auto min = std::min_element(kv.second.begin(), kv.second.end());
                if (min == kv.second.end())
                {
                    continue;
                }

                minIndexes.emplace(kv.first, *min);
            }

            return minIndexes;
        }

        void mergeOutputsSplitBoundaries(
            std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> &dest,
            const std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> &src)
        {
            for (const auto &elem : src)
            {
                auto it = dest.find(elem.first);
                if (it == dest.end())
                {
                    dest.emplace(elem.first, elem.second);
                    continue;
                }

                if (it->second > elem.second)
                {
                    it->second = elem.second;
                }
            }
        }

        void cutTail(std::deque<CachedBlockInfo> &cache, size_t count)
        {
            if (count >= cache.size())
            {
                cache.clear();
                return;
            }

            cache.erase(std::next(cache.begin(), cache.size() - count), cache.end());
        }

        const std::string DB_VERSION_KEY = "db_scheme_version";

        class DatabaseVersionReadBatch : public IReadBatch
        {
        public:
            virtual ~DatabaseVersionReadBatch() {}

            virtual std::vector<std::string> getRawKeys() const override
            {
                return {DB_VERSION_KEY};
            }

            virtual void submitRawResult(const std::vector<std::string> &values,
                                         const std::vector<bool> &resultStates) override
            {
                assert(values.size() == 1);
                assert(resultStates.size() == values.size());

                if (!resultStates[0])
                {
                    return;
                }

                version = static_cast<uint32_t>(std::atoi(values[0].c_str()));
            }

            std::optional<uint32_t> getDbSchemeVersion()
            {
                return version;
            }

        private:
            std::optional<uint32_t> version;
        };

        class DatabaseVersionWriteBatch : public IWriteBatch
        {
        public:
            explicit DatabaseVersionWriteBatch(const uint32_t version) :
                schemeVersion(version)
            {
            }

            std::vector<std::pair<std::string, std::string>> extractRawDataToInsert() override
            {
                return {make_pair(DB_VERSION_KEY, std::to_string(schemeVersion))};
            }

            std::vector<std::string> extractRawKeysToRemove() override
            {
                return {};
            }

        private:
            uint32_t schemeVersion;
        };

        const uint32_t CURRENT_DB_SCHEME_VERSION = 2;

    } // namespace

    struct DatabaseBlockchainCache::ExtendedPushedBlockInfo
    {
        PushedBlockInfo pushedBlockInfo;
        uint64_t timestamp;
    };

    DatabaseBlockchainCache::DatabaseBlockchainCache(const Currency &curr,
                                                     IDataBase &dataBase,
                                                     IBlockchainCacheFactory &blockchainCacheFactory,
                                                     std::shared_ptr<Logging::ILogger> _logger,
                                                     const uint32_t liteHeight) :
        currency(curr),
        database(dataBase),
        blockchainCacheFactory(blockchainCacheFactory),
        logger(std::move(_logger), "DatabaseBlockchainCache"),
        liteHeight(liteHeight)
    {
        DatabaseVersionReadBatch readBatch;
        auto ec = database.read(readBatch);
        if (ec)
        {
            throw std::system_error(ec);
        }

        auto version = readBatch.getDbSchemeVersion();
        if (!version)
        {
            logger(Logging::DEBUGGING) << "DB scheme version not found, writing: " << CURRENT_DB_SCHEME_VERSION;

            DatabaseVersionWriteBatch writeBatch(CURRENT_DB_SCHEME_VERSION);
            auto writeError = database.write(writeBatch);
            if (writeError)
            {
                throw std::system_error(writeError);
            }
        }
        else
        {
            logger(Logging::DEBUGGING) << "Current db scheme version: " << *version;
        }

        if (getTopBlockIndex() == 0)
        {
            logger(Logging::DEBUGGING) << "top block index is null, add genesis block";
            addGenesisBlock(CachedBlock(currency.genesisBlock()));
        }
    }

    bool DatabaseBlockchainCache::checkDBSchemeVersion(IDataBase &database, std::shared_ptr<Logging::ILogger> _logger)
    {
        Logging::LoggerRef logger(_logger, "DatabaseBlockchainCache");

        DatabaseVersionReadBatch readBatch;
        auto ec = database.read(readBatch);
        if (ec)
        {
            throw std::system_error(ec);
        }

        auto version = readBatch.getDbSchemeVersion();
        if (!version)
        {
            // DB scheme version not found. Looks like it was just created.
            return true;
        }
        else if (*version < CURRENT_DB_SCHEME_VERSION)
        {
            logger(Logging::WARNING) << "DB scheme version is less than expected. Expected version "
                                     << CURRENT_DB_SCHEME_VERSION << ". Actual version " << *version
                                     << ". DB will be destroyed and recreated from blocks.bin file.";
            return false;
        }
        else if (*version > CURRENT_DB_SCHEME_VERSION)
        {
            logger(Logging::ERROR) << "DB scheme version is greater than expected. Expected version "
                                   << CURRENT_DB_SCHEME_VERSION << ". Actual version " << *version
                                   << ". Please update your software.";
            throw std::runtime_error("DB scheme version is greater than expected");
        }
        else
        {
            return true;
        }
    }

    void DatabaseBlockchainCache::deleteClosestTimestampBlockIndex(BlockchainWriteBatch &writeBatch,
                                                                   uint32_t splitBlockIndex)
    {
        auto batch = BlockchainReadBatch().requestCachedBlock(splitBlockIndex);
        auto blockResult = readDatabase(batch);
        auto timestamp = getCachedBlockInfo(splitBlockIndex).timestamp;

        auto midnight = roundToMidnight(timestamp);
        auto timestampResult = requestClosestBlockIndexByTimestamp(midnight, database);
        if (!timestampResult.second)
        {
            logger(Logging::ERROR)
                << "deleteClosestTimestampBlockIndex error: get closest timestamp block index, database read failed";
            throw std::runtime_error("Couldn't get closest timestamp block index");
        }

        assert(bool(timestampResult.first));

        auto blockIndex = *timestampResult.first;
        assert(splitBlockIndex >= blockIndex);

        if (splitBlockIndex != blockIndex)
        {
            midnight += ONE_DAY_SECONDS;
        }

        BlockchainReadBatch midnightBatch;
        while (readDatabase(midnightBatch.requestClosestTimestampBlockIndex(midnight))
                   .getClosestTimestampBlockIndex()
                   .count(midnight))
        {
            writeBatch.removeClosestTimestampBlockIndex(midnight);
            midnight += ONE_DAY_SECONDS;
        }

        logger(Logging::TRACE) << "deleted closest timestamp";
    }

    /*
     * This methods splits cache, upper part (ie blocks with indexes greater or equal to splitBlockIndex)
     * is copied to new BlockchainCache
     */
    std::unique_ptr<IBlockchainCache> DatabaseBlockchainCache::split(uint32_t splitBlockIndex)
    {
        assert(splitBlockIndex <= getTopBlockIndex());

        /* Splitting means undoing blocks, which needs the transaction records
           and the block-index-to-key-image lists that index-only heights never
           stored. The lite height sits far enough below the top that no honest
           reorg reaches here, so this is a corrupt or hostile chain rather than
           something to attempt and half finish. */
        if (isLiteIndexOnlyHeight(splitBlockIndex))
        {
            logger(Logging::ERROR) << "Refusing to split at index " << splitBlockIndex
                                   << ", below this lite node's full block height " << liteHeight
                                   << ". The data needed to undo those blocks was never stored.";

            throw std::runtime_error("Cannot split below the lite node height");
        }

        logger(Logging::DEBUGGING) << "split at index " << splitBlockIndex
                                   << " started, top block index: " << getTopBlockIndex();

        auto cache = blockchainCacheFactory.createBlockchainCache(currency, this, splitBlockIndex);

        using DeleteBlockInfo = std::tuple<uint32_t, Crypto::Hash, TransactionValidatorState, uint64_t>;
        std::vector<DeleteBlockInfo> deletingBlocks;

        BlockchainWriteBatch writeBatch;
        auto currentTop = getTopBlockIndex();
        for (uint32_t blockIndex = splitBlockIndex; blockIndex <= currentTop; ++blockIndex)
        {
            ExtendedPushedBlockInfo extendedInfo = getExtendedPushedBlockInfo(blockIndex);

            auto validatorState = extendedInfo.pushedBlockInfo.validatorState;
            logger(Logging::DEBUGGING) << "pushing block " << blockIndex << " to child segment";
            auto blockHash = pushBlockToAnotherCache(*cache, std::move(extendedInfo.pushedBlockInfo));

            deletingBlocks.emplace_back(blockIndex, blockHash, validatorState, extendedInfo.timestamp);
        }

        for (auto it = deletingBlocks.rbegin(); it != deletingBlocks.rend(); ++it)
        {
            auto blockIndex = std::get<0>(*it);
            auto blockHash = std::get<1>(*it);
            auto &validatorState = std::get<2>(*it);
            uint64_t timestamp = std::get<3>(*it);

            writeBatch.removeCachedBlock(blockHash, blockIndex).removeRawBlock(blockIndex);
            requestDeleteSpentOutputs(writeBatch, blockIndex, validatorState);
            requestRemoveTimestamp(writeBatch, timestamp, blockHash);
        }

        auto deletingTransactionHashes = requestTransactionHashesFromBlockIndex(splitBlockIndex);
        requestDeleteTransactions(writeBatch, deletingTransactionHashes);
        requestDeletePaymentIds(writeBatch, deletingTransactionHashes);

        std::vector<ExtendedTransactionInfo> extendedTransactions;
        if (!requestExtendedTransactionInfos(deletingTransactionHashes, database, extendedTransactions))
        {
            logger(Logging::ERROR) << "Error while split: failed to request extended transaction info";
            throw std::runtime_error("failed to request extended transaction info"); // TODO: make error codes
        }

        std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> keyIndexSplitBoundaries;
        for (const auto &transaction : extendedTransactions)
        {
            auto txkeyBoundaries = getMinGlobalIndexesByAmount(transaction.amountToKeyIndexes);
            mergeOutputsSplitBoundaries(keyIndexSplitBoundaries, txkeyBoundaries);
        }

        requestDeleteKeyOutputs(writeBatch, keyIndexSplitBoundaries);

        deleteClosestTimestampBlockIndex(writeBatch, splitBlockIndex);

        logger(Logging::DEBUGGING) << "Performing delete operations";
        // all data and indexes are now copied, no errors detected, can now erase data from database
        auto err = database.write(writeBatch);
        if (err)
        {
            logger(Logging::ERROR) << "split write failed, " << err.message();
            throw std::runtime_error(err.message());
        }

        cutTail(unitsCache, currentTop + 1 - splitBlockIndex);

        children.push_back(cache.get());
        logger(Logging::TRACE) << "Delete successfull";

        // invalidate top block index and hash
        topBlockIndex = std::nullopt;
        topBlockHash = std::nullopt;
        transactionsCount = std::nullopt;

        logger(Logging::DEBUGGING) << "split completed";
        // return new cache
        return cache;
    }

    void DatabaseBlockchainCache::rewind(const uint64_t height)
    {
        /* Same reasoning as split(): the blocks below the lite height cannot be
           undone, because what undoing them needs was never written. Checked
           before the height <= 1 shortcut, so a lite node cannot quietly wipe
           itself back to genesis either. */
        if (isLiteIndexOnlyHeight(static_cast<uint32_t>(height)))
        {
            logger(Logging::ERROR) << "Refusing to rewind to " << height
                                   << ", below this lite node's full block height " << liteHeight
                                   << ". The data needed to undo those blocks was never stored.";

            throw std::runtime_error("Cannot rewind below the lite node height");
        }

        /* 0 height, much much faster to just remove DB and recreate it than
         * remove everything. */
        if (height <= 1)
        {
            logger(Logging::TRACE) << "DatabaseBlockchainCache::rewind height=" << std::to_string(height)
                                   << " calling database.recreate()";
            database.recreate();
            return;
        }

        auto cache = blockchainCacheFactory.createBlockchainCache(currency, this, height);

        using DeleteBlockInfo = std::tuple<uint32_t, Crypto::Hash, TransactionValidatorState, uint64_t>;
        std::vector<DeleteBlockInfo> deletingBlocks;

        BlockchainWriteBatch writeBatch;
        auto currentTop = getTopBlockIndex();

        if (height >= currentTop)
        {
            return;
        }

        for (uint32_t blockIndex = height; blockIndex <= currentTop; ++blockIndex)
        {
            ExtendedPushedBlockInfo extendedInfo = getExtendedPushedBlockInfo(blockIndex);

            auto validatorState = extendedInfo.pushedBlockInfo.validatorState;
            logger(Logging::DEBUGGING) << "pushing block " << blockIndex << " to child segment";
            auto blockHash = pushBlockToAnotherCache(*cache, std::move(extendedInfo.pushedBlockInfo));

            deletingBlocks.emplace_back(blockIndex, blockHash, validatorState, extendedInfo.timestamp);
        }

        const auto blockHashes = getBlockHashes(height, currentTop - height);

        uint64_t blockIndex = height;

        for (const auto &hash : blockHashes)
        {
            writeBatch.removeCachedBlock(hash, blockIndex).removeRawBlock(blockIndex);
            blockIndex++;
            logger(Logging::DEBUGGING) << "Scheduling deletion of block " << blockIndex;
        }

        for (auto it = deletingBlocks.rbegin(); it != deletingBlocks.rend(); ++it)
        {
            auto blockIndex = std::get<0>(*it);
            auto blockHash = std::get<1>(*it);
            auto &validatorState = std::get<2>(*it);
            uint64_t timestamp = std::get<3>(*it);

            writeBatch.removeCachedBlock(blockHash, blockIndex).removeRawBlock(blockIndex);
            requestDeleteSpentOutputs(writeBatch, blockIndex, validatorState);
            requestRemoveTimestamp(writeBatch, timestamp, blockHash);
        }

        /* Get transaction hashes in blocks starting at height */
        auto deletingTransactionHashes = requestTransactionHashesFromBlockIndex(height);

        if (deletingTransactionHashes.size() > 0)
        {
            logger(Logging::DEBUGGING) << "Going to delete " << std::to_string(deletingTransactionHashes.size())
                                       << " transaction(s).";
        }
        else
        {
            logger(Logging::DEBUGGING) << "There is no transaction from this height.";
        }

        /* Delete those transaction. */
        try
        {
            requestDeleteTransactions(writeBatch, deletingTransactionHashes);
        }
        catch (std::exception &e)
        {
            logger(Logging::ERROR) << "requestDeleteTransactions(writeBatch, deletingTransactionHashes): " << e.what();
        }

        /* Delete payment IDs for transaction hashes */
        try
        {
            requestDeletePaymentIds(writeBatch, deletingTransactionHashes);
        }
        catch (std::exception &e)
        {
            logger(Logging::ERROR) << "requestDeleteTransactions(writeBatch, deletingTransactionHashes): " << e.what();
        }

        /* Get extended transaction data */
        std::vector<ExtendedTransactionInfo> extendedTransactions;
        if (!requestExtendedTransactionInfos(deletingTransactionHashes, database, extendedTransactions))
        {
            logger(Logging::ERROR) << "Error while rewinding: failed to request extended transaction info";
            throw std::runtime_error(
                "Error while rewinding: Failed to request extended transaction info from database.");
        }

        std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> keyIndexSplitBoundaries;
        for (const auto &transaction : extendedTransactions)
        {
            auto txkeyBoundaries = getMinGlobalIndexesByAmount(transaction.amountToKeyIndexes);
            mergeOutputsSplitBoundaries(keyIndexSplitBoundaries, txkeyBoundaries);
        }

        /* Remove outputs for transactions */
        requestDeleteKeyOutputs(writeBatch, keyIndexSplitBoundaries);

        deleteClosestTimestampBlockIndex(writeBatch, height);

        logger(Logging::DEBUGGING) << "Performing delete operations";

        // all data and indexes are now copied, no errors detected, can now erase data from database
        auto err = database.write(writeBatch);

        if (err)
        {
            logger(Logging::ERROR) << "split write failed, " << err.message();
            throw std::runtime_error(err.message());
        }

        /* Remove cached blocks */
        cutTail(unitsCache, currentTop + 1 - height);
        children.push_back(cache.get());
        logger(Logging::TRACE) << "Delete successful";

        // invalidate top block index and hash
        topBlockIndex = std::nullopt;
        topBlockHash = std::nullopt;
        transactionsCount = std::nullopt;
    }

    // returns hash of pushed block
    Crypto::Hash DatabaseBlockchainCache::pushBlockToAnotherCache(IBlockchainCache &segment,
                                                                  PushedBlockInfo &&pushedBlockInfo)
    {
        BlockTemplate block;
        bool br = fromBinaryArray(block, pushedBlockInfo.rawBlock.block);
        if (br)
        {
        }
        assert(br);

        std::vector<CachedTransaction> transactions;
        bool tr = Utils::restoreCachedTransactions(pushedBlockInfo.rawBlock.transactions, transactions);
        if (tr)
        {
        }
        assert(tr);

        CachedBlock cachedBlock(block);
        segment.pushBlock(cachedBlock,
                          transactions,
                          pushedBlockInfo.validatorState,
                          pushedBlockInfo.blockSize,
                          pushedBlockInfo.generatedCoins,
                          pushedBlockInfo.blockDifficulty,
                          std::move(pushedBlockInfo.rawBlock));

        return cachedBlock.getBlockHash();
    }

    std::vector<Crypto::Hash> DatabaseBlockchainCache::requestTransactionHashesFromBlockIndex(uint32_t splitBlockIndex)
    {
        logger(Logging::DEBUGGING) << "Requesting transaction hashes starting from block index " << splitBlockIndex;

        BlockchainReadBatch readBatch;
        for (uint32_t blockIndex = splitBlockIndex; blockIndex <= getTopBlockIndex(); ++blockIndex)
        {
            readBatch.requestTransactionHashesByBlock(blockIndex);
        }

        std::vector<Crypto::Hash> transactionHashes;

        auto dbResult = readDatabase(readBatch);
        for (const auto &kv : dbResult.getTransactionHashesByBlocks())
        {
            for (const auto &hash : kv.second)
            {
                transactionHashes.emplace_back(hash);
            }
        }

        return transactionHashes;
    }

    void DatabaseBlockchainCache::requestDeleteTransactions(BlockchainWriteBatch &writeBatch,
                                                            const std::vector<Crypto::Hash> &transactionHashes)
    {
        for (const auto &hash : transactionHashes)
        {
            assert(getCachedTransactionsCount() > 0);
            writeBatch.removeCachedTransaction(hash, getCachedTransactionsCount() - 1);
            transactionsCount = *transactionsCount - 1;
        }
    }

    void DatabaseBlockchainCache::requestDeletePaymentIds(BlockchainWriteBatch &writeBatch,
                                                          const std::vector<Crypto::Hash> &transactionHashes)
    {
        std::unordered_map<Crypto::Hash, size_t> paymentCounts;

        for (const auto &hash : transactionHashes)
        {
            Crypto::Hash paymentId;
            if (!requestPaymentId(database, hash, paymentId))
            {
                continue;
            }

            paymentCounts[paymentId] += 1;
        }

        for (const auto &kv : paymentCounts)
        {
            requestDeletePaymentId(writeBatch, kv.first, kv.second);
        }
    }

    void DatabaseBlockchainCache::requestDeletePaymentId(BlockchainWriteBatch &writeBatch,
                                                         const Crypto::Hash &paymentId,
                                                         size_t toDelete)
    {
        size_t count = requestPaymentIdTransactionsCount(database, paymentId);
        assert(count > 0);
        assert(count >= toDelete);

        logger(Logging::DEBUGGING) << "Deleting last " << toDelete << " transaction hashes of payment id " << paymentId;
        writeBatch.removePaymentId(paymentId, static_cast<uint32_t>(count - toDelete));
    }

    void DatabaseBlockchainCache::requestDeleteSpentOutputs(BlockchainWriteBatch &writeBatch,
                                                            uint32_t blockIndex,
                                                            const TransactionValidatorState &spentOutputs)
    {
        logger(Logging::DEBUGGING) << "Deleting spent outputs for block index " << blockIndex;

        std::vector<Crypto::KeyImage> spentKeys(spentOutputs.spentKeyImages.begin(), spentOutputs.spentKeyImages.end());

        writeBatch.removeSpentKeyImages(blockIndex, spentKeys);
    }

    void DatabaseBlockchainCache::requestDeleteKeyOutputs(
        BlockchainWriteBatch &writeBatch,
        const std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> &boundaries)
    {
        if (boundaries.empty())
        {
            // hardly possible
            logger(Logging::DEBUGGING) << "DatabaseBlockchainCache::requestDeleteKeyOutputs: No key output amounts...";
            return;
        }

        BlockchainReadBatch readBatch;
        for (auto kv : boundaries)
        {
            readBatch.requestKeyOutputGlobalIndexesCountForAmount(kv.first);
        }

        std::unordered_map<IBlockchainCache::Amount, uint32_t> amountCounts =
            readDatabase(readBatch).getKeyOutputGlobalIndexesCountForAmounts();
        assert(amountCounts.size() == boundaries.size());

        for (const auto &kv : amountCounts)
        {
            auto it = boundaries.find(
                kv.first); // can't be equal end() since assert(amountCounts.size() == boundaries.size())
            requestDeleteKeyOutputsAmount(writeBatch, kv.first, it->second, kv.second);
        }
    }

    void DatabaseBlockchainCache::requestDeleteKeyOutputsAmount(BlockchainWriteBatch &writeBatch,
                                                                IBlockchainCache::Amount amount,
                                                                IBlockchainCache::GlobalOutputIndex boundary,
                                                                uint32_t outputsCount)
    {
        logger(Logging::DEBUGGING) << "Requesting delete for key output amount " << amount
                                   << " starting from global index " << boundary << " to " << (outputsCount - 1);

        writeBatch.removeKeyOutputGlobalIndexes(amount, outputsCount - boundary, boundary);
        for (GlobalOutputIndex index = boundary; index < outputsCount; ++index)
        {
            writeBatch.removeKeyOutputInfo(amount, index);
        }

        updateKeyOutputCount(amount, boundary - outputsCount);
    }

    void DatabaseBlockchainCache::requestRemoveTimestamp(BlockchainWriteBatch &batch,
                                                         uint64_t timestamp,
                                                         const Crypto::Hash &blockHash)
    {
        auto readBatch = BlockchainReadBatch().requestBlockHashesByTimestamp(timestamp);
        auto result = readDatabase(readBatch);

        if (result.getBlockHashesByTimestamp().count(timestamp) == 0)
        {
            return;
        }

        auto indexes = result.getBlockHashesByTimestamp().at(timestamp);
        auto it = std::find(indexes.begin(), indexes.end(), blockHash);
        indexes.erase(it);

        if (indexes.empty())
        {
            logger(Logging::DEBUGGING) << "Deleting timestamp " << timestamp;
            batch.removeTimestamp(timestamp);
        }
        else
        {
            logger(Logging::DEBUGGING) << "Deleting block hash " << blockHash << " from timestamp " << timestamp;
            batch.insertTimestamp(timestamp, indexes);
        }
    }

    void DatabaseBlockchainCache::pushTransaction(const CachedTransaction &cachedTransaction,
                                                  uint32_t blockIndex,
                                                  uint16_t transactionBlockIndex,
                                                  BlockchainWriteBatch &batch,
                                                  WalletTypes::RawTransaction *walletTxOut)
    {
        logger(Logging::DEBUGGING) << "push transaction with hash " << cachedTransaction.getTransactionHash();
        const auto &tx = cachedTransaction.getTransaction();

        /* Below a lite node's lite height the transaction record, the payment ID
           index and the transaction public key index are never written, and the
           per-output transaction hash is zeroed. Everything consensus needs from
           this transaction still goes into the batch: the key output info, the
           per-amount global indexes and the amount list, which are what ring
           member resolution and decoy selection read. See LITENODE.md. */
        const bool indexOnly = isLiteIndexOnlyHeight(blockIndex);

        ExtendedTransactionInfo transactionCacheInfo;
        transactionCacheInfo.blockIndex = blockIndex;
        transactionCacheInfo.transactionIndex = transactionBlockIndex;
        transactionCacheInfo.transactionHash = cachedTransaction.getTransactionHash();
        transactionCacheInfo.unlockTime = tx.unlockTime;

        assert(tx.outputs.size() <= std::numeric_limits<uint16_t>::max());

        transactionCacheInfo.globalIndexes.reserve(tx.outputs.size());
        transactionCacheInfo.outputs.reserve(tx.outputs.size());
        auto outputCount = 0;
        std::unordered_map<Amount, std::vector<PackedOutIndex>> keyIndexes;

        std::set<Amount> newKeyAmounts;

        for (auto &output : tx.outputs)
        {
            transactionCacheInfo.outputs.push_back(output.target);

            PackedOutIndex poi;
            poi.blockIndex = blockIndex;
            poi.transactionIndex = transactionBlockIndex;
            poi.outputIndex = outputCount++;

            if (std::holds_alternative<KeyOutput>(output.target))
            {
                keyIndexes[output.amount].push_back(poi);
                auto outputCountForAmount = updateKeyOutputCount(output.amount, 1);
                if (outputCountForAmount == 1)
                {
                    newKeyAmounts.insert(output.amount);
                }

                assert(outputCountForAmount > 0);
                auto globalIndex = outputCountForAmount - 1;
                transactionCacheInfo.globalIndexes.push_back(globalIndex);
                // output global index:
                transactionCacheInfo.amountToKeyIndexes[output.amount].push_back(globalIndex);

                KeyOutputInfo outputInfo;
                outputInfo.publicKey = std::get<KeyOutput>(output.target).key;

                /* Only a rescan or an explorer reads this back, and a lite node
                   offers neither below its lite height. It is 32 bytes of high
                   entropy per key output that nothing can ever read, and the one
                   part of the database a compressor cannot help with. Zeroed
                   rather than removed: the record layout and the schema version
                   stay exactly as they are, no reader needs to know, and a great
                   many identical zero hashes cost almost nothing once RocksDB
                   has compressed them. */
                outputInfo.transactionHash = indexOnly ? Crypto::Hash {} : transactionCacheInfo.transactionHash;
                outputInfo.unlockTime = transactionCacheInfo.unlockTime;
                outputInfo.outputIndex = poi.outputIndex;

                batch.insertKeyOutputInfo(output.amount, globalIndex, outputInfo);

                /* Populate compact wallet sync data if requested */
                if (walletTxOut)
                {
                    WalletTypes::KeyOutput keyOut;
                    keyOut.key = outputInfo.publicKey;
                    keyOut.amount = output.amount;
                    keyOut.globalOutputIndex = globalIndex;
                    walletTxOut->keyOutputs.push_back(keyOut);
                }
            }
        }

        for (auto &amountToOutputs : keyIndexes)
        {
            batch.insertKeyOutputGlobalIndexes(amountToOutputs.first,
                                               amountToOutputs.second,
                                               updateKeyOutputCount(amountToOutputs.first, 0)); // Size already updated.
        }

        if (!newKeyAmounts.empty())
        {
            assert(keyOutputAmountsCount.has_value());
            batch.insertKeyOutputAmounts(newKeyAmounts, *keyOutputAmountsCount);
        }

        if (indexOnly)
        {
            /* Still count it, or the chain-wide transaction total would only
               cover the blocks stored in full. */
            batch.insertTransactionCount(getCachedTransactionsCount() + 1);
            transactionsCount = *transactionsCount + 1;

            logger(Logging::DEBUGGING) << "push transaction with hash " << cachedTransaction.getTransactionHash()
                                       << " completed (index only)";
            return;
        }

        Crypto::Hash paymentId;
        const bool hasPaymentId = getPaymentIdFromTxExtra(cachedTransaction.getTransaction().extra, paymentId);
        if (hasPaymentId)
        {
            insertPaymentId(batch, cachedTransaction.getTransactionHash(), paymentId);
        }

        /* Store the transaction public key so wallet sync can work even after raw blocks are pruned. */
        const Crypto::PublicKey txPublicKey =
            getTransactionPublicKeyFromExtra(cachedTransaction.getTransaction().extra);
        batch.insertTransactionPublicKey(cachedTransaction.getTransactionHash(), txPublicKey);

        /* Finish populating wallet sync data: header fields, key inputs, payment ID */
        if (walletTxOut)
        {
            walletTxOut->hash = cachedTransaction.getTransactionHash();
            walletTxOut->transactionPublicKey = txPublicKey;
            walletTxOut->unlockTime = tx.unlockTime;

            for (const auto &input : tx.inputs)
            {
                if (std::holds_alternative<KeyInput>(input))
                {
                    walletTxOut->keyInputs.push_back(std::get<KeyInput>(input));
                }
            }

            if (hasPaymentId)
            {
                walletTxOut->paymentID = Common::podToHex(paymentId);
            }
        }

        batch.insertCachedTransaction(transactionCacheInfo, getCachedTransactionsCount() + 1);
        transactionsCount = *transactionsCount + 1;
        logger(Logging::DEBUGGING) << "push transaction with hash " << cachedTransaction.getTransactionHash()
                                   << " finished";
    }

    uint32_t DatabaseBlockchainCache::updateKeyOutputCount(Amount amount, int32_t diff) const
    {
        auto it = keyOutputCountsForAmounts.find(amount);
        if (it == keyOutputCountsForAmounts.end())
        {
            logger(Logging::TRACE) << "updateKeyOutputCount: failed to found key for amount " << std::to_string(amount)
                                   << ", request database";

            BlockchainReadBatch batch;
            auto result = readDatabase(batch.requestKeyOutputGlobalIndexesCountForAmount(amount));
            auto found = result.getKeyOutputGlobalIndexesCountForAmounts().find(amount);
            auto val = found != result.getKeyOutputGlobalIndexesCountForAmounts().end() ? found->second : 0;
            it = keyOutputCountsForAmounts.insert({amount, val}).first;
            logger(Logging::TRACE) << "updateKeyOutputCount: database replied: amount " << amount << " value " << val;

            if (val == 0)
            {
                if (!keyOutputAmountsCount)
                {
                    auto result = readDatabase(batch.requestKeyOutputAmountsCount());
                    keyOutputAmountsCount = result.getKeyOutputAmountsCount();
                }

                keyOutputAmountsCount = *keyOutputAmountsCount + 1;
            }
        }
        else if (!keyOutputAmountsCount)
        {
            auto result = readDatabase(BlockchainReadBatch().requestKeyOutputAmountsCount());
            keyOutputAmountsCount = result.getKeyOutputAmountsCount();
        }

        it->second += diff;
        assert(it->second >= 0);
        return it->second;
    }

    void DatabaseBlockchainCache::insertPaymentId(BlockchainWriteBatch &batch,
                                                  const Crypto::Hash &transactionHash,
                                                  const Crypto::Hash &paymentId)
    {
        BlockchainReadBatch readBatch;
        uint32_t count = 0;

        auto readResult = readDatabase(readBatch.requestTransactionCountByPaymentId(paymentId));
        if (readResult.getTransactionCountByPaymentIds().count(paymentId) != 0)
        {
            count = readResult.getTransactionCountByPaymentIds().at(paymentId);
        }

        count += 1;

        batch.insertPaymentId(transactionHash, paymentId, count);
    }

    void DatabaseBlockchainCache::insertBlockTimestamp(BlockchainWriteBatch &batch,
                                                       uint64_t timestamp,
                                                       const Crypto::Hash &blockHash)
    {
        BlockchainReadBatch readBatch;
        readBatch.requestBlockHashesByTimestamp(timestamp);

        std::vector<Crypto::Hash> blockHashes;
        auto readResult = readDatabase(readBatch);

        if (readResult.getBlockHashesByTimestamp().count(timestamp) != 0)
        {
            blockHashes = readResult.getBlockHashesByTimestamp().at(timestamp);
        }

        blockHashes.emplace_back(blockHash);

        batch.insertTimestamp(timestamp, blockHashes);
    }

    void DatabaseBlockchainCache::pushBlock(const CachedBlock &cachedBlock,
                                            const std::vector<CachedTransaction> &cachedTransactions,
                                            const TransactionValidatorState &validatorState,
                                            size_t blockSize,
                                            uint64_t generatedCoins,
                                            uint64_t blockDifficulty,
                                            RawBlock &&rawBlock)
    {
        BlockchainWriteBatch batch;
        logger(Logging::DEBUGGING) << "push block with hash " << cachedBlock.getBlockHash() << ", and "
                                   << cachedTransactions.size() + 1 << " transactions"; //+1 for base transaction

        // TODO: cache top block difficulty, size, timestamp, coins; use it here
        auto lastBlockInfo = getCachedBlockInfo(getTopBlockIndex());
        auto cumulativeDifficulty = lastBlockInfo.cumulativeDifficulty + blockDifficulty;
        auto alreadyGeneratedCoins = lastBlockInfo.alreadyGeneratedCoins + generatedCoins;
        auto alreadyGeneratedTransactions = lastBlockInfo.alreadyGeneratedTransactions + cachedTransactions.size() + 1;

        CachedBlockInfo blockInfo;
        blockInfo.blockHash = cachedBlock.getBlockHash();
        blockInfo.alreadyGeneratedCoins = alreadyGeneratedCoins;
        blockInfo.alreadyGeneratedTransactions = alreadyGeneratedTransactions;
        blockInfo.cumulativeDifficulty = cumulativeDifficulty;
        blockInfo.blockSize = static_cast<uint32_t>(blockSize);
        blockInfo.timestamp = cachedBlock.getBlock().timestamp;

        const uint32_t newBlockIndex = getTopBlockIndex() + 1;

        /* Below a lite node's lite height only the indexes that later blocks
           actually read are kept: the key image -> block index entries, the key
           output info and per-amount counts written by pushTransaction, and the
           block info itself. The block body, its transaction hash list, the
           rewind index and the wallet sync archive all go. See LITENODE.md. */
        const bool indexOnly = isLiteIndexOnlyHeight(newBlockIndex);

        batch.insertSpentKeyImages(newBlockIndex, validatorState.spentKeyImages, !indexOnly);

        auto txHashes = cachedBlock.getBlock().transactionHashes;
        auto baseTransaction = cachedBlock.getBlock().baseTransaction;
        auto cachedBaseTransaction = CachedTransaction {std::move(baseTransaction)};

        // base transaction's hash is always the first one in index for this block
        txHashes.insert(txHashes.begin(), cachedBaseTransaction.getTransactionHash());

        batch.insertCachedBlock(blockInfo, newBlockIndex, indexOnly ? std::vector<Crypto::Hash> {} : txHashes);

        if (!indexOnly)
        {
            batch.insertRawBlock(newBlockIndex, rawBlock);
        }

        /* Push transactions and simultaneously collect compact wallet sync data */
        auto transactionIndex = 0;
        WalletTypes::RawTransaction coinbaseWalletTx;
        pushTransaction(cachedBaseTransaction, newBlockIndex, transactionIndex++, batch, &coinbaseWalletTx);

        std::vector<WalletTypes::RawTransaction> txWalletData;
        txWalletData.reserve(cachedTransactions.size());
        for (const auto &transaction : cachedTransactions)
        {
            txWalletData.emplace_back();
            pushTransaction(transaction, newBlockIndex, transactionIndex++, batch, &txWalletData.back());
        }

        /* Assemble and store compact WalletBlockInfo — survives raw-block pruning.
           A lite node skips it below the lite height: it exists so a wallet can
           sync across pruned raw blocks, and a lite node does not offer a sync
           that reaches down there at all. */
        if (!indexOnly)
        {
            WalletTypes::WalletBlockInfo walletBlock;
            walletBlock.blockHeight = newBlockIndex;
            walletBlock.blockHash   = cachedBlock.getBlockHash();
            walletBlock.blockTimestamp = cachedBlock.getBlock().timestamp;

            /* Coinbase: copy base fields only (no keyInputs by design) */
            WalletTypes::RawCoinbaseTransaction coinbaseSyncTx;
            coinbaseSyncTx.hash               = coinbaseWalletTx.hash;
            coinbaseSyncTx.transactionPublicKey = coinbaseWalletTx.transactionPublicKey;
            coinbaseSyncTx.keyOutputs         = coinbaseWalletTx.keyOutputs;
            coinbaseSyncTx.unlockTime         = coinbaseWalletTx.unlockTime;
            walletBlock.coinbaseTransaction   = coinbaseSyncTx;

            walletBlock.transactions = std::move(txWalletData);
            batch.insertWalletSyncBlock(newBlockIndex, walletBlock);
        }

        /* The timestamp index exists to answer "which height was this date",
           which is how a wallet starts a scan from a date. A lite node cannot
           serve a scan starting below its lite height at all, so it is dead
           weight down there. */
        if (!indexOnly)
        {
            auto closestBlockIndexDb =
                requestClosestBlockIndexByTimestamp(roundToMidnight(cachedBlock.getBlock().timestamp), database);
            if (!closestBlockIndexDb.second)
            {
                logger(Logging::ERROR) << "push block " << cachedBlock.getBlockHash()
                                       << " request closest block index by timestamp failed";
                throw std::runtime_error("Couldn't get closest to timestamp block index");
            }

            if (!closestBlockIndexDb.first)
            {
                batch.insertClosestTimestampBlockIndex(roundToMidnight(cachedBlock.getBlock().timestamp),
                                                       getTopBlockIndex() + 1);
            }
        }

        // We aren't even using this so why add this?
        // insertBlockTimestamp(batch, cachedBlock.getBlock().timestamp, cachedBlock.getBlockHash());

        auto res = database.write(batch);
        if (res)
        {
            logger(Logging::ERROR) << "push block " << cachedBlock.getBlockHash() << " write failed: " << res.message();
            throw std::runtime_error(res.message());
        }

        topBlockIndex = *topBlockIndex + 1;
        topBlockHash = cachedBlock.getBlockHash();
        logger(Logging::DEBUGGING) << "push block " << cachedBlock.getBlockHash() << " completed";

        unitsCache.push_back(blockInfo);
        if (unitsCache.size() > unitsCacheSize)
        {
            unitsCache.pop_front();
        }
    }

    PushedBlockInfo DatabaseBlockchainCache::getPushedBlockInfo(uint32_t blockIndex) const
    {
        return getExtendedPushedBlockInfo(blockIndex).pushedBlockInfo;
    }

    bool DatabaseBlockchainCache::checkIfSpent(const Crypto::KeyImage &keyImage, uint32_t blockIndex) const
    {
        auto batch = BlockchainReadBatch().requestBlockIndexBySpentKeyImage(keyImage);
        auto res = database.readThreadSafe(batch);

        if (res)
        {
            logger(Logging::ERROR) << "checkIfSpent failed, request to database failed: " << res.message();
            return false;
        }

        auto readResult = batch.extractResult();
        auto it = readResult.getBlockIndexesBySpentKeyImages().find(keyImage);

        return it != readResult.getBlockIndexesBySpentKeyImages().end() && it->second <= blockIndex;
    }

    bool DatabaseBlockchainCache::checkIfSpent(const Crypto::KeyImage &keyImage) const
    {
        return checkIfSpent(keyImage, getTopBlockIndex());
    }

    bool DatabaseBlockchainCache::isTransactionSpendTimeUnlocked(uint64_t unlockTime) const
    {
        return isTransactionSpendTimeUnlocked(unlockTime, getTopBlockIndex());
    }

    bool DatabaseBlockchainCache::isTransactionSpendTimeUnlocked(uint64_t unlockTime, uint32_t blockIndex) const
    {
        if (unlockTime < currency.maxBlockHeight())
        {
            // interpret as block index
            return blockIndex + currency.lockedTxAllowedDeltaBlocks() >= unlockTime;
        }

        if (blockIndex >= CryptoNote::parameters::TRANSACTION_INPUT_BLOCKTIME_VALIDATION_HEIGHT)
        {
            /* Get the last block timestamp from an existing method call */
            const std::vector<uint64_t> lastBlockTimestamps = getLastTimestamps(1);

            /* Pop the last timestamp off the vector */
            const uint64_t lastBlockTimestamp = lastBlockTimestamps.at(0);

            /* Compare our delta seconds plus our last time stamp against the unlock time */
            return lastBlockTimestamp + currency.lockedTxAllowedDeltaSeconds() >= unlockTime;
        }

        // interpret as time
        return static_cast<uint64_t>(time(nullptr)) + currency.lockedTxAllowedDeltaSeconds() >= unlockTime;
    }

    ExtractOutputKeysResult DatabaseBlockchainCache::extractKeyOutputKeys(
        uint64_t amount,
        Common::ArrayView<uint32_t> globalIndexes,
        std::vector<Crypto::PublicKey> &publicKeys) const
    {
        return extractKeyOutputKeys(amount, getTopBlockIndex(), globalIndexes, publicKeys);
    }

    ExtractOutputKeysResult DatabaseBlockchainCache::extractKeyOutputKeys(
        uint64_t amount,
        uint32_t blockIndex,
        Common::ArrayView<uint32_t> globalIndexes,
        std::vector<Crypto::PublicKey> &publicKeys) const
    {
        return extractKeyOutputs(amount,
                                 blockIndex,
                                 globalIndexes,
                                 [this, &publicKeys, blockIndex](const CachedTransactionInfo &info,
                                                                 PackedOutIndex index,
                                                                 uint32_t globalIndex)
                                 {
                                     if (!isTransactionSpendTimeUnlocked(info.unlockTime, blockIndex))
                                     {
                                         logger(Logging::DEBUGGING)
                                             << "extractKeyOutputKeys: output " << globalIndex << " is locked";
                                         return ExtractOutputKeysResult::OUTPUT_LOCKED;
                                     }

                                     auto &output = info.outputs[index.outputIndex];
                                     assert(std::holds_alternative<KeyOutput>(output));
                                     publicKeys.push_back(std::get<KeyOutput>(output).key);

                                     return ExtractOutputKeysResult::SUCCESS;
                                 });
    }

    ExtractOutputKeysResult DatabaseBlockchainCache::extractKeyOtputIndexes(
        uint64_t amount,
        Common::ArrayView<uint32_t> globalIndexes,
        std::vector<PackedOutIndex> &outIndexes) const
    {
        if (!requestPackedOutputs(amount, globalIndexes, database, outIndexes))
        {
            logger(Logging::ERROR) << "extractKeyOtputIndexes failed: failed to read database";
            return ExtractOutputKeysResult::INVALID_GLOBAL_INDEX;
        }

        return ExtractOutputKeysResult::SUCCESS;
    }

    ExtractOutputKeysResult DatabaseBlockchainCache::extractKeyOtputReferences(
        uint64_t amount,
        Common::ArrayView<uint32_t> globalIndexes,
        std::vector<std::pair<Crypto::Hash, size_t>> &outputReferences) const
    {
        return extractKeyOutputs(
            amount,
            getTopBlockIndex(),
            globalIndexes,
            [&outputReferences](const CachedTransactionInfo &info, PackedOutIndex index, uint32_t globalIndex)
            {
                outputReferences.push_back(std::make_pair(info.transactionHash, index.outputIndex));
                return ExtractOutputKeysResult::SUCCESS;
            });
    }

    uint32_t DatabaseBlockchainCache::getTopBlockIndex() const
    {
        if (!topBlockIndex)
        {
            auto batch = BlockchainReadBatch().requestLastBlockIndex();
            auto result = database.read(batch);

            if (result)
            {
                logger(Logging::ERROR) << "Failed to read top block index from database";
                throw std::system_error(result);
            }

            auto readResult = batch.extractResult();
            if (!readResult.getLastBlockIndex().second)
            {
                logger(Logging::TRACE) << "Top block index does not exist in database";
                topBlockIndex = 0;
            }

            topBlockIndex = readResult.getLastBlockIndex().first;
        }

        return *topBlockIndex;
    }

    uint8_t DatabaseBlockchainCache::getBlockMajorVersionForHeight(uint32_t height) const
    {
        UpgradeManager upgradeManager;
        upgradeManager.addMajorBlockVersion(BLOCK_MAJOR_VERSION_2, currency.upgradeHeight(BLOCK_MAJOR_VERSION_2));
        upgradeManager.addMajorBlockVersion(BLOCK_MAJOR_VERSION_3, currency.upgradeHeight(BLOCK_MAJOR_VERSION_3));
        upgradeManager.addMajorBlockVersion(BLOCK_MAJOR_VERSION_4, currency.upgradeHeight(BLOCK_MAJOR_VERSION_4));
        upgradeManager.addMajorBlockVersion(BLOCK_MAJOR_VERSION_5, currency.upgradeHeight(BLOCK_MAJOR_VERSION_5));
        upgradeManager.addMajorBlockVersion(BLOCK_MAJOR_VERSION_6, currency.upgradeHeight(BLOCK_MAJOR_VERSION_6));
        upgradeManager.addMajorBlockVersion(BLOCK_MAJOR_VERSION_7, currency.upgradeHeight(BLOCK_MAJOR_VERSION_7));
        return upgradeManager.getBlockMajorVersion(height);
    }

    uint64_t DatabaseBlockchainCache::getCachedTransactionsCount() const
    {
        if (!transactionsCount)
        {
            auto batch = BlockchainReadBatch().requestTransactionsCount();
            auto result = database.read(batch);

            if (result)
            {
                logger(Logging::ERROR) << "Failed to read transactions count from database";
                throw std::system_error(result);
            }

            auto readResult = batch.extractResult();
            if (!readResult.getTransactionsCount().second)
            {
                logger(Logging::TRACE) << "Transactions count does not exist in database";
                transactionsCount = 0;
            }
            else
            {
                transactionsCount = readResult.getTransactionsCount().first;
            }
        }

        return *transactionsCount;
    }

    const Crypto::Hash &DatabaseBlockchainCache::getTopBlockHash() const
    {
        if (!topBlockHash)
        {
            auto batch = BlockchainReadBatch().requestCachedBlock(getTopBlockIndex());
            auto result = readDatabase(batch);
            topBlockHash = result.getCachedBlocks().at(getTopBlockIndex()).blockHash;
        }
        return *topBlockHash;
    }

    uint32_t DatabaseBlockchainCache::getBlockCount() const
    {
        return getTopBlockIndex() + 1;
    }

    bool DatabaseBlockchainCache::hasBlock(const Crypto::Hash &blockHash) const
    {
        auto batch = BlockchainReadBatch().requestBlockIndexByBlockHash(blockHash);
        auto result = database.read(batch);
        return !result && batch.extractResult().getBlockIndexesByBlockHashes().count(blockHash);
    }

    uint32_t DatabaseBlockchainCache::getBlockIndex(const Crypto::Hash &blockHash) const
    {
        if (blockHash == getTopBlockHash())
        {
            return getTopBlockIndex();
        }

        auto batch = BlockchainReadBatch().requestBlockIndexByBlockHash(blockHash);
        auto result = readDatabase(batch);
        return result.getBlockIndexesByBlockHashes().at(blockHash);
    }

    bool DatabaseBlockchainCache::hasTransaction(const Crypto::Hash &transactionHash) const
    {
        auto batch = BlockchainReadBatch().requestCachedTransaction(transactionHash);
        auto result = database.read(batch);
        return !result && batch.extractResult().getCachedTransactions().count(transactionHash);
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastTimestamps(size_t count) const
    {
        return getLastTimestamps(count, getTopBlockIndex(), UseGenesis {true});
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastTimestamps(size_t count,
                                                                     uint32_t blockIndex,
                                                                     UseGenesis useGenesis) const
    {
        return getLastUnits(count, blockIndex, useGenesis, [](const CachedBlockInfo &inf) { return inf.timestamp; });
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastBlocksSizes(size_t count) const
    {
        return getLastBlocksSizes(count, getTopBlockIndex(), UseGenesis {true});
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastBlocksSizes(size_t count,
                                                                      uint32_t blockIndex,
                                                                      UseGenesis useGenesis) const
    {
        return getLastUnits(count, blockIndex, useGenesis, [](const CachedBlockInfo &cb) { return cb.blockSize; });
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastCumulativeDifficulties(size_t count,
                                                                                 uint32_t blockIndex,
                                                                                 UseGenesis useGenesis) const
    {
        return getLastUnits(count,
                            blockIndex,
                            useGenesis,
                            [](const CachedBlockInfo &info) { return info.cumulativeDifficulty; });
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastCumulativeDifficulties(size_t count) const
    {
        return getLastCumulativeDifficulties(count, getTopBlockIndex(), UseGenesis {true});
    }

    uint64_t DatabaseBlockchainCache::getDifficultyForNextBlock() const
    {
        return getDifficultyForNextBlock(getTopBlockIndex());
    }

    uint64_t DatabaseBlockchainCache::getDifficultyForNextBlock(uint32_t blockIndex) const
    {
        assert(blockIndex <= getTopBlockIndex());
        uint8_t nextBlockMajorVersion = getBlockMajorVersionForHeight(blockIndex + 1);
        auto timestamps =
            getLastTimestamps(CryptoNote::parameters::DIFFICULTY_BLOCKS_COUNT, blockIndex, UseGenesis {false});
        auto commulativeDifficulties = getLastCumulativeDifficulties(CryptoNote::parameters::DIFFICULTY_BLOCKS_COUNT,
                                                                     blockIndex,
                                                                     UseGenesis {false});
        return currency.getNextDifficulty(nextBlockMajorVersion,
                                          blockIndex,
                                          std::move(timestamps),
                                          std::move(commulativeDifficulties));
    }

    uint64_t DatabaseBlockchainCache::getCurrentCumulativeDifficulty() const
    {
        return getCachedBlockInfo(getTopBlockIndex()).cumulativeDifficulty;
    }

    uint64_t DatabaseBlockchainCache::getCurrentCumulativeDifficulty(uint32_t blockIndex) const
    {
        assert(blockIndex <= getTopBlockIndex());
        return getCachedBlockInfo(blockIndex).cumulativeDifficulty;
    }

    CachedBlockInfo DatabaseBlockchainCache::getCachedBlockInfo(uint32_t index) const
    {
        auto batch = BlockchainReadBatch().requestCachedBlock(index);
        auto result = readDatabase(batch);
        return result.getCachedBlocks().at(index);
    }

    uint64_t DatabaseBlockchainCache::getAlreadyGeneratedCoins() const
    {
        return getAlreadyGeneratedCoins(getTopBlockIndex());
    }

    uint64_t DatabaseBlockchainCache::getAlreadyGeneratedCoins(uint32_t blockIndex) const
    {
        return getCachedBlockInfo(blockIndex).alreadyGeneratedCoins;
    }

    uint64_t DatabaseBlockchainCache::getAlreadyGeneratedTransactions(uint32_t blockIndex) const
    {
        return getCachedBlockInfo(blockIndex).alreadyGeneratedTransactions;
    }

    std::vector<CachedBlockInfo> DatabaseBlockchainCache::getLastCachedUnits(uint32_t blockIndex,
                                                                             size_t count,
                                                                             UseGenesis useGenesis) const
    {
        assert(blockIndex <= getTopBlockIndex());

        std::vector<CachedBlockInfo> cachedResult;
        const uint32_t cacheStartIndex = (getTopBlockIndex() + 1) - static_cast<uint32_t>(unitsCache.size());

        count = std::min(unitsCache.size(), count);

        if (cacheStartIndex > blockIndex || count == 0)
        {
            return cachedResult;
        }

        count = std::min(blockIndex - cacheStartIndex + 1, static_cast<uint32_t>(count));
        uint32_t offset = static_cast<uint32_t>(blockIndex + 1 - count) - cacheStartIndex;

        assert(offset < unitsCache.size());

        if (!useGenesis && cacheStartIndex == 0 && offset == 0)
        {
            ++offset;
            --count;
        }

        if (offset >= unitsCache.size() || count == 0)
        {
            return cachedResult;
        }

        cachedResult.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            cachedResult.push_back(unitsCache[offset + i]);
        }

        return cachedResult;
    }

    std::vector<CachedBlockInfo> DatabaseBlockchainCache::getLastDbUnits(uint32_t blockIndex,
                                                                         size_t count,
                                                                         UseGenesis useGenesis) const
    {
        uint32_t readFrom = blockIndex + 1 - std::min(blockIndex + 1, static_cast<uint32_t>(count));
        if (readFrom == 0 && !useGenesis)
        {
            readFrom += 1;
        }

        uint32_t toRead = blockIndex - readFrom + 1;
        std::vector<CachedBlockInfo> units;
        units.reserve(toRead);

        const uint32_t step = 200;
        while (toRead > 0)
        {
            auto next = std::min(toRead, step);
            toRead -= next;

            BlockchainReadBatch batch;
            for (auto id = readFrom; id < readFrom + next; ++id)
            {
                batch.requestCachedBlock(id);
            }

            readFrom += next;

            auto res = readDatabase(batch);

            std::map<uint32_t, CachedBlockInfo> sortedResult(res.getCachedBlocks().begin(),
                                                             res.getCachedBlocks().end());
            for (const auto &kv : sortedResult)
            {
                units.push_back(kv.second);
            }
            //    std::transform(sortedResult.begin(), sortedResult.end(), std::back_inserter(units),
            //                   [&](const std::pair<uint32_t, CachedBlockInfo>& cb) { return pred(cb.second); });
        }

        return units;
    }

    std::vector<uint64_t> DatabaseBlockchainCache::getLastUnits(
        size_t count,
        uint32_t blockIndex,
        UseGenesis useGenesis,
        std::function<uint64_t(const CachedBlockInfo &)> pred) const
    {
        assert(count <= std::numeric_limits<uint32_t>::max());

        auto cachedUnits = getLastCachedUnits(blockIndex, count, useGenesis);

        uint32_t availableUnits = blockIndex;
        if (useGenesis)
        {
            availableUnits += 1;
        }

        assert(availableUnits >= cachedUnits.size());

        if (availableUnits - cachedUnits.size() == 0)
        {
            std::vector<uint64_t> result;
            result.reserve(cachedUnits.size());
            for (const auto &unit : cachedUnits)
            {
                result.push_back(pred(unit));
            }

            return result;
        }

        assert(blockIndex + 1 >= cachedUnits.size());
        uint32_t dbIndex = blockIndex - static_cast<uint32_t>(cachedUnits.size());

        assert(count >= cachedUnits.size());
        size_t leftCount = count - cachedUnits.size();

        auto dbUnits = getLastDbUnits(dbIndex, leftCount, useGenesis);
        std::vector<uint64_t> result;
        result.reserve(dbUnits.size() + cachedUnits.size());
        for (const auto &unit : dbUnits)
        {
            result.push_back(pred(unit));
        }

        for (const auto &unit : cachedUnits)
        {
            result.push_back(pred(unit));
        }

        return result;
    }

    Crypto::Hash DatabaseBlockchainCache::getBlockHash(uint32_t blockIndex) const
    {
        if (blockIndex == getTopBlockIndex())
        {
            return getTopBlockHash();
        }

        auto batch = BlockchainReadBatch().requestCachedBlock(blockIndex);
        auto result = readDatabase(batch);
        return result.getCachedBlocks().at(blockIndex).blockHash;
    }

    std::vector<Crypto::Hash> DatabaseBlockchainCache::getBlockHashes(uint32_t startIndex, size_t maxCount) const
    {
        assert(startIndex <= getTopBlockIndex());
        assert(maxCount <= std::numeric_limits<uint32_t>::max());

        uint32_t count = std::min(getTopBlockIndex() - startIndex + 1, static_cast<uint32_t>(maxCount));
        if (count == 0)
        {
            return {};
        }

        BlockchainReadBatch request;
        auto index = startIndex;
        try
        {
            while (index != startIndex + count)
            {
                request.requestCachedBlock(index++);
            }
        }
        catch (std::exception &e)
        {
            logger(Logging::TRACE) << "DatabaseBlockchainCache::getBlockHashes:request.requestCachedBlock: "
                                   << e.what();
        }

        auto result = readDatabase(request);
        logger(Logging::TRACE) << "DatabaseBlockchainCache::getBlockHashes:result.getCachedBlocks().size(): "
                               << std::to_string(result.getCachedBlocks().size());
        logger(Logging::TRACE) << "DatabaseBlockchainCache::getBlockHashes:count " << std::to_string(count);
        if (!result.getCachedBlocks().empty())
        {
            assert(result.getCachedBlocks().size() == count);

            std::vector<Crypto::Hash> hashes;
            hashes.reserve(count);

            std::map<uint32_t, CachedBlockInfo> sortedResult(result.getCachedBlocks().begin(),
                                                             result.getCachedBlocks().end());

            std::transform(sortedResult.begin(),
                           sortedResult.end(),
                           std::back_inserter(hashes),
                           [](const std::pair<uint32_t, CachedBlockInfo> &cb) { return cb.second.blockHash; });
            return hashes;
        }
        else
        {
            return {};
        }
    }

    IBlockchainCache *DatabaseBlockchainCache::getParent() const
    {
        return nullptr;
    }

    uint32_t DatabaseBlockchainCache::getStartBlockIndex() const
    {
        return 0;
    }

    size_t DatabaseBlockchainCache::getKeyOutputsCountForAmount(uint64_t amount, uint32_t blockIndex) const
    {
        uint32_t outputsCount = requestKeyOutputGlobalIndexesCountForAmount(amount, database);

        auto getOutput = std::bind(retrieveKeyOutput, std::placeholders::_1, std::placeholders::_2, std::ref(database));
        auto begin = DbOutputConstIterator(getOutput, amount, 0);
        auto end = DbOutputConstIterator(getOutput, amount, outputsCount);

        auto it = std::lower_bound(begin,
                                   end,
                                   blockIndex,
                                   [](const PackedOutIndex &output, uint32_t blockIndex)
                                   { return output.blockIndex < blockIndex; });

        size_t result = static_cast<size_t>(std::distance(begin, it));
        logger(Logging::DEBUGGING) << "Key outputs count for amount " << amount << " is " << result
                                   << " by block index " << blockIndex;

        return result;
    }

    std::tuple<bool, uint64_t> DatabaseBlockchainCache::getBlockHeightForTimestamp(uint64_t timestamp) const
    {
        /* One entry per day, holding the first block mined that day. Asking
           for a day with no entry used to give up here, and a day with no
           entry is ordinary: a node that fast-synced or a lite node has none
           below its floor, a stalled chain skips days outright, and every day
           has none until its first block is mined. A wallet whose scan start
           landed on such a day was served nothing at all.

           So walk back to the nearest earlier day that does have an entry, as
           getTimestampLowerBoundBlockIndex below already does. The genesis day
           is always indexed, so the walk ends there at the latest. Starting a
           scan earlier than asked costs time; not starting one costs the
           wallet every transaction it has. */
        auto midnight = roundToMidnight(timestamp);

        while (true)
        {
            const auto [blockHeight, success] = requestClosestBlockIndexByTimestamp(midnight, database);

            /* Failed to read from DB */
            if (!success)
            {
                logger(Logging::DEBUGGING) << "getBlockHeightForTimestamp failed: failed to read database";
                throw std::runtime_error("Couldn't get closest to timestamp block index");
            }

            if (blockHeight)
            {
                return {true, *blockHeight};
            }

            /* Ran out of days before finding one. Nothing in this database is
               older than the timestamp asked for. */
            if (midnight < ONE_DAY_SECONDS)
            {
                return {false, 0};
            }

            midnight -= ONE_DAY_SECONDS;
        }
    }

    uint32_t DatabaseBlockchainCache::getTimestampLowerBoundBlockIndex(uint64_t timestamp) const
    {
        auto midnight = roundToMidnight(timestamp);

        while (midnight > 0)
        {
            auto dbRes = requestClosestBlockIndexByTimestamp(midnight, database);
            if (!dbRes.second)
            {
                logger(Logging::DEBUGGING) << "getTimestampLowerBoundBlockIndex failed: failed to read database";
                throw std::runtime_error("Couldn't get closest to timestamp block index");
            }

            if (!dbRes.first)
            {
                midnight -= 60 * 60 * 24;
                continue;
            }

            return *dbRes.first;
        }

        return 0;
    }

    bool DatabaseBlockchainCache::getTransactionGlobalIndexes(const Crypto::Hash &transactionHash,
                                                              std::vector<uint32_t> &globalIndexes) const
    {
        auto batch = BlockchainReadBatch().requestCachedTransaction(transactionHash);
        auto result = database.read(batch);
        if (result)
        {
            logger(Logging::DEBUGGING) << "getTransactionGlobalIndexes failed: failed to read database";
            return false;
        }

        auto readResult = batch.extractResult();
        auto it = readResult.getCachedTransactions().find(transactionHash);
        if (it == readResult.getCachedTransactions().end())
        {
            logger(Logging::DEBUGGING) << "getTransactionGlobalIndexes failed: cached transaction for hash "
                                       << transactionHash << " not present";
            return false;
        }

        globalIndexes = it->second.globalIndexes;
        return true;
    }

    size_t DatabaseBlockchainCache::getTransactionCount() const
    {
        return static_cast<size_t>(getCachedTransactionsCount());
    }

    uint32_t DatabaseBlockchainCache::getBlockIndexContainingTx(const Crypto::Hash &transactionHash) const
    {
        auto batch = BlockchainReadBatch().requestCachedTransaction(transactionHash);
        auto result = readDatabase(batch);
        return result.getCachedTransactions().at(transactionHash).blockIndex;
    }

    size_t DatabaseBlockchainCache::getChildCount() const
    {
        return children.size();
    }

    void DatabaseBlockchainCache::save() {}

    void DatabaseBlockchainCache::load() {}

    std::vector<BinaryArray> DatabaseBlockchainCache::getRawTransactions(
        const std::vector<Crypto::Hash> &transactions,
        std::vector<Crypto::Hash> &missedTransactions) const
    {
        std::vector<BinaryArray> found;
        getRawTransactions(transactions, found, missedTransactions);
        return found;
    }

    std::vector<BinaryArray> DatabaseBlockchainCache::getRawTransactions(
        const std::vector<Crypto::Hash> &transactions) const
    {
        std::vector<Crypto::Hash> missed;
        std::vector<BinaryArray> found;
        getRawTransactions(transactions, found, missed);
        return found;
    }

    void DatabaseBlockchainCache::getRawTransactions(const std::vector<Crypto::Hash> &transactions,
                                                     std::vector<BinaryArray> &foundTransactions,
                                                     std::vector<Crypto::Hash> &missedTransactions) const
    {
        if (transactions.empty())
        {
            return;
        }

        BlockchainReadBatch batch;
        for (auto &hash : transactions)
        {
            batch.requestCachedTransaction(hash);
        }

        auto res = readDatabase(batch);
        for (auto &tx : res.getCachedTransactions())
        {
            batch.requestRawBlock(tx.second.blockIndex);
        }

        foundTransactions.reserve(foundTransactions.size() + transactions.size());
        auto &hashesMap = res.getCachedTransactions();

        /* If no transactions were found in the DB (e.g. anchor/synthetic blocks whose
           coinbase is not indexed), skip the second read to avoid passing an empty batch
           to readDatabase, which would trigger an internal error. */
        if (hashesMap.empty())
        {
            for (const auto &hash : transactions)
            {
                missedTransactions.push_back(hash);
            }
            return;
        }

        auto blocks = readDatabase(batch);
        auto &blocksMap = blocks.getRawBlocks();
        for (const auto &hash : transactions)
        {
            auto transactionIt = hashesMap.find(hash);
            if (transactionIt == hashesMap.end())
            {
                logger(Logging::DEBUGGING)
                    << "detected missing transaction for hash " << hash << " in getRawTransaction";
                missedTransactions.push_back(hash);
                continue;
            }

            auto blockIt = blocksMap.find(transactionIt->second.blockIndex);
            if (blockIt == blocksMap.end())
            {
                logger(Logging::DEBUGGING)
                    << "detected missing transaction for hash " << hash << " in getRawTransaction";
                missedTransactions.push_back(hash);
                continue;
            }

            if (transactionIt->second.transactionIndex == 0)
            {
                auto block = fromBinaryArray<BlockTemplate>(blockIt->second.block);
                foundTransactions.emplace_back(toBinaryArray(block.baseTransaction));
            }
            else
            {
                assert(blockIt->second.transactions.size() >= transactionIt->second.transactionIndex - 1);
                foundTransactions.emplace_back(
                    blockIt->second.transactions[transactionIt->second.transactionIndex - 1]);
            }
        }
    }

    RawBlock DatabaseBlockchainCache::getBlockByIndex(uint32_t index) const
    {
        if (index > 0 && index < getPruneFloor())
        {
            throw std::out_of_range(
                "Block " + std::to_string(index) + " has been pruned (prune floor: "
                + std::to_string(getPruneFloor()) + ")");
        }
        auto batch = BlockchainReadBatch().requestRawBlock(index);
        auto res = readDatabase(batch);
        return std::move(res.getRawBlocks().at(index));
    }

    BinaryArray DatabaseBlockchainCache::getRawTransaction(uint32_t blockIndex, uint32_t transactionIndex) const
    {
        return getBlockByIndex(blockIndex).transactions.at(transactionIndex);
    }

    std::vector<Crypto::Hash> DatabaseBlockchainCache::getTransactionHashes() const
    {
        assert(false);
        return {};
    }

    std::vector<uint32_t> DatabaseBlockchainCache::getRandomOutsByAmount(uint64_t amount,
                                                                         size_t count,
                                                                         uint32_t blockIndex) const
    {
        auto batch = BlockchainReadBatch().requestKeyOutputGlobalIndexesCountForAmount(amount);
        auto result = readDatabase(batch);
        auto outputsCount = result.getKeyOutputGlobalIndexesCountForAmounts();
        auto outputsToPick = std::min(static_cast<uint32_t>(count), outputsCount[amount]);

        std::vector<uint32_t> resultOuts;
        resultOuts.reserve(outputsToPick);

        ShuffleGenerator<uint32_t> generator(outputsCount[amount]);

        while (outputsToPick)
        {
            std::vector<uint32_t> globalIndexes;
            globalIndexes.reserve(outputsToPick);

            try
            {
                for (uint32_t i = 0; i < outputsToPick; ++i, globalIndexes.push_back(generator()))
                {
                }
                // std::generate_n(std::back_inserter(globalIndexes), outputsToPick, generator);
            }
            catch (const SequenceEnded &)
            {
                logger(Logging::TRACE) << "getRandomOutsByAmount: generator reached sequence end";
                return resultOuts;
            }

            std::vector<PackedOutIndex> outputs;
            if (extractKeyOtputIndexes(amount,
                                       Common::ArrayView<uint32_t>(globalIndexes.data(), globalIndexes.size()),
                                       outputs)
                != ExtractOutputKeysResult::SUCCESS)
            {
                logger(Logging::DEBUGGING) << "getRandomOutsByAmount: failed to extract key output indexes";
                throw std::runtime_error("Invalid output index"); // TODO: make error code
            }

            std::vector<ExtendedTransactionInfo> transactions;
            if (!requestExtendedTransactionInfos(outputs, database, transactions))
            {
                logger(Logging::TRACE) << "getRandomOutsByAmount: requestExtendedTransactionInfos failed";
                throw std::runtime_error("Error while requesting transactions"); // TODO: make error code
            }

            assert(globalIndexes.size() == transactions.size());

            uint32_t uppperBlockIndex = 0;

            /* Only select unlocked outputs. */
            if (blockIndex >= CryptoNote::parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW_V2_HEIGHT
                                  + CryptoNote::parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW_V2)
            {
                uppperBlockIndex = blockIndex - CryptoNote::parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW_V2;
            }
            else if (blockIndex >= CryptoNote::parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW)
            {
                uppperBlockIndex = blockIndex - CryptoNote::parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW;
            }

            for (size_t i = 0; i < transactions.size(); ++i)
            {
                if (!isTransactionSpendTimeUnlocked(transactions[i].unlockTime, blockIndex)
                    || transactions[i].blockIndex > uppperBlockIndex)
                {
                    continue;
                }

                resultOuts.push_back(globalIndexes[i]);
                --outputsToPick;
            }
        }

        return resultOuts;
    }

    ExtractOutputKeysResult DatabaseBlockchainCache::extractKeyOutputs(
        uint64_t amount,
        uint32_t blockIndex,
        Common::ArrayView<uint32_t> globalIndexes,
        std::function<ExtractOutputKeysResult(const CachedTransactionInfo &info,
                                              PackedOutIndex index,
                                              uint32_t globalIndex)> callback) const
    {
        BlockchainReadBatch batch;
        for (auto it = globalIndexes.begin(); it != globalIndexes.end(); ++it)
        {
            batch.requestKeyOutputInfo(amount, *it);
        }

        auto result = readDatabase(batch).getKeyOutputInfo();
        std::map<std::pair<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex>, KeyOutputInfo> sortedResult(
            result.begin(),
            result.end());
        for (const auto &kv : sortedResult)
        {
            ExtendedTransactionInfo tx;
            tx.unlockTime = kv.second.unlockTime;
            tx.transactionHash = kv.second.transactionHash;
            tx.outputs.resize(kv.second.outputIndex + 1);
            tx.outputs[kv.second.outputIndex] = KeyOutput {kv.second.publicKey};
            PackedOutIndex fakePoi;
            fakePoi.outputIndex = kv.second.outputIndex;

            // TODO: change the interface of extractKeyOutputs to return vector of structures instead of passing
            // callback as predicate
            auto ret = callback(tx, fakePoi, kv.first.second);
            if (ret != ExtractOutputKeysResult::SUCCESS)
            {
                logger(Logging::DEBUGGING) << "extractKeyOutputs failed : callback returned error";
                return ret;
            }
        }

        return ExtractOutputKeysResult::SUCCESS;
    }

    std::vector<Crypto::Hash> DatabaseBlockchainCache::getTransactionHashesByPaymentId(
        const Crypto::Hash &paymentId) const
    {
        auto countBatch = BlockchainReadBatch().requestTransactionCountByPaymentId(paymentId);
        uint32_t transactionsCountByPaymentId =
            readDatabase(countBatch).getTransactionCountByPaymentIds().at(paymentId);

        BlockchainReadBatch transactionBatch;
        for (uint32_t i = 0; i < transactionsCountByPaymentId; ++i)
        {
            transactionBatch.requestTransactionHashByPaymentId(paymentId, i);
        }

        auto result = readDatabase(transactionBatch);
        std::vector<Crypto::Hash> transactionHashes;
        transactionHashes.reserve(result.getTransactionHashesByPaymentIds().size());
        for (const auto &kv : result.getTransactionHashesByPaymentIds())
        {
            transactionHashes.emplace_back(kv.second);
        }

        return transactionHashes;
    }

    std::vector<Crypto::Hash> DatabaseBlockchainCache::getBlockHashesByTimestamps(uint64_t timestampBegin,
                                                                                  size_t secondsCount) const
    {
        std::vector<Crypto::Hash> blockHashes;
        if (secondsCount == 0)
        {
            return blockHashes;
        }

        BlockchainReadBatch batch;
        for (uint64_t timestamp = timestampBegin; timestamp < timestampBegin + static_cast<uint64_t>(secondsCount);
             ++timestamp)
        {
            batch.requestBlockHashesByTimestamp(timestamp);
        }

        auto result = readDatabase(batch);
        for (uint64_t timestamp = timestampBegin; timestamp < timestampBegin + static_cast<uint64_t>(secondsCount);
             ++timestamp)
        {
            if (result.getBlockHashesByTimestamp().count(timestamp) == 0)
            {
                continue;
            }

            const auto &hashes = result.getBlockHashesByTimestamp().at(timestamp);
            blockHashes.insert(blockHashes.end(), hashes.begin(), hashes.end());
        }

        return blockHashes;
    }

    uint64_t DatabaseBlockchainCache::getMinRawBlockHeight(uint64_t fromHeight) const
    {
        const uint64_t storageBlockCount = getBlockCount();

        if (fromHeight >= storageBlockCount)
        {
            return fromHeight;
        }

        /* Fast path: probe the requested height directly. This is the common
           case (the range is not pruned) and answers the question in a single
           read instead of a log2(chain) binary search on every RPC request. */
        {
            auto batch = BlockchainReadBatch().requestRawBlock(static_cast<uint32_t>(fromHeight));

            if (!readDatabase(batch).getRawBlocks().empty())
            {
                return fromHeight;
            }
        }

        uint64_t lo = fromHeight + 1, hi = storageBlockCount;

        while (lo < hi)
        {
            uint64_t mid = lo + (hi - lo) / 2;
            auto batch = BlockchainReadBatch().requestRawBlock(static_cast<uint32_t>(mid));
            const auto result = readDatabase(batch).getRawBlocks();

            if (!result.empty())
            {
                hi = mid;
            }
            else
            {
                lo = mid + 1;
            }
        }

        return lo;
    }

    std::vector<RawBlock> DatabaseBlockchainCache::getNonEmptyBlocks(const uint64_t startHeight,
                                                                     const size_t blockCount) const
    {
        std::vector<RawBlock> orderedBlocks;

        const uint32_t storageBlockCount = getBlockCount();

        uint64_t height = startHeight;

        while (orderedBlocks.size() < blockCount && height < storageBlockCount)
        {
            uint64_t batchStart = height;

            /* Lets try taking the amount we need *2, to try and balance not needing
               multiple DB requests to get the amount we need of non empty blocks, with
               not taking too many */
            uint64_t endHeight = batchStart + (blockCount * 2);

            auto blockBatch = BlockchainReadBatch().requestRawBlocks(batchStart, endHeight);
            const auto rawBlocks = readDatabase(blockBatch).getRawBlocks();

            if (rawBlocks.empty())
            {
                /* All blocks in this batch were pruned. Binary-search for the first
                   available raw block to jump directly to the prune floor instead of
                   scanning O(N/batch) sequential DB reads. */
                const uint64_t nextHeight = getMinRawBlockHeight(batchStart);

                /* Forward progress guard: if the search cannot advance past the
                   current position (only reachable if the chain changed under us
                   between the two reads), stop rather than spin forever. */
                if (nextHeight <= batchStart)
                {
                    break;
                }

                height = nextHeight;
                continue;
            }

            /* Sort by height for ordered iteration. Pruned entries were already
               erased from the map by deserializeValues, so gaps are skipped. */
            std::map<uint32_t, RawBlock> sorted(rawBlocks.begin(), rawBlocks.end());

            for (const auto &[h, block] : sorted)
            {
                height = h + 1;

                if (block.transactions.empty())
                {
                    continue;
                }

                orderedBlocks.push_back(block);

                if (orderedBlocks.size() >= blockCount)
                {
                    break;
                }
            }
        }

        return orderedBlocks;
    }

    std::vector<RawBlock> DatabaseBlockchainCache::getBlocksByHeight(const uint64_t startHeight,
                                                                     uint64_t endHeight) const
    {
        auto blockBatch = BlockchainReadBatch().requestRawBlocks(startHeight, endHeight);

        /* Get the info from the DB */
        auto rawBlocks = readDatabase(blockBatch).getRawBlocks();

        /* Sort by height and return only entries found in DB. Pruned entries were
           erased from the map by deserializeValues, so gaps are silently skipped. */
        std::map<uint32_t, RawBlock> sorted(rawBlocks.begin(), rawBlocks.end());

        std::vector<RawBlock> orderedBlocks;

        for (const auto &[height, block] : sorted)
        {
            orderedBlocks.push_back(block);
        }

        return orderedBlocks;
    }

    std::vector<WalletTypes::WalletBlockInfo> DatabaseBlockchainCache::getPrunedWalletBlocks(
        uint64_t startHeight,
        uint64_t endHeight,
        bool skipCoinbaseTransactions) const
    {
        std::vector<WalletTypes::WalletBlockInfo> result;

        if (startHeight >= endHeight)
        {
            return result;
        }

        /* Cap to available chain height */
        const uint64_t storageCount = static_cast<uint64_t>(getBlockCount());

        logger(Logging::DEBUGGING) << "getPrunedWalletBlocks: [" << startHeight << ", " << endHeight
                              << ") storageCount=" << storageCount;

        if (endHeight > storageCount)
        {
            endHeight = storageCount;
        }

        /* Track which heights we've covered so we can fall back to legacy
           for any gaps where "w" records are absent. */
        std::set<uint64_t> coveredHeights;

        /* Read compact wallet sync records stored at push time under the "w" prefix.
           These contain complete WalletBlockInfo (outputs + key images + payment IDs)
           and are never deleted by the prune pass.
           Process in batches of 100 to keep DB read sizes reasonable. */
        constexpr uint64_t BATCH_SIZE = 100;

        for (uint64_t batchStart = startHeight; batchStart < endHeight; batchStart += BATCH_SIZE)
        {
            const uint64_t batchEnd = std::min(batchStart + BATCH_SIZE, endHeight);

            BlockchainReadBatch batch;
            batch.requestWalletSyncBlocks(batchStart, batchEnd);

            /* Use the batched read. readThreadSafe issues a separate lookup per
               key, so serving a hundred-block batch cost a hundred round trips
               into the database instead of one; the rest of this class already
               reads through the same batched path from these threads. */
            if (database.read(batch)) { continue; }

            auto res = batch.extractResult();
            const auto &walletBlocks = res.getWalletSyncBlocks();

            /* Return blocks in height order */
            for (uint64_t h = batchStart; h < batchEnd; ++h)
            {
                auto it = walletBlocks.find(static_cast<uint32_t>(h));
                if (it == walletBlocks.end()) { continue; }

                WalletTypes::WalletBlockInfo block = it->second;
                if (skipCoinbaseTransactions)
                {
                    block.coinbaseTransaction = std::nullopt;
                }
                result.push_back(std::move(block));
                coveredHeights.insert(h);
            }
        }

        /* If some heights are missing "w" records (DB predates this feature),
           fill the gaps using the legacy reconstruction path. */
        const uint64_t expectedCount = endHeight - startHeight;

        logger(Logging::DEBUGGING) << "getPrunedWalletBlocks: w-records covered " << coveredHeights.size()
                              << " of " << expectedCount << " heights, result so far: " << result.size();

        if (coveredHeights.size() < expectedCount)
        {
            /* Rebuild only the heights actually missing. This used to rerun the
               legacy path over the whole range whenever a single height lacked
               a record, and one always does near the start of the chain because
               genesis is pushed without one, so every wallet syncing from zero
               paid for a full reconstruction of each batch and then discarded
               nearly all of it. */
            uint64_t firstMissing = endHeight;
            uint64_t lastMissing = startHeight;

            for (uint64_t h = startHeight; h < endHeight; ++h)
            {
                if (coveredHeights.find(h) == coveredHeights.end())
                {
                    firstMissing = std::min(firstMissing, h);
                    lastMissing = std::max(lastMissing, h);
                }
            }

            if (firstMissing < endHeight)
            {
                auto legacyBlocks =
                    getPrunedWalletBlocksLegacy(firstMissing, lastMissing + 1, skipCoinbaseTransactions);

                logger(Logging::DEBUGGING)
                    << "getPrunedWalletBlocksLegacy returned " << legacyBlocks.size() << " blocks for ["
                    << firstMissing << ", " << (lastMissing + 1) << ")";

                for (auto &block : legacyBlocks)
                {
                    if (coveredHeights.find(block.blockHeight) == coveredHeights.end())
                    {
                        result.push_back(std::move(block));
                    }
                }

                /* Re-sort by height after merging */
                std::sort(result.begin(), result.end(),
                    [](const auto &a, const auto &b) { return a.blockHeight < b.blockHeight; });
            }
        }

        logger(Logging::DEBUGGING) << "getPrunedWalletBlocks: final result " << result.size() << " blocks";

        return result;
    }

    /* Legacy reconstruction path — kept for databases upgraded without a resync.
       Reconstructs partial WalletBlockInfo from surviving DB fragments.
       Missing: key images (spent detection) and payment IDs.
       Only called if "w" records are absent for a height range. */
    std::vector<WalletTypes::WalletBlockInfo> DatabaseBlockchainCache::getPrunedWalletBlocksLegacy(
        uint64_t startHeight,
        uint64_t endHeight,
        bool skipCoinbaseTransactions) const
    {
        std::vector<WalletTypes::WalletBlockInfo> result;

        if (startHeight >= endHeight) { return result; }

        const uint64_t storageCount = static_cast<uint64_t>(getBlockCount());
        if (endHeight > storageCount) { endHeight = storageCount; }

        constexpr uint64_t BATCH_SIZE = 100;

        for (uint64_t batchStart = startHeight; batchStart < endHeight; batchStart += BATCH_SIZE)
        {
            const uint64_t batchEnd = std::min(batchStart + BATCH_SIZE, endHeight);

            /* Step 1: read block infos and tx hash lists for this batch */
            BlockchainReadBatch blockBatch;
            for (uint64_t h = batchStart; h < batchEnd; ++h)
            {
                blockBatch.requestCachedBlock(static_cast<uint32_t>(h));
                blockBatch.requestTransactionHashesByBlock(static_cast<uint32_t>(h));
            }

            std::optional<BlockchainReadResult> blockResultOpt;
            try
            {
                blockResultOpt.emplace(readDatabase(blockBatch));
            }
            catch (const std::exception &e)
            {
                logger(Logging::ERROR) << "getPrunedWalletBlocksLegacy: batch readDatabase failed for ["
                                       << batchStart << ", " << batchEnd << "): " << e.what();
                continue;
            }

            const auto &blockInfos = blockResultOpt->getCachedBlocks();
            const auto &txHashesByBlock = blockResultOpt->getTransactionHashesByBlocks();

            logger(Logging::DEBUGGING) << "getPrunedWalletBlocksLegacy: batch [" << batchStart
                                  << ", " << batchEnd << ") found " << blockInfos.size()
                                  << " blockInfos, " << txHashesByBlock.size() << " txHashesByBlock";

            for (uint64_t h = batchStart; h < batchEnd; ++h)
            {
                try
                {
                    const auto biIt = blockInfos.find(static_cast<uint32_t>(h));
                    if (biIt == blockInfos.end())
                    {
                        logger(Logging::WARNING) << "getPrunedWalletBlocksLegacy: missing CachedBlockInfo at height " << h;
                        continue;
                    }
                    const auto txIt = txHashesByBlock.find(static_cast<uint32_t>(h));

                    const CachedBlockInfo &bi = biIt->second;

                    WalletTypes::WalletBlockInfo walletBlock;
                    walletBlock.blockHeight = h;
                    walletBlock.blockHash = bi.blockHash;
                    walletBlock.blockTimestamp = bi.timestamp;

                    if (txIt == txHashesByBlock.end())
                    {
                        /* No tx-hash list for this block; emit a metadata-only beacon so
                           the wallet advances its height counter through the pruned range. */
                        result.push_back(walletBlock);
                        continue;
                    }

                    const std::vector<Crypto::Hash> &txHashes = txIt->second;

                    if (txHashes.empty())
                    {
                        result.push_back(walletBlock);
                        continue;
                    }

                    /* Step 2: read tx infos + tx public keys for all txs in this block */
                    BlockchainReadBatch txBatch;
                    txBatch.requestCachedTransactions(txHashes);
                    txBatch.requestTransactionPublicKeys(txHashes);
                    auto txResult = readDatabase(txBatch);
                    const auto &txInfos = txResult.getCachedTransactions();
                    const auto &txPubKeys = txResult.getTransactionPublicKeys();

                    for (const auto &txHash : txHashes)
                    {
                        auto txInfoIt = txInfos.find(txHash);
                        if (txInfoIt == txInfos.end())
                        {
                            continue;
                        }
                        const ExtendedTransactionInfo &txInfo = txInfoIt->second;

                        /* Retrieve stored transaction public key (may be zero if tx predates this feature) */
                        Crypto::PublicKey txPubKey{};
                        auto pkIt = txPubKeys.find(txHash);
                        if (pkIt != txPubKeys.end())
                        {
                            txPubKey = pkIt->second;
                        }

                        /* Build reverse map: globalIndex → amount, from amountToKeyIndexes */
                        std::unordered_map<uint32_t, uint64_t> globalIndexToAmount;
                        for (const auto &[amount, globalIndices] : txInfo.amountToKeyIndexes)
                        {
                            for (const auto gIdx : globalIndices)
                            {
                                globalIndexToAmount[gIdx] = amount;
                            }
                        }

                        /* Build key outputs list */
                        std::vector<WalletTypes::KeyOutput> keyOutputs;
                        keyOutputs.reserve(txInfo.outputs.size());
                        for (size_t i = 0; i < txInfo.outputs.size(); ++i)
                        {
                            const auto &target = txInfo.outputs[i];
                            if (!std::holds_alternative<CryptoNote::KeyOutput>(target))
                            {
                                continue;
                            }
                            WalletTypes::KeyOutput ko;
                            ko.key = std::get<CryptoNote::KeyOutput>(target).key;
                            ko.amount = 0;
                            if (i < txInfo.globalIndexes.size())
                            {
                                const uint32_t gIdx = txInfo.globalIndexes[i];
                                auto amtIt = globalIndexToAmount.find(gIdx);
                                if (amtIt != globalIndexToAmount.end())
                                {
                                    ko.amount = amtIt->second;
                                }
                                ko.globalOutputIndex = gIdx;
                            }
                            keyOutputs.push_back(ko);
                        }

                        /* transactionIndex == 0 means coinbase */
                        if (txInfo.transactionIndex == 0)
                        {
                            if (!skipCoinbaseTransactions)
                            {
                                WalletTypes::RawCoinbaseTransaction coinbaseTx;
                                coinbaseTx.hash = txHash;
                                coinbaseTx.transactionPublicKey = txPubKey;
                                coinbaseTx.unlockTime = txInfo.unlockTime;
                                coinbaseTx.keyOutputs = std::move(keyOutputs);
                                walletBlock.coinbaseTransaction = std::move(coinbaseTx);
                            }
                        }
                        else
                        {
                            WalletTypes::RawTransaction tx;
                            tx.hash = txHash;
                            tx.transactionPublicKey = txPubKey;
                            tx.unlockTime = txInfo.unlockTime;
                            tx.keyOutputs = std::move(keyOutputs);
                            /* keyInputs and paymentID are not preserved after pruning */
                            walletBlock.transactions.push_back(std::move(tx));
                        }
                    }

                    result.push_back(walletBlock);
                }
                catch (const std::exception &e)
                {
                    logger(Logging::WARNING) << "getPrunedWalletBlocksLegacy: failed at height "
                                             << h << ": " << e.what();
                    /* Push a metadata-only block so the wallet still advances its
                       height counter through the pruned range. The block's hash and
                       timestamp come from CachedBlockInfo which was already read
                       successfully in the outer batch. */
                    const auto biIt2 = blockInfos.find(static_cast<uint32_t>(h));
                    if (biIt2 != blockInfos.end())
                    {
                        WalletTypes::WalletBlockInfo fallbackBlock;
                        fallbackBlock.blockHeight = h;
                        fallbackBlock.blockHash = biIt2->second.blockHash;
                        fallbackBlock.blockTimestamp = biIt2->second.timestamp;
                        result.push_back(std::move(fallbackBlock));
                    }
                }
            }
        }

        return result;
    }

    std::unordered_map<Crypto::Hash, std::vector<uint64_t>> DatabaseBlockchainCache::getGlobalIndexes(
        const std::vector<Crypto::Hash> transactionHashes) const
    {
        auto txBatch = BlockchainReadBatch().requestCachedTransactions(transactionHashes);

        database.read(txBatch);

        auto txs = txBatch.extractResult().getCachedTransactions();

        std::unordered_map<Crypto::Hash, std::vector<uint64_t>> indexes;

        for (const auto &[txHash, transaction] : txs)
        {
            indexes[txHash].assign(transaction.globalIndexes.begin(), transaction.globalIndexes.end());
        }

        return indexes;
    }

    DatabaseBlockchainCache::ExtendedPushedBlockInfo DatabaseBlockchainCache::getExtendedPushedBlockInfo(
        uint32_t blockIndex) const
    {
        assert(blockIndex <= getTopBlockIndex());

        auto batch = BlockchainReadBatch()
                         .requestRawBlock(blockIndex)
                         .requestCachedBlock(blockIndex)
                         .requestSpentKeyImagesByBlock(blockIndex);

        if (blockIndex > 0)
        {
            batch.requestCachedBlock(blockIndex - 1);
        }

        auto dbResult = readDatabase(batch);
        const CachedBlockInfo &blockInfo = dbResult.getCachedBlocks().at(blockIndex);
        const CachedBlockInfo &previousBlockInfo =
            blockIndex > 0 ? dbResult.getCachedBlocks().at(blockIndex - 1) : NULL_CACHED_BLOCK_INFO;

        ExtendedPushedBlockInfo extendedInfo;

        extendedInfo.pushedBlockInfo.rawBlock = dbResult.getRawBlocks().at(blockIndex);
        extendedInfo.pushedBlockInfo.blockSize = blockInfo.blockSize;
        extendedInfo.pushedBlockInfo.blockDifficulty =
            blockInfo.cumulativeDifficulty - previousBlockInfo.cumulativeDifficulty;
        extendedInfo.pushedBlockInfo.generatedCoins =
            blockInfo.alreadyGeneratedCoins - previousBlockInfo.alreadyGeneratedCoins;

        const auto &spentKeyImages = dbResult.getSpentKeyImagesByBlock().at(blockIndex);

        extendedInfo.pushedBlockInfo.validatorState.spentKeyImages.insert(spentKeyImages.begin(), spentKeyImages.end());

        extendedInfo.timestamp = blockInfo.timestamp;

        return extendedInfo;
    }

    void DatabaseBlockchainCache::setParent(IBlockchainCache *ptr)
    {
        assert(false);
    }

    void DatabaseBlockchainCache::addChild(IBlockchainCache *ptr)
    {
        if (std::find(children.begin(), children.end(), ptr) == children.end())
        {
            children.push_back(ptr);
        }
    }

    bool DatabaseBlockchainCache::deleteChild(IBlockchainCache *ptr)
    {
        auto it = std::remove(children.begin(), children.end(), ptr);
        auto res = it != children.end();
        children.erase(it, children.end());
        return res;
    }

    BlockchainReadResult DatabaseBlockchainCache::readDatabase(BlockchainReadBatch &batch) const
    {
        auto result = database.read(batch);
        if (result)
        {
            logger(Logging::ERROR) << "failed to read database, error is " << result.message();
            throw std::runtime_error(result.message());
        }

        return batch.extractResult();
    }

    /* --sync-from-height bootstrap helpers ---------------------------------------- */

    namespace
    {
        const std::string SYNC_FLOOR_KEY = "sync_floor_height";
    } // namespace

    uint32_t DatabaseBlockchainCache::getSyncFloorHeight() const
    {
        struct SyncFloorReadBatch : public IReadBatch
        {
            std::vector<std::string> getRawKeys() const override { return {SYNC_FLOOR_KEY}; }
            void submitRawResult(const std::vector<std::string> &values,
                                 const std::vector<bool> &states) override
            {
                if (!states.empty() && states[0])
                    height = static_cast<uint32_t>(std::stoul(values[0]));
            }
            std::optional<uint32_t> height;
        };

        SyncFloorReadBatch batch;
        auto ec = database.read(batch);
        if (ec)
        {
            logger(Logging::ERROR) << "getSyncFloorHeight: read error: " << ec.message();
            return 0;
        }
        return batch.height.value_or(0);
    }

    void DatabaseBlockchainCache::injectBootstrapAnchor(
        uint32_t anchorHeight,
        const Crypto::Hash &anchorHash,
        uint64_t anchorTimestamp,
        uint64_t alreadyGeneratedCoins,
        uint64_t cumulativeDifficulty,
        uint64_t alreadyGeneratedTransactions,
        uint64_t windowCumulDiff,
        uint64_t anchorPrevBlockDiff,
        const uint64_t *lwmaTimestamps)
    {
        logger(Logging::INFO)
            << "injectBootstrapAnchor: injecting sync anchor at height " << anchorHeight
            << " (" << Common::podToHex(anchorHash) << ")";

        /* Build a minimal CachedBlockInfo for the anchor. */
        CachedBlockInfo anchorInfo;
        anchorInfo.blockHash                    = anchorHash;
        anchorInfo.timestamp                    = anchorTimestamp;
        anchorInfo.cumulativeDifficulty         = cumulativeDifficulty;
        anchorInfo.alreadyGeneratedCoins        = alreadyGeneratedCoins;
        anchorInfo.alreadyGeneratedTransactions = alreadyGeneratedTransactions;
        anchorInfo.blockSize =
            static_cast<uint32_t>(currency.blockGrantedFullRewardZone());

        BlockchainWriteBatch anchorBatch;

        /* Insert DIFFICULTY_BLOCKS_COUNT-1 synthetic CachedBlockInfo entries at
         * heights [anchorHeight-N .. anchorHeight-1].  Without these, the first
         * 60 blocks after the anchor are computed with the LWMA startup guess
         * (10000) because getLastTimestamps() finds fewer than 61 DB entries.
         * Those wrong per-block difficulties are stored in cumulativeDifficulty and
         * permanently corrupt the LWMA – the network difficulty (~31 M) never
         * recovers.  The synthetic entries give LWMA exactly 61 data points
         * immediately so block anchorHeight+1 gets the correct difficulty. */
        {
            const uint32_t N = static_cast<uint32_t>(CryptoNote::parameters::DIFFICULTY_BLOCKS_COUNT) - 1; // 60

            /* Determine per-block cumDiff spacing.
             *
             * When anchorPrevBlockDiff > 0, use it as the last synthetic block's
             * contribution (= D[anchorHeight] = cumDiff[anchor] - cumDiff[anchor-1]).
             * The remaining N-1 blocks split the leftover evenly.  This gives the
             * LWMA's prev_D the exact real-network value, which controls clamping.
             *
             * Fall back to uniform windowCumulDiff/N spacing when anchorPrevBlockDiff
             * is not provided. */
            const uint64_t lastSynthDiff = (anchorPrevBlockDiff > 0)
                ? anchorPrevBlockDiff
                : ((windowCumulDiff > 0) ? windowCumulDiff / N : cumulativeDifficulty / anchorHeight);
            const uint64_t otherAvgDiff  = (anchorPrevBlockDiff > 0 && N > 1)
                ? (windowCumulDiff - anchorPrevBlockDiff) / (N - 1)
                : lastSynthDiff;

            /* Determine per-block timestamp spacing.
             *
             * lwmaTimestamps[0..60] are the exact on-chain timestamps for heights
             * [anchorHeight-60 .. anchorHeight].  When provided they are stored
             * directly so that L = sum(ST[i]*i) in the LWMA formula exactly matches
             * the real network's value.  Without them we fall back to uniform
             * DIFFICULTY_TARGET_V3 spacing from the anchor timestamp. */
            const bool haveExactTs = (lwmaTimestamps != nullptr) && (lwmaTimestamps[0] != 0);
            const uint64_t blockTime = CryptoNote::parameters::DIFFICULTY_TARGET_V3;

            for (uint32_t i = 0; i < N; ++i)
            {
                const uint32_t syntheticHeight = anchorHeight - N + i; // anchorHeight-60 .. anchorHeight-1
                const uint32_t stepsFromAnchor = anchorHeight - syntheticHeight; // N..1

                CachedBlockInfo synthInfo;

                /* Unique dummy hash: all-zero except last 4 bytes encode the height. */
                synthInfo.blockHash = Constants::NULL_HASH;
                synthInfo.blockHash.data[28] = static_cast<uint8_t>((syntheticHeight >> 24) & 0xFF);
                synthInfo.blockHash.data[29] = static_cast<uint8_t>((syntheticHeight >> 16) & 0xFF);
                synthInfo.blockHash.data[30] = static_cast<uint8_t>((syntheticHeight >>  8) & 0xFF);
                synthInfo.blockHash.data[31] = static_cast<uint8_t>((syntheticHeight >>  0) & 0xFF);

                /* Interpolate cumulativeDifficulty backwards from the anchor.
                 * The block immediately before the anchor uses anchorPrevBlockDiff;
                 * all earlier blocks use otherAvgDiff. */
                if (stepsFromAnchor == 1)
                {
                    synthInfo.cumulativeDifficulty = cumulativeDifficulty - lastSynthDiff;
                }
                else
                {
                    synthInfo.cumulativeDifficulty =
                        cumulativeDifficulty - lastSynthDiff
                        - static_cast<uint64_t>(stepsFromAnchor - 1) * otherAvgDiff;
                }

                /* Timestamp: exact from checkpoint data when available, otherwise
                 * linear extrapolation from anchor at one block-time per step. */
                if (haveExactTs)
                {
                    // lwmaTimestamps index 0 = anchorHeight-60, index 60 = anchorHeight
                    synthInfo.timestamp = lwmaTimestamps[i];
                }
                else
                {
                    synthInfo.timestamp = anchorTimestamp - static_cast<uint64_t>(stepsFromAnchor) * blockTime;
                }

                synthInfo.alreadyGeneratedCoins        = alreadyGeneratedCoins;
                synthInfo.alreadyGeneratedTransactions = alreadyGeneratedTransactions;
                synthInfo.blockSize = static_cast<uint32_t>(currency.blockGrantedFullRewardZone());

                anchorBatch.insertCachedBlock(synthInfo, syntheticHeight, {} /* no txHashes */);
                /* No raw block stored – these entries exist only for LWMA seeding.
                 * getBlockByIndex() on a synthetic height will throw std::out_of_range
                 * (no raw block in DB), which Core::getBlocks() catches gracefully. */
            }
        }

        anchorBatch.insertCachedBlock(anchorInfo, anchorHeight, {} /* no txHashes */);
        anchorBatch.insertRawBlock(anchorHeight, RawBlock{} /* empty – served by peers */);

        auto res = database.write(anchorBatch);
        if (res)
        {
            logger(Logging::ERROR) << "injectBootstrapAnchor: write failed: " << res.message();
            throw std::runtime_error(res.message());
        }

        /* Persist the sync floor height as DB metadata. */
        struct SyncFloorWriteBatch : public IWriteBatch
        {
            explicit SyncFloorWriteBatch(uint32_t h) : height(h) {}
            std::vector<std::pair<std::string, std::string>> extractRawDataToInsert() override
            {
                return {{SYNC_FLOOR_KEY, std::to_string(height)}};
            }
            std::vector<std::string> extractRawKeysToRemove() override { return {}; }
            uint32_t height;
        };

        SyncFloorWriteBatch floorBatch(anchorHeight);
        auto res2 = database.write(floorBatch);
        if (res2)
        {
            logger(Logging::ERROR) << "injectBootstrapAnchor: floor metadata write failed: " << res2.message();
            throw std::runtime_error(res2.message());
        }

        /* Invalidate in-memory caches so next access picks up the anchor.
         * Also clear unitsCache: addGenesisBlock() populated it with the genesis
         * block before bootstrapFromHeight() was called.  After the anchor injection
         * getTopBlockIndex() jumps to anchorHeight, so cacheStartIndex would be
         * miscalculated as anchorHeight and the genesis entry would be treated as
         * the block at that height – corrupting LWMA input data. */
        topBlockIndex = std::nullopt;
        unitsCache.clear();
        topBlockHash  = std::nullopt;

        logger(Logging::INFO) << "injectBootstrapAnchor: complete, DB top is now " << getTopBlockIndex();
    }

    /* ----------------------------------------------------------------------------- */

    void DatabaseBlockchainCache::addGenesisBlock(CachedBlock &&genesisBlock)
    {
        uint64_t minerReward = 0;
        for (const TransactionOutput &output : genesisBlock.getBlock().baseTransaction.outputs)
        {
            minerReward += output.amount;
        }

        assert(minerReward > 0);

        uint64_t baseTransactionSize = getObjectBinarySize(genesisBlock.getBlock().baseTransaction);
        assert(baseTransactionSize < std::numeric_limits<uint32_t>::max());

        BlockchainWriteBatch batch;

        CachedBlockInfo blockInfo {genesisBlock.getBlockHash(),
                                   genesisBlock.getBlock().timestamp,
                                   1,
                                   minerReward,
                                   1,
                                   uint32_t(baseTransactionSize)};

        auto baseTransaction = genesisBlock.getBlock().baseTransaction;
        auto cachedBaseTransaction = CachedTransaction {std::move(baseTransaction)};

        /* Collect the compact wallet-sync record for genesis too. Every other
           block gets one at push time; skipping genesis left a permanent hole
           at height 0, which is exactly where a wallet syncing from scratch
           starts looking. */
        WalletTypes::RawTransaction coinbaseWalletTx;
        pushTransaction(cachedBaseTransaction, 0, 0, batch, &coinbaseWalletTx);

        {
            WalletTypes::WalletBlockInfo walletBlock;
            walletBlock.blockHeight = 0;
            walletBlock.blockHash = genesisBlock.getBlockHash();
            walletBlock.blockTimestamp = genesisBlock.getBlock().timestamp;

            WalletTypes::RawCoinbaseTransaction coinbaseSyncTx;
            coinbaseSyncTx.hash = coinbaseWalletTx.hash;
            coinbaseSyncTx.transactionPublicKey = coinbaseWalletTx.transactionPublicKey;
            coinbaseSyncTx.keyOutputs = coinbaseWalletTx.keyOutputs;
            coinbaseSyncTx.unlockTime = coinbaseWalletTx.unlockTime;
            walletBlock.coinbaseTransaction = coinbaseSyncTx;

            batch.insertWalletSyncBlock(0, walletBlock);
        }

        batch.insertCachedBlock(blockInfo, 0, {cachedBaseTransaction.getTransactionHash()});
        batch.insertRawBlock(0, {toBinaryArray(genesisBlock.getBlock()), {}});
        batch.insertClosestTimestampBlockIndex(roundToMidnight(genesisBlock.getBlock().timestamp), 0);

        auto res = database.write(batch);
        if (res)
        {
            logger(Logging::ERROR) << "addGenesisBlock failed: failed to write to database, " << res.message();
            throw std::runtime_error(res.message());
        }

        topBlockHash = genesisBlock.getBlockHash();

        unitsCache.push_back(blockInfo);
    }

    uint32_t DatabaseBlockchainCache::getPruneFloor() const
    {
        if (!m_pruneFloor)
        {
            auto batch = BlockchainReadBatch().requestPruneFloor();
            auto res = readDatabase(batch);
            const auto &pf = res.getPruneFloor();
            m_pruneFloor = pf.second ? pf.first : 0;
        }

        /* A lite node holds no raw block below its lite height either, and the
           floor is exactly the question every caller is asking: the lowest
           height a block body can be read from. Reporting it here means the
           peer-serving, wallet-sync and getrawblocks paths that already handle
           a pruned floor handle the lite region too, rather than each having to
           learn about lite mode separately. The two are mutually exclusive at
           the command line, so in practice only one of them is ever non-zero. */
        return std::max(*m_pruneFloor, liteHeight);
    }

    void DatabaseBlockchainCache::pruneRawBlocksBefore(uint32_t height)
    {
        /* Never prune genesis block (index 0); clamp to 1. */
        if (height <= 1)
        {
            return;
        }

        /* --lite and --prune cannot be combined, so this is only reachable if
           something asks a lite node to prune anyway. There is nothing below
           the lite height to delete, and writing a prune floor there would
           record a deletion that never happened. */
        if (liteHeight != 0)
        {
            logger(Logging::DEBUGGING)
                << "pruneRawBlocksBefore: ignoring on a lite node; nothing is stored below height " << liteHeight;
            return;
        }

        const uint32_t currentFloor = getPruneFloor();
        if (height <= currentFloor)
        {
            logger(Logging::DEBUGGING) << "pruneRawBlocksBefore: height " << height
                                       << " already below prune floor " << currentFloor << ", nothing to do.";
            return;
        }

        constexpr uint32_t BATCH_SIZE = 5000;
        uint32_t from = std::max(currentFloor, uint32_t(1)); // never delete genesis
        uint32_t deleted = 0;

        logger(Logging::INFO) << "pruneRawBlocksBefore: deleting raw blocks [" << from << ", " << height << ").";

        while (from < height)
        {
            const uint32_t to = std::min(from + BATCH_SIZE, height);

            BlockchainWriteBatch batch;
            for (uint32_t i = from; i < to; ++i)
            {
                batch.removeRawBlock(i);
            }
            batch.setPruneFloor(to);

            auto err = database.write(batch);
            if (err)
            {
                logger(Logging::ERROR) << "pruneRawBlocksBefore: write failed at [" << from << ", " << to
                                       << "): " << err.message();
                throw std::runtime_error("pruneRawBlocksBefore write failed: " + err.message());
            }

            deleted += (to - from);
            from = to;
            m_pruneFloor = to;
        }

        logger(Logging::INFO) << "pruneRawBlocksBefore: deleted " << deleted
                              << " raw block(s), new prune floor = " << *m_pruneFloor << ".";
    }

} // namespace CryptoNote
