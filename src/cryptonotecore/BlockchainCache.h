// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "BlockchainStorage.h"
#include "Currency.h"
#include "IBlockchainCache.h"
#include "common/StringView.h"
#include "cryptonotecore/UpgradeManager.h"

#include <set>
#include <cstring>
#include <iterator>
#include <map>
#include <unordered_map>
#include <vector>

namespace CryptoNote
{
    class ISerializer;

    struct SpentKeyImage
    {
        uint32_t blockIndex;

        Crypto::KeyImage keyImage;

        void serialize(ISerializer &s);
    };

    struct CachedTransactionInfo
    {
        uint32_t blockIndex;

        uint32_t transactionIndex;

        Crypto::Hash transactionHash;

        uint64_t unlockTime;

        std::vector<TransactionOutputTarget> outputs;

        // needed for getTransactionGlobalIndexes query
        std::vector<uint32_t> globalIndexes;

        void serialize(ISerializer &s);

    };


    struct OutputGlobalIndexesForAmount
    {
        uint32_t startIndex = 0;

        // 1. This container must be sorted by PackedOutIndex::blockIndex and PackedOutIndex::transactionIndex
        // 2. GlobalOutputIndex for particular output is calculated as following: startIndex + index in vector
        std::vector<PackedOutIndex> outputs;

        void serialize(ISerializer &s);
    };

    struct PaymentIdTransactionHashPair
    {
        Crypto::Hash paymentId;

        Crypto::Hash transactionHash;

        void serialize(ISerializer &s);
    };

    bool serialize(PackedOutIndex &value, Common::StringView name, CryptoNote::ISerializer &serializer);

    class DatabaseBlockchainCache;

    class BlockchainCache : public IBlockchainCache
    {
      public:
        BlockchainCache(
            const std::string &filename,
            const Currency &currency,
            std::shared_ptr<Logging::ILogger> logger,
            IBlockchainCache *parent,
            uint32_t startIndex = 0);

        // Returns upper part of segment. [this] remains lower part.
        // All of indexes on blockIndex == splitBlockIndex belong to upper part
        std::unique_ptr<IBlockchainCache> split(uint32_t splitBlockIndex) override;

        void rewind(const uint64_t height) override;

        /* Child segment caches never hold prunable raw blocks; these are no-ops. */
        void pruneRawBlocksBefore(uint32_t height) override {}
        uint32_t getPruneFloor() const override { return 0; }

        virtual void pushBlock(
            const CachedBlock &cachedBlock,
            const std::vector<CachedTransaction> &cachedTransactions,
            const TransactionValidatorState &validatorState,
            size_t blockSize,
            uint64_t generatedCoins,
            uint64_t blockDifficulty,
            RawBlock &&rawBlock) override;

        virtual PushedBlockInfo getPushedBlockInfo(uint32_t index) const override;

        bool checkIfSpent(const Crypto::KeyImage &keyImage, uint32_t blockIndex) const override;

        bool checkIfSpent(const Crypto::KeyImage &keyImage) const override;

        bool isTransactionSpendTimeUnlocked(uint64_t unlockTime) const override;

        bool isTransactionSpendTimeUnlocked(uint64_t unlockTime, uint32_t blockIndex) const override;

        ExtractOutputKeysResult extractKeyOutputKeys(
            uint64_t amount,
            Common::ArrayView<uint32_t> globalIndexes,
            std::vector<Crypto::PublicKey> &publicKeys) const override;

        ExtractOutputKeysResult extractKeyOutputKeys(
            uint64_t amount,
            uint32_t blockIndex,
            Common::ArrayView<uint32_t> globalIndexes,
            std::vector<Crypto::PublicKey> &publicKeys) const override;

        ExtractOutputKeysResult extractKeyOtputIndexes(
            uint64_t amount,
            Common::ArrayView<uint32_t> globalIndexes,
            std::vector<PackedOutIndex> &outIndexes) const override;

        ExtractOutputKeysResult extractKeyOtputReferences(
            uint64_t amount,
            Common::ArrayView<uint32_t> globalIndexes,
            std::vector<std::pair<Crypto::Hash, size_t>> &outputReferences) const override;

