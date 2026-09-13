// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "ITransaction.h"
#include "ITransfersContainer.h"
#include "crypto/crypto.h"
#include "cryptonotecore/CryptoNoteBasic.h"
#include "cryptonotecore/Currency.h"
#include "logging/LoggerRef.h"
#include "serialization/CryptoNoteSerialization.h"
#include "serialization/ISerializer.h"
#include "serialization/SerializationOverloads.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace CryptoNote
{
    struct TransactionOutputInformationIn;

    class SpentOutputDescriptor
    {
      public:
        SpentOutputDescriptor();

        SpentOutputDescriptor(const TransactionOutputInformationIn &transactionInfo);

        SpentOutputDescriptor(const Crypto::KeyImage *keyImage);

        void assign(const Crypto::KeyImage *keyImage);

        bool operator==(const SpentOutputDescriptor &other) const;

        size_t hash() const;

      private:
        TransactionTypes::OutputType m_type;

        union {
            const Crypto::KeyImage *m_keyImage;
            struct
            {
                uint64_t m_amount;
                uint32_t m_globalOutputIndex;
            };
        };
    };

    struct SpentOutputDescriptorHasher
    {
        size_t operator()(const SpentOutputDescriptor &descriptor) const
        {
            return descriptor.hash();
        }
    };

    struct TransactionOutputInformationIn : public TransactionOutputInformation
    {
        Crypto::KeyImage keyImage; //!< \attention Used only for TransactionTypes::OutputType::Key
    };

    struct TransactionOutputInformationEx : public TransactionOutputInformationIn
    {
        uint64_t unlockTime;

        uint32_t blockHeight;

        uint32_t transactionIndex;

        bool visible;

        SpentOutputDescriptor getSpentOutputDescriptor() const
        {
            return SpentOutputDescriptor(*this);
        }

        const Crypto::Hash &getTransactionHash() const
        {
            return transactionHash;
        }

        void serialize(CryptoNote::ISerializer &s)
        {
            s(reinterpret_cast<uint8_t &>(type), "type");
            s(amount, "");
            serializeGlobalOutputIndex(s, globalOutputIndex, "");
            s(outputInTransaction, "");
            s(transactionPublicKey, "");
            s(keyImage, "");
            s(unlockTime, "");
            serializeBlockHeight(s, blockHeight, "");
            s(transactionIndex, "");
            s(transactionHash, "");
            s(visible, "");

            if (type == TransactionTypes::OutputType::Key)
            {
                s(outputKey, "");
            }
        }
    };

    struct TransactionBlockInfo
    {
        uint32_t height;

        uint64_t timestamp;

        uint32_t transactionIndex;

        void serialize(ISerializer &s)
        {
            serializeBlockHeight(s, height, "height");
            s(timestamp, "timestamp");
            s(transactionIndex, "transactionIndex");
        }
    };

    struct SpentTransactionOutput : TransactionOutputInformationEx
    {
        TransactionBlockInfo spendingBlock;

        Crypto::Hash spendingTransactionHash;

        uint32_t inputInTransaction;

        const Crypto::Hash &getSpendingTransactionHash() const
        {
            return spendingTransactionHash;
        }

        void serialize(ISerializer &s)
        {
            TransactionOutputInformationEx::serialize(s);
            s(spendingBlock, "spendingBlock");
            s(spendingTransactionHash, "spendingTransactionHash");
            s(inputInTransaction, "inputInTransaction");
        }
    };

    enum class KeyImageState
    {
        Unconfirmed,
        Confirmed,
        Spent
    };

    struct KeyOutputInfo
    {
        KeyImageState state;
        size_t count;
    };

    namespace TransfersDetail
    {
        /* Index tags. Each one names a view of one of the containers below. */
        struct SpentOutputDescriptorIndex
        {
        };
        struct ContainingTransactionIndex
        {
        };
        struct SpendingTransactionIndex
        {
        };
        struct TransactionBlockHeightIndex
        {
        };

        /* The fields the spent output descriptor is built from. A descriptor for a
           key output holds a pointer to the key image of the record it describes,
           so it cannot be compared against a record that has since been
           overwritten. These are copied by value instead, which lets a replaced
           record be checked for a changed key. */
        struct DescriptorFields
        {
            TransactionTypes::OutputType type;

            Crypto::KeyImage keyImage;

            uint64_t amount;

            uint32_t globalOutputIndex;

            template<typename T>
            static DescriptorFields of(const T &record)
            {
                return DescriptorFields {record.type, record.keyImage, record.amount, record.globalOutputIndex};
            }

            bool operator==(const DescriptorFields &other) const
            {
                if (type != other.type)
                {
                    return false;
                }

                if (type == TransactionTypes::OutputType::Key)
                {
                    return keyImage == other.keyImage;
                }

                return amount == other.amount && globalOutputIndex == other.globalOutputIndex;
            }
        };

        /* Iterator over one of the secondary views. The mapped value is an
           iterator into the list of records. */
        template<typename T, typename MapIterator> class ViewIterator
        {
          public:
            using iterator_category = std::forward_iterator_tag;

            using value_type = T;

            using difference_type = std::ptrdiff_t;

            using pointer = T *;

            using reference = T &;

            ViewIterator() = default;

            explicit ViewIterator(MapIterator it): m_it(it) {}

            reference operator*() const
            {
                return *m_it->second;
            }

            pointer operator->() const
            {
                return &*m_it->second;
            }

            ViewIterator &operator++()
            {
                ++m_it;
                return *this;
            }

            ViewIterator operator++(int)
            {
                ViewIterator tmp(*this);
                ++m_it;
                return tmp;
            }

            const MapIterator &base() const
            {
                return m_it;
            }

            friend bool operator==(const ViewIterator &a, const ViewIterator &b)
            {
                return a.m_it == b.m_it;
            }

            friend bool operator!=(const ViewIterator &a, const ViewIterator &b)
            {
                return a.m_it != b.m_it;
            }

          private:
            MapIterator m_it {};
        };

        /* Holds transfer records with a view keyed on the spent output descriptor,
           a view keyed on the hash of the transaction that contains the output
           and, for spent outputs, a view keyed on the hash of the transaction that
           spends it.

           Records live in a list so that their addresses never move: a descriptor
           for a key output points at the key image inside the record it describes,
           and the secondary views hold iterators into that list. */
        template<typename T, bool DescriptorUnique, bool HasSpendingIndex> class TransferMultiView
        {
          private:
            using Records = std::list<T>;

          public:
            using value_type = T;

            using iterator = typename Records::iterator;

            using const_iterator = iterator;

          private:
            using DescriptorMap =
                std::unordered_multimap<SpentOutputDescriptor, iterator, SpentOutputDescriptorHasher>;

            using HashMap = std::unordered_multimap<Crypto::Hash, iterator>;

          public:
            using descriptor_iterator = ViewIterator<T, typename DescriptorMap::iterator>;

            using hash_iterator = ViewIterator<T, typename HashMap::iterator>;

            TransferMultiView() = default;

            /* The views hold a pointer back to the container they belong to, so a
               move has to move the data and leave every view pointing here. */
            TransferMultiView(TransferMultiView &&other) noexcept:
                m_records(std::move(other.m_records)),
                m_byDescriptor(std::move(other.m_byDescriptor)),
                m_byTransaction(std::move(other.m_byTransaction)),
                m_bySpending(std::move(other.m_bySpending))
            {
                other.clear();
            }

            TransferMultiView &operator=(TransferMultiView &&other) noexcept
            {
                if (this != &other)
                {
                    m_records = std::move(other.m_records);
                    m_byDescriptor = std::move(other.m_byDescriptor);
                    m_byTransaction = std::move(other.m_byTransaction);
                    m_bySpending = std::move(other.m_bySpending);
                    other.clear();
                }

                return *this;
            }

            TransferMultiView(const TransferMultiView &) = delete;

            TransferMultiView &operator=(const TransferMultiView &) = delete;

            /* View keyed on the spent output descriptor. */
            class DescriptorView
            {
              public:
                using value_type = T;

                using iterator = descriptor_iterator;

                using const_iterator = descriptor_iterator;

                explicit DescriptorView(TransferMultiView *owner): m_owner(owner) {}

                descriptor_iterator begin() const
                {
                    return descriptor_iterator(m_owner->m_byDescriptor.begin());
                }

                descriptor_iterator end() const
                {
                    return descriptor_iterator(m_owner->m_byDescriptor.end());
                }

                std::pair<descriptor_iterator, descriptor_iterator>
                    equal_range(const SpentOutputDescriptor &descriptor) const
                {
                    auto range = m_owner->m_byDescriptor.equal_range(descriptor);
                    return {descriptor_iterator(range.first), descriptor_iterator(range.second)};
                }

                size_t count(const SpentOutputDescriptor &descriptor) const
                {
                    return m_owner->m_byDescriptor.count(descriptor);
                }

                size_t size() const
                {
                    return m_owner->size();
                }

                descriptor_iterator erase(descriptor_iterator it) const
                {
                    auto mapIt = it.base();
                    auto next = std::next(mapIt);

                    m_owner->eraseNode(mapIt->second, &mapIt);

                    return descriptor_iterator(next);
                }

                bool replace(descriptor_iterator it, const T &value) const
                {
                    return m_owner->replaceNode(it.base()->second, value);
                }

              private:
                TransferMultiView *m_owner;
            };

            /* View keyed on the hash of a containing or spending transaction. */
            template<bool Spending> class TransactionHashView
            {
              public:
                using value_type = T;

                using iterator = hash_iterator;

                using const_iterator = hash_iterator;

                explicit TransactionHashView(TransferMultiView *owner): m_owner(owner) {}

                hash_iterator begin() const
                {
                    return hash_iterator(map().begin());
                }

                hash_iterator end() const
                {
                    return hash_iterator(map().end());
                }

                std::pair<hash_iterator, hash_iterator> equal_range(const Crypto::Hash &hash) const
                {
                    auto range = map().equal_range(hash);
                    return {hash_iterator(range.first), hash_iterator(range.second)};
                }

                size_t count(const Crypto::Hash &hash) const
                {
                    return map().count(hash);
                }

                size_t size() const
                {
                    return m_owner->size();
                }

                hash_iterator erase(hash_iterator it) const
                {
                    auto mapIt = it.base();
                    auto next = std::next(mapIt);

                    if constexpr (Spending)
                    {
                        m_owner->eraseNode(mapIt->second, nullptr, &mapIt);
                    }
                    else
                    {
                        m_owner->eraseNode(mapIt->second, nullptr, nullptr, &mapIt);
                    }

                    return hash_iterator(next);
                }

                bool replace(hash_iterator it, const T &value) const
                {
                    return m_owner->replaceNode(it.base()->second, value);
                }

              private:
                typename TransferMultiView::HashMap &map() const
                {
                    if constexpr (Spending)
                    {
                        return m_owner->m_bySpending;
                    }
                    else
                    {
                        return m_owner->m_byTransaction;
                    }
                }

                TransferMultiView *m_owner;
            };

            using ContainingView = TransactionHashView<false>;

            using SpendingView = TransactionHashView<true>;

            template<typename Tag> auto &get()
            {
                if constexpr (std::is_same_v<Tag, SpentOutputDescriptorIndex>)
                {
                    return m_descriptorView;
                }
                else if constexpr (std::is_same_v<Tag, ContainingTransactionIndex>)
                {
                    return m_containingView;
                }
                else
                {
                    static_assert(std::is_same_v<Tag, SpendingTransactionIndex>, "unknown transfer index tag");
                    static_assert(HasSpendingIndex, "this container has no spending transaction view");
                    return m_spendingView;
                }
            }

            template<typename Tag> const auto &get() const
            {
                return constCast()->template get<Tag>();
            }

            iterator begin() const
            {
                return constCast()->m_records.begin();
            }

            iterator end() const
            {
                return constCast()->m_records.end();
            }

            size_t size() const
            {
                return m_records.size();
            }

            bool empty() const
            {
                return m_records.empty();
            }

            std::pair<iterator, bool> insert(T &&record)
            {
                return insertRecord(std::move(record));
            }

            std::pair<iterator, bool> insert(const T &record)
            {
                T copy = record;
                return insertRecord(std::move(copy));
            }

            /* The position hint is accepted and ignored, matching the hashed index
               this replaces. It is here so that std::inserter keeps working. */
            iterator insert(iterator, const T &record)
            {
                T copy = record;
                return insertRecord(std::move(copy)).first;
            }

            iterator erase(iterator it)
            {
                auto next = std::next(it);
                eraseNode(it);
                return next;
            }

            bool replace(iterator it, const T &value)
            {
                return replaceNode(it, value);
            }

            void clear()
            {
                m_byDescriptor.clear();
                m_byTransaction.clear();
                m_bySpending.clear();
                m_records.clear();
            }

          private:
            TransferMultiView *constCast() const
            {
                return const_cast<TransferMultiView *>(this);
            }

            std::pair<iterator, bool> insertRecord(T &&record)
            {
                if constexpr (DescriptorUnique)
                {
                    /* The descriptor of the incoming record is built from a
                       temporary, which is fine because it is only used to look
                       up an existing entry. */
                    const T &probe = record;
                    auto existing = m_byDescriptor.find(probe.getSpentOutputDescriptor());

                    if (existing != m_byDescriptor.end())
                    {
                        return {existing->second, false};
                    }
                }

                auto node = m_records.insert(m_records.end(), std::move(record));

                m_byDescriptor.emplace(node->getSpentOutputDescriptor(), node);
                m_byTransaction.emplace(node->getTransactionHash(), node);

                if constexpr (HasSpendingIndex)
                {
                    m_bySpending.emplace(node->getSpendingTransactionHash(), node);
                }

                return {node, true};
            }

            /* Drops a record from every view. The already erased argument names the
               map entry the caller is erasing through, if any, so that it is not
               looked up again after the caller has removed it. */
            void eraseNode(
                iterator node,
                const typename DescriptorMap::iterator *descriptorEntry = nullptr,
                const typename HashMap::iterator *spendingEntry = nullptr,
                const typename HashMap::iterator *containingEntry = nullptr)
            {
                if (descriptorEntry != nullptr)
                {
                    m_byDescriptor.erase(*descriptorEntry);
                }
                else
                {
                    eraseDescriptorEntry(node);
                }

                if (containingEntry != nullptr)
                {
                    m_byTransaction.erase(*containingEntry);
                }
                else
                {
                    eraseHashEntry(m_byTransaction, node->getTransactionHash(), node);
                }

                if constexpr (HasSpendingIndex)
                {
                    if (spendingEntry != nullptr)
                    {
                        m_bySpending.erase(*spendingEntry);
                    }
                    else
                    {
                        eraseHashEntry(m_bySpending, node->getSpendingTransactionHash(), node);
                    }
                }
                else
                {
                    (void)spendingEntry;
                }

                m_records.erase(node);
            }

            void eraseDescriptorEntry(iterator node)
            {
                auto range = m_byDescriptor.equal_range(node->getSpentOutputDescriptor());

                for (auto it = range.first; it != range.second; ++it)
                {
                    if (it->second == node)
                    {
                        m_byDescriptor.erase(it);
                        return;
                    }
                }
            }

            void eraseHashEntry(HashMap &map, const Crypto::Hash &hash, iterator node)
            {
                auto range = map.equal_range(hash);

                for (auto it = range.first; it != range.second; ++it)
                {
                    if (it->second == node)
                    {
                        map.erase(it);
                        return;
                    }
                }
            }

            /* Overwrites a record in place. Views are only re-keyed when the key
               they are built on actually changed, so an iterator held by the caller
               survives a replacement that leaves the keys alone. */
            bool replaceNode(iterator node, const T &value)
            {
                const DescriptorFields oldDescriptor = DescriptorFields::of(*node);
                const Crypto::Hash oldTransactionHash = node->getTransactionHash();

                Crypto::Hash oldSpendingHash {};

                if constexpr (HasSpendingIndex)
                {
                    oldSpendingHash = node->getSpendingTransactionHash();
                }

                *node = value;

                if (!(DescriptorFields::of(*node) == oldDescriptor))
                {
                    eraseDescriptorEntryByFields(oldDescriptor, node);
                    m_byDescriptor.emplace(node->getSpentOutputDescriptor(), node);
                }

                if (!(node->getTransactionHash() == oldTransactionHash))
                {
                    eraseHashEntry(m_byTransaction, oldTransactionHash, node);
                    m_byTransaction.emplace(node->getTransactionHash(), node);
                }

                if constexpr (HasSpendingIndex)
                {
                    if (!(node->getSpendingTransactionHash() == oldSpendingHash))
                    {
                        eraseHashEntry(m_bySpending, oldSpendingHash, node);
                        m_bySpending.emplace(node->getSpendingTransactionHash(), node);
                    }
                }

                return true;
            }

            /* The record has already been overwritten, so the entry to drop has to
               be found by walking the whole map rather than by hashing the key. */
            void eraseDescriptorEntryByFields(const DescriptorFields &, iterator node)
            {
                for (auto it = m_byDescriptor.begin(); it != m_byDescriptor.end(); ++it)
                {
                    if (it->second == node)
                    {
                        m_byDescriptor.erase(it);
                        return;
                    }
                }
            }

            Records m_records;

            DescriptorMap m_byDescriptor;

            HashMap m_byTransaction;

            HashMap m_bySpending;

            DescriptorView m_descriptorView {this};

            ContainingView m_containingView {this};

            SpendingView m_spendingView {this};

            friend class DescriptorView;
        };

        /* Holds the transactions seen by a transfers container, with a unique view
           keyed on the transaction hash and an ordered view keyed on the block
           height. */
        class TransactionRecords
        {
          private:
            using Records = std::list<TransactionInformation>;

          public:
            using value_type = TransactionInformation;

            using iterator = Records::iterator;

            using const_iterator = iterator;

          private:
            using HashMap = std::unordered_map<Crypto::Hash, iterator>;

            using HeightMap = std::multimap<uint32_t, iterator>;

          public:
            TransactionRecords() = default;

            /* The height view holds a pointer back to the container it belongs to,
               so a move has to move the data and leave the view pointing here. */
            TransactionRecords(TransactionRecords &&other) noexcept:
                m_records(std::move(other.m_records)),
                m_byHash(std::move(other.m_byHash)),
                m_byHeight(std::move(other.m_byHeight))
            {
                other.clear();
            }

            TransactionRecords &operator=(TransactionRecords &&other) noexcept
            {
                if (this != &other)
                {
                    m_records = std::move(other.m_records);
                    m_byHash = std::move(other.m_byHash);
                    m_byHeight = std::move(other.m_byHeight);
                    other.clear();
                }

                return *this;
            }

            TransactionRecords(const TransactionRecords &) = delete;

            TransactionRecords &operator=(const TransactionRecords &) = delete;

            class HeightIterator
            {
              public:
                using iterator_category = std::bidirectional_iterator_tag;

                using value_type = TransactionInformation;

                using difference_type = std::ptrdiff_t;

                using pointer = TransactionInformation *;

                using reference = TransactionInformation &;

                HeightIterator() = default;

                explicit HeightIterator(HeightMap::iterator it): m_it(it) {}

                reference operator*() const
                {
                    return *m_it->second;
                }

                pointer operator->() const
                {
                    return &*m_it->second;
                }

                HeightIterator &operator++()
                {
                    ++m_it;
                    return *this;
                }

                HeightIterator operator++(int)
                {
                    HeightIterator tmp(*this);
                    ++m_it;
                    return tmp;
                }

                HeightIterator &operator--()
                {
                    --m_it;
                    return *this;
                }

                HeightIterator operator--(int)
                {
                    HeightIterator tmp(*this);
                    --m_it;
                    return tmp;
                }

                const HeightMap::iterator &base() const
                {
                    return m_it;
                }

                friend bool operator==(const HeightIterator &a, const HeightIterator &b)
                {
                    return a.m_it == b.m_it;
                }

                friend bool operator!=(const HeightIterator &a, const HeightIterator &b)
                {
                    return a.m_it != b.m_it;
                }

              private:
                HeightMap::iterator m_it {};
            };

            class HeightView
            {
              public:
                using value_type = TransactionInformation;

                using iterator = HeightIterator;

                using const_iterator = HeightIterator;

                explicit HeightView(TransactionRecords *owner): m_owner(owner) {}

                HeightIterator begin() const
                {
                    return HeightIterator(m_owner->m_byHeight.begin());
                }

                HeightIterator end() const
                {
                    return HeightIterator(m_owner->m_byHeight.end());
                }

                HeightIterator lower_bound(uint32_t height) const
                {
                    return HeightIterator(m_owner->m_byHeight.lower_bound(height));
                }

                HeightIterator upper_bound(uint32_t height) const
                {
                    return HeightIterator(m_owner->m_byHeight.upper_bound(height));
                }

                size_t size() const
                {
                    return m_owner->size();
                }

                HeightIterator erase(HeightIterator it) const
                {
                    auto mapIt = it.base();
                    auto next = std::next(mapIt);

                    m_owner->eraseNode(mapIt->second, &mapIt);

                    return HeightIterator(next);
                }

              private:
                TransactionRecords *m_owner;
            };

            template<typename Tag> HeightView &get()
            {
                static_assert(
                    std::is_same_v<Tag, TransactionBlockHeightIndex>, "unknown transaction record index tag");
                return m_heightView;
            }

            template<typename Tag> const HeightView &get() const
            {
                return constCast()->template get<Tag>();
            }

            iterator begin() const
            {
                return constCast()->m_records.begin();
            }

            iterator end() const
            {
                return constCast()->m_records.end();
            }

            size_t size() const
            {
                return m_records.size();
            }

            bool empty() const
            {
                return m_records.empty();
            }

            iterator find(const Crypto::Hash &hash) const
            {
                auto it = constCast()->m_byHash.find(hash);

                if (it == constCast()->m_byHash.end())
                {
                    return end();
                }

                return it->second;
            }

            size_t count(const Crypto::Hash &hash) const
            {
                return constCast()->m_byHash.count(hash);
            }

            std::pair<iterator, bool> insert(TransactionInformation &&record)
            {
                return insertRecord(std::move(record));
            }

            std::pair<iterator, bool> insert(const TransactionInformation &record)
            {
                TransactionInformation copy = record;
                return insertRecord(std::move(copy));
            }

            /* The position hint is accepted and ignored; it is here so that
               std::inserter keeps working. */
            iterator insert(iterator, const TransactionInformation &record)
            {
                TransactionInformation copy = record;
                return insertRecord(std::move(copy)).first;
            }

            iterator erase(iterator it)
            {
                auto next = std::next(it);
                eraseNode(it);
                return next;
            }

            bool replace(iterator node, const TransactionInformation &value)
            {
                const uint32_t oldHeight = node->blockHeight;
                const Crypto::Hash oldHash = node->transactionHash;

                *node = value;

                if (node->blockHeight != oldHeight)
                {
                    eraseHeightEntry(oldHeight, node);
                    m_byHeight.emplace(node->blockHeight, node);
                }

                if (!(node->transactionHash == oldHash))
                {
                    m_byHash.erase(oldHash);

                    if (!m_byHash.emplace(node->transactionHash, node).second)
                    {
                        return false;
                    }
                }

                return true;
            }

            void clear()
            {
                m_byHash.clear();
                m_byHeight.clear();
                m_records.clear();
            }

          private:
            TransactionRecords *constCast() const
            {
                return const_cast<TransactionRecords *>(this);
            }

            std::pair<iterator, bool> insertRecord(TransactionInformation &&record)
            {
                auto existing = m_byHash.find(record.transactionHash);

                if (existing != m_byHash.end())
                {
                    return {existing->second, false};
                }

                auto node = m_records.insert(m_records.end(), std::move(record));

                m_byHash.emplace(node->transactionHash, node);
                m_byHeight.emplace(node->blockHeight, node);

                return {node, true};
            }

            void eraseNode(iterator node, const HeightMap::iterator *heightEntry = nullptr)
            {
                m_byHash.erase(node->transactionHash);

                if (heightEntry != nullptr)
                {
                    m_byHeight.erase(*heightEntry);
                }
                else
                {
                    eraseHeightEntry(node->blockHeight, node);
                }

                m_records.erase(node);
            }

            void eraseHeightEntry(uint32_t height, iterator node)
            {
                auto range = m_byHeight.equal_range(height);

                for (auto it = range.first; it != range.second; ++it)
                {
                    if (it->second == node)
                    {
                        m_byHeight.erase(it);
                        return;
                    }
                }
            }

            Records m_records;

            HashMap m_byHash;

            HeightMap m_byHeight;

            HeightView m_heightView {this};
        };
    } // namespace TransfersDetail

    class TransfersContainer : public ITransfersContainer
    {
      public:
        TransfersContainer(
            const CryptoNote::Currency &currency,
            std::shared_ptr<Logging::ILogger> logger,
            size_t transactionSpendableAge);

        bool addTransaction(
            const TransactionBlockInfo &block,
            const ITransactionReader &tx,
            const std::vector<TransactionOutputInformationIn> &transfers);

        bool deleteUnconfirmedTransaction(const Crypto::Hash &transactionHash);

        bool markTransactionConfirmed(
            const TransactionBlockInfo &block,
            const Crypto::Hash &transactionHash,
            const std::vector<uint32_t> &globalIndices);

        std::vector<Crypto::Hash> detach(uint32_t height);

        bool advanceHeight(uint32_t height);

        // ITransfersContainer
        virtual size_t transactionsCount() const override;

        virtual uint64_t balance(uint32_t flags) const override;

        virtual void getOutputs(std::vector<TransactionOutputInformation> &transfers, uint32_t flags) const override;

        virtual bool getTransactionInformation(
            const Crypto::Hash &transactionHash,
            TransactionInformation &info,
            uint64_t *amountIn = nullptr,
            uint64_t *amountOut = nullptr) const override;

        virtual std::vector<TransactionOutputInformation>
            getTransactionOutputs(const Crypto::Hash &transactionHash, uint32_t flags) const override;

        // only type flags are feasible for this function
        virtual std::vector<TransactionOutputInformation>
            getTransactionInputs(const Crypto::Hash &transactionHash, uint32_t flags) const override;

        virtual void getUnconfirmedTransactions(std::vector<Crypto::Hash> &transactions) const override;

        virtual std::vector<SpentTransactionOutput> getUnspentInputs() const override;

        virtual std::vector<SpentTransactionOutput> getSpentInputs() const override;

        // IStreamSerializable
        virtual void save(std::ostream &os) override;

        virtual void load(std::istream &in) override;

      private:
        using SpentOutputDescriptorIndex = TransfersDetail::SpentOutputDescriptorIndex;

        using ContainingTransactionIndex = TransfersDetail::ContainingTransactionIndex;

        using SpendingTransactionIndex = TransfersDetail::SpendingTransactionIndex;

        using TransactionBlockHeightIndex = TransfersDetail::TransactionBlockHeightIndex;

        using TransactionMultiIndex = TransfersDetail::TransactionRecords;

        using UnconfirmedTransfersMultiIndex =
            TransfersDetail::TransferMultiView<TransactionOutputInformationEx, false, false>;

        using AvailableTransfersMultiIndex =
            TransfersDetail::TransferMultiView<TransactionOutputInformationEx, false, false>;

        using SpentTransfersMultiIndex = TransfersDetail::TransferMultiView<SpentTransactionOutput, true, true>;

      private:
        void addTransaction(const TransactionBlockInfo &block, const ITransactionReader &tx);

        bool addTransactionOutputs(
            const TransactionBlockInfo &block,
            const ITransactionReader &tx,
            const std::vector<TransactionOutputInformationIn> &transfers);

        bool addTransactionInputs(const TransactionBlockInfo &block, const ITransactionReader &tx);

        void deleteTransactionTransfers(const Crypto::Hash &transactionHash);

        bool isSpendTimeUnlocked(uint64_t unlockTime) const;

        bool isIncluded(const TransactionOutputInformationEx &info, uint32_t flags) const;

        static bool isIncluded(TransactionTypes::OutputType type, uint32_t state, uint32_t flags);

        void updateTransfersVisibility(const Crypto::KeyImage &keyImage);

        void copyToSpent(
            const TransactionBlockInfo &block,
            const ITransactionReader &tx,
            size_t inputIndex,
            const TransactionOutputInformationEx &output);

      private:
        TransactionMultiIndex m_transactions;

        UnconfirmedTransfersMultiIndex m_unconfirmedTransfers;

        AvailableTransfersMultiIndex m_availableTransfers;

        SpentTransfersMultiIndex m_spentTransfers;

        uint32_t m_currentHeight; // current height is needed to check if a transfer is unlocked
        size_t m_transactionSpendableAge;

        const CryptoNote::Currency &m_currency;

        mutable std::mutex m_mutex;

        Logging::LoggerRef m_logger;
    };

} // namespace CryptoNote