        uint32_t getTopBlockIndex() const override;

        const Crypto::Hash &getTopBlockHash() const override;

        uint32_t getBlockCount() const override;

        bool hasBlock(const Crypto::Hash &blockHash) const override;

        uint32_t getBlockIndex(const Crypto::Hash &blockHash) const override;

        bool hasTransaction(const Crypto::Hash &transactionHash) const override;

        std::vector<uint64_t> getLastTimestamps(size_t count) const override;

        std::vector<uint64_t> getLastTimestamps(size_t count, uint32_t blockIndex, UseGenesis) const override;

        std::vector<uint64_t> getLastBlocksSizes(size_t count) const override;

        std::vector<uint64_t> getLastBlocksSizes(size_t count, uint32_t blockIndex, UseGenesis) const override;

        std::vector<uint64_t>
            getLastCumulativeDifficulties(size_t count, uint32_t blockIndex, UseGenesis) const override;

        std::vector<uint64_t> getLastCumulativeDifficulties(size_t count) const override;

        uint64_t getDifficultyForNextBlock() const override;

        uint64_t getDifficultyForNextBlock(uint32_t blockIndex) const override;

        virtual uint64_t getCurrentCumulativeDifficulty() const override;

        virtual uint64_t getCurrentCumulativeDifficulty(uint32_t blockIndex) const override;

        uint64_t getAlreadyGeneratedCoins() const override;

        uint64_t getAlreadyGeneratedCoins(uint32_t blockIndex) const override;

        uint64_t getAlreadyGeneratedTransactions(uint32_t blockIndex) const override;

        std::vector<uint64_t> getLastUnits(
            size_t count,
            uint32_t blockIndex,
            UseGenesis use,
            std::function<uint64_t(const CachedBlockInfo &)> pred) const override;

        Crypto::Hash getBlockHash(uint32_t blockIndex) const override;

        virtual std::vector<Crypto::Hash> getBlockHashes(uint32_t startIndex, size_t maxCount) const override;

        virtual IBlockchainCache *getParent() const override;

        virtual void setParent(IBlockchainCache *p) override;

        virtual uint32_t getStartBlockIndex() const override;

        virtual size_t getKeyOutputsCountForAmount(uint64_t amount, uint32_t blockIndex) const override;

        std::tuple<bool, uint64_t> getBlockHeightForTimestamp(uint64_t timestamp) const override;

        virtual uint32_t getTimestampLowerBoundBlockIndex(uint64_t timestamp) const override;

        virtual bool getTransactionGlobalIndexes(
            const Crypto::Hash &transactionHash,
            std::vector<uint32_t> &globalIndexes) const override;

        virtual size_t getTransactionCount() const override;

        virtual uint32_t getBlockIndexContainingTx(const Crypto::Hash &transactionHash) const override;

        virtual size_t getChildCount() const override;

        virtual void addChild(IBlockchainCache *child) override;

        virtual bool deleteChild(IBlockchainCache *) override;

        virtual void save() override;

        virtual void load() override;

        virtual std::vector<BinaryArray> getRawTransactions(
            const std::vector<Crypto::Hash> &transactions,
            std::vector<Crypto::Hash> &missedTransactions) const override;

        virtual std::vector<BinaryArray>
            getRawTransactions(const std::vector<Crypto::Hash> &transactions) const override;

        void getRawTransactions(
            const std::vector<Crypto::Hash> &transactions,
            std::vector<BinaryArray> &foundTransactions,
            std::vector<Crypto::Hash> &missedTransactions) const override;

        virtual std::unordered_map<Crypto::Hash, std::vector<uint64_t>>
            getGlobalIndexes(const std::vector<Crypto::Hash> transactionHashes) const override;

        virtual RawBlock getBlockByIndex(uint32_t index) const override;

        virtual BinaryArray getRawTransaction(uint32_t blockIndex, uint32_t transactionIndex) const override;

        virtual std::vector<Crypto::Hash> getTransactionHashes() const override;

        virtual std::vector<uint32_t>
            getRandomOutsByAmount(uint64_t amount, size_t count, uint32_t blockIndex) const override;

        virtual ExtractOutputKeysResult extractKeyOutputs(
            uint64_t amount,
            uint32_t blockIndex,
            Common::ArrayView<uint32_t> globalIndexes,
            std::function<
                ExtractOutputKeysResult(const CachedTransactionInfo &info, PackedOutIndex index, uint32_t globalIndex)>
                pred) const override;

        virtual std::vector<Crypto::Hash> getTransactionHashesByPaymentId(const Crypto::Hash &paymentId) const override;

        virtual std::vector<Crypto::Hash>
            getBlockHashesByTimestamps(uint64_t timestampBegin, size_t secondsCount) const override;

        virtual std::vector<RawBlock>
            getBlocksByHeight(const uint64_t startHeight, const uint64_t endHeight) const override;

        virtual std::vector<RawBlock>
            getNonEmptyBlocks(const uint64_t startHeight, const size_t blockCount) const override;

      private:
        struct BlockIndexTag
        {
        };
        struct BlockHashTag
        {
        };
        struct TransactionHashTag
        {
        };
        struct KeyImageTag
        {
        };
        struct TransactionInBlockTag
        {
        };
        struct PackedOutputTag
        {
        };
        struct TimestampTag
        {
        };
        struct PaymentIdTag
        {
        };

        /* Spent key images, previously a multi_index_container ordered by
           block index and unique on key image.

           The set is ordered by block index first and key image second, which
           gives the ordered view directly, and the map provides the unique key
           image lookup. Both are node based, so the iterators the map holds
           stay valid as the set grows. */
        class SpentKeyImagesContainer
        {
          public:
            struct Order
            {
                using is_transparent = void;

                bool operator()(const SpentKeyImage &a, const SpentKeyImage &b) const
                {
                    if (a.blockIndex != b.blockIndex)
                    {
                        return a.blockIndex < b.blockIndex;
                    }

                    return std::memcmp(&a.keyImage, &b.keyImage, sizeof(a.keyImage)) < 0;
                }

                /* Lets the set be searched by block index alone. */
                bool operator()(const SpentKeyImage &a, uint32_t blockIndex) const
                {
                    return a.blockIndex < blockIndex;
                }

                bool operator()(uint32_t blockIndex, const SpentKeyImage &b) const
                {
                    return blockIndex < b.blockIndex;
                }
            };

            using Storage = std::set<SpentKeyImage, Order>;

            using const_iterator = Storage::const_iterator;

            const_iterator begin() const
            {
                return m_ordered.begin();
            }

            const_iterator end() const
            {
                return m_ordered.end();
            }

            size_t size() const
            {
                return m_ordered.size();
            }

            void insert(const SpentKeyImage &image)
            {
                const auto [position, inserted] = m_ordered.insert(image);

                if (inserted)
                {
                    m_byKeyImage.emplace(image.keyImage, position);
                }
            }

            /* Null when the key image has not been spent in this segment. */
            const SpentKeyImage *findByKeyImage(const Crypto::KeyImage &keyImage) const
            {
                const auto it = m_byKeyImage.find(keyImage);
                return it == m_byKeyImage.end() ? nullptr : &*it->second;
            }

            std::pair<const_iterator, const_iterator> equalRangeByBlockIndex(uint32_t blockIndex) const
            {
                return {m_ordered.lower_bound(blockIndex), m_ordered.upper_bound(blockIndex)};
            }

            /* Moves every entry at or above splitBlockIndex into other. */
            void splitTo(SpentKeyImagesContainer &other, uint32_t splitBlockIndex)
            {
                for (auto it = m_ordered.lower_bound(splitBlockIndex); it != m_ordered.end();)
                {
                    other.insert(*it);
                    m_byKeyImage.erase(it->keyImage);
                    it = m_ordered.erase(it);
                }
            }

          private:
            Storage m_ordered;

            std::unordered_map<Crypto::KeyImage, const_iterator> m_byKeyImage;
        };

        /* Cached transactions, previously a multi_index_container with three
           indexes: unique on (blockIndex, transactionIndex), ordered by block
           index, and unique on transaction hash.

           The map owns the entries and covers the hash index. The ordered map
           keyed by the (blockIndex, transactionIndex) pair covers the other
           two at once: an exact lookup is a find, and the by-block-index view
           is the same ordering, since the pair compares on block index first. */
        class TransactionsCacheContainer
        {
          public:
            using Storage = std::unordered_map<Crypto::Hash, CachedTransactionInfo>;

            using Position = std::pair<uint32_t, uint32_t>;

            class const_iterator
            {
              public:
                using iterator_category = std::forward_iterator_tag;
                using value_type = CachedTransactionInfo;
                using difference_type = std::ptrdiff_t;
                using pointer = const CachedTransactionInfo *;
                using reference = const CachedTransactionInfo &;

                explicit const_iterator(Storage::const_iterator it): m_it(it) {}

                reference operator*() const
                {
                    return m_it->second;
                }

                pointer operator->() const
                {
                    return &m_it->second;
                }

                const_iterator &operator++()
                {
                    ++m_it;
                    return *this;
                }

                bool operator==(const const_iterator &other) const
                {
                    return m_it == other.m_it;
                }

                bool operator!=(const const_iterator &other) const
                {
                    return !(*this == other);
                }

              private:
                Storage::const_iterator m_it;
            };

            const_iterator begin() const
            {
                return const_iterator(m_byHash.begin());
            }

            const_iterator end() const
            {
                return const_iterator(m_byHash.end());
            }

            size_t size() const
            {
                return m_byHash.size();
            }

            void insert(CachedTransactionInfo &&info)
            {
                const Position position {info.blockIndex, info.transactionIndex};
                const Crypto::Hash hash = info.transactionHash;

                const auto [entry, inserted] = m_byHash.emplace(hash, std::move(info));

                if (inserted)
                {
                    m_byPosition.emplace(position, &entry->second);
                }
            }

            size_t countByHash(const Crypto::Hash &transactionHash) const
            {
                return m_byHash.count(transactionHash);
            }

            const CachedTransactionInfo *findByHash(const Crypto::Hash &transactionHash) const
            {
                const auto it = m_byHash.find(transactionHash);
                return it == m_byHash.end() ? nullptr : &it->second;
            }

            const CachedTransactionInfo *findInBlock(uint32_t blockIndex, uint32_t transactionIndex) const
            {
                const auto it = m_byPosition.find(Position {blockIndex, transactionIndex});
                return it == m_byPosition.end() ? nullptr : it->second;
            }

            /* Transaction hashes at or above a block index, in block order.
               The split needs these before moving anything, because removing a
               payment id reads the entry it is about to move. */
            std::vector<Crypto::Hash> hashesFromBlock(uint32_t blockIndex) const
            {
                std::vector<Crypto::Hash> hashes;

                for (auto it = m_byPosition.lower_bound(Position {blockIndex, 0}); it != m_byPosition.end(); ++it)
                {
                    hashes.push_back(it->second->transactionHash);
                }

                return hashes;
            }

            void eraseByHash(const Crypto::Hash &transactionHash)
            {
                const auto it = m_byHash.find(transactionHash);

                if (it == m_byHash.end())
                {
                    return;
                }

                m_byPosition.erase(Position {it->second.blockIndex, it->second.transactionIndex});
                m_byHash.erase(it);
            }

          private:
            Storage m_byHash;

            std::map<Position, const CachedTransactionInfo *> m_byPosition;
        };

        /* Block info, previously a multi_index_container with a random access
           index, a unique index on block hash and one ordered by timestamp.

           Blocks only ever arrive at the end and leave from the end, so a
           vector gives the positional view directly and the positions of the
           blocks that stay are never disturbed. The other two views map back
           to a position. Position i holds blockIndex - startIndex. */
        class BlockInfoContainer
        {
          public:
            using Storage = std::vector<CachedBlockInfo>;

            using const_iterator = Storage::const_iterator;

            const_iterator begin() const
            {
                return m_blocks.begin();
            }

            const_iterator end() const
            {
                return m_blocks.end();
            }

            size_t size() const
            {
                return m_blocks.size();
            }

            bool empty() const
            {
                return m_blocks.empty();
            }

            const CachedBlockInfo &front() const
            {
                return m_blocks.front();
            }

            const CachedBlockInfo &back() const
            {
                return m_blocks.back();
            }

            const CachedBlockInfo &operator[](size_t position) const
            {
                return m_blocks[position];
            }

            const CachedBlockInfo &at(size_t position) const
            {
                return m_blocks.at(position);
            }

            void push_back(CachedBlockInfo &&info)
            {
                const Crypto::Hash hash = info.blockHash;
                const uint64_t timestamp = info.timestamp;
                const size_t position = m_blocks.size();

                m_blocks.push_back(std::move(info));
                m_byHash.emplace(hash, position);
                m_byTimestamp.emplace(timestamp, position);
            }

            size_t countByHash(const Crypto::Hash &blockHash) const
            {
                return m_byHash.count(blockHash);
            }

            /* Position of a block, or size() when it is not here. */
            size_t positionOfHash(const Crypto::Hash &blockHash) const
            {
                const auto it = m_byHash.find(blockHash);
                return it == m_byHash.end() ? m_blocks.size() : it->second;
            }

            std::vector<Crypto::Hash> hashesInTimestampRange(uint64_t first, uint64_t last) const
            {
                std::vector<Crypto::Hash> hashes;

                const auto begin = m_byTimestamp.lower_bound(first);
                const auto end = m_byTimestamp.upper_bound(last);

                for (auto it = begin; it != end; ++it)
                {
                    hashes.push_back(m_blocks[it->second].blockHash);
                }

                return hashes;
            }

            /* Moves everything from position onwards into other, keeping order. */
            void splitTo(BlockInfoContainer &other, size_t position)
            {
                for (size_t i = position; i < m_blocks.size(); i++)
                {
                    other.push_back(CachedBlockInfo(m_blocks[i]));
                }

                for (size_t i = position; i < m_blocks.size(); i++)
                {
                    m_byHash.erase(m_blocks[i].blockHash);

                    const auto range = m_byTimestamp.equal_range(m_blocks[i].timestamp);

                    for (auto it = range.first; it != range.second; ++it)
                    {
                        if (it->second == i)
                        {
                            m_byTimestamp.erase(it);
                            break;
                        }
                    }
                }

                m_blocks.erase(m_blocks.begin() + static_cast<std::ptrdiff_t>(position), m_blocks.end());
            }

          private:
            Storage m_blocks;

            std::unordered_map<Crypto::Hash, size_t> m_byHash;

            std::multimap<uint64_t, size_t> m_byTimestamp;
        };

        /* Payment id to transaction hash, previously a multi_index_container
           grouped by payment id and unique on transaction hash. The map owns
           the pairs and is node based, so the pointers in the grouping view
           survive rehashing. */
        class PaymentIdContainer
        {
          public:
            using Storage = std::unordered_map<Crypto::Hash, PaymentIdTransactionHashPair>;

            /* Iterates the pairs themselves rather than map entries, so
               serialisation reads the same sequence as before. */
            class const_iterator
            {
              public:
                using iterator_category = std::forward_iterator_tag;
                using value_type = PaymentIdTransactionHashPair;
                using difference_type = std::ptrdiff_t;
                using pointer = const PaymentIdTransactionHashPair *;
                using reference = const PaymentIdTransactionHashPair &;

                explicit const_iterator(Storage::const_iterator it): m_it(it) {}

                reference operator*() const
                {
                    return m_it->second;
                }

                pointer operator->() const
                {
                    return &m_it->second;
                }

                const_iterator &operator++()
                {
                    ++m_it;
                    return *this;
                }

                bool operator==(const const_iterator &other) const
                {
                    return m_it == other.m_it;
                }

                bool operator!=(const const_iterator &other) const
                {
                    return !(*this == other);
                }

              private:
                Storage::const_iterator m_it;
            };

            const_iterator begin() const
            {
                return const_iterator(m_byTransactionHash.begin());
            }

            const_iterator end() const
            {
                return const_iterator(m_byTransactionHash.end());
            }

            void insert(const PaymentIdTransactionHashPair &pair)
            {
                const auto [position, inserted] = m_byTransactionHash.emplace(pair.transactionHash, pair);

                if (inserted)
                {
                    m_byPaymentId.emplace(pair.paymentId, &position->second);
                }
            }

            const PaymentIdTransactionHashPair *findByTransactionHash(const Crypto::Hash &transactionHash) const
            {
                const auto it = m_byTransactionHash.find(transactionHash);
                return it == m_byTransactionHash.end() ? nullptr : &it->second;
            }

            void eraseByTransactionHash(const Crypto::Hash &transactionHash)
            {
                const auto it = m_byTransactionHash.find(transactionHash);

                if (it == m_byTransactionHash.end())
                {
                    return;
                }

                const auto range = m_byPaymentId.equal_range(it->second.paymentId);

                for (auto entry = range.first; entry != range.second; ++entry)
                {
                    if (entry->second == &it->second)
                    {
                        m_byPaymentId.erase(entry);
                        break;
                    }
                }

                m_byTransactionHash.erase(it);
            }

            std::vector<Crypto::Hash> transactionHashesFor(const Crypto::Hash &paymentId) const
            {
                std::vector<Crypto::Hash> hashes;

                const auto range = m_byPaymentId.equal_range(paymentId);

                for (auto it = range.first; it != range.second; ++it)
                {
                    hashes.push_back(it->second->transactionHash);
                }

                return hashes;
            }

          private:
            Storage m_byTransactionHash;

            std::unordered_multimap<Crypto::Hash, const PaymentIdTransactionHashPair *> m_byPaymentId;
        };

        typedef std::map<uint64_t, OutputGlobalIndexesForAmount> OutputsGlobalIndexesContainer;

        typedef std::map<BlockIndex, std::vector<std::pair<Amount, GlobalOutputIndex>>> OutputSpentInBlock;

        typedef std::set<std::pair<Amount, GlobalOutputIndex>> SpentOutputsOnAmount;

        const uint32_t CURRENT_SERIALIZATION_VERSION = 1;

        std::string filename;

        const Currency &currency;

        Logging::LoggerRef logger;

        IBlockchainCache *parent;

        // index of first block stored in this cache
        uint32_t startIndex;

        TransactionsCacheContainer transactions;

        SpentKeyImagesContainer spentKeyImages;

        BlockInfoContainer blockInfos;

        OutputsGlobalIndexesContainer keyOutputsGlobalIndexes;

        PaymentIdContainer paymentIds;

        std::unique_ptr<BlockchainStorage> storage;

        std::vector<IBlockchainCache *> children;

        void serialize(ISerializer &s);

        void addSpentKeyImage(const Crypto::KeyImage &keyImage, uint32_t blockIndex);

        void pushTransaction(const CachedTransaction &tx, uint32_t blockIndex, uint16_t transactionBlockIndex);

        void splitSpentKeyImages(BlockchainCache &newCache, uint32_t splitBlockIndex);

        void splitTransactions(BlockchainCache &newCache, uint32_t splitBlockIndex);

        void splitBlocks(BlockchainCache &newCache, uint32_t splitBlockIndex);

        void splitKeyOutputsGlobalIndexes(BlockchainCache &newCache, uint32_t splitBlockIndex);

        void removePaymentId(const Crypto::Hash &transactionHash, BlockchainCache &newCache);

        uint32_t insertKeyOutputToGlobalIndex(uint64_t amount, PackedOutIndex output, uint32_t blockIndex);

        enum class OutputSearchResult : uint8_t
        {
            FOUND,
            NOT_FOUND,
            INVALID_ARGUMENT
        };

        TransactionValidatorState fillOutputsSpentByBlock(uint32_t blockIndex) const;

        uint8_t getBlockMajorVersionForHeight(uint32_t height) const;

        void fixChildrenParent(IBlockchainCache *p);

        void doPushBlock(
            const CachedBlock &cachedBlock,
            const std::vector<CachedTransaction> &cachedTransactions,
            const TransactionValidatorState &validatorState,
            size_t blockSize,
            uint64_t generatedCoins,
            uint64_t blockDifficulty,
            RawBlock &&rawBlock);
    };

} // namespace CryptoNote
