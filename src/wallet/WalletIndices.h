// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "ITransfersContainer.h"
#include "WalletGreenTypes.h"
#include "common/FileMappedVector.h"
#include "crypto/chacha8.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <map>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CryptoNote
{
    const uint64_t ACCOUNT_CREATE_TIME_ACCURACY = 60 * 60 * 24;

    struct WalletRecord
    {
        Crypto::PublicKey spendPublicKey;
        Crypto::SecretKey spendSecretKey;
        CryptoNote::ITransfersContainer *container = nullptr;
        uint64_t pendingBalance = 0;
        uint64_t actualBalance = 0;
        time_t creationTimestamp;
    };

#pragma pack(push, 1)
    struct EncryptedWalletRecord
    {
        Crypto::chacha8_iv iv;
        // Secret key, public key and creation timestamp
        uint8_t data[sizeof(Crypto::PublicKey) + sizeof(Crypto::SecretKey) + sizeof(uint64_t)];
    };
#pragma pack(pop)

    /* Index tags. These name a view of one of the containers below. They used to
       select an index of a boost::multi_index_container; the containers are now
       written by hand on top of the standard library, but the tags are kept so
       that the calling code reads the same way. */
    struct RandomAccessIndex
    {
    };
    struct KeysIndex
    {
    };
    struct TransfersContainerIndex
    {
    };

    struct WalletIndex
    {
    };
    struct TransactionOutputIndex
    {
    };
    struct BlockHeightIndex
    {
    };

    struct TransactionHashIndex
    {
    };
    struct TransactionIndex
    {
    };
    struct BlockHashIndex
    {
    };

    namespace WalletIndicesDetail
    {
        /* Random access iterator over a vector of owning pointers that hands out
           references to the pointees. Used wherever the old code walked the
           random access index of a multi index container. */
        template<typename T> class PointerVectorIterator
        {
          public:
            using iterator_category = std::random_access_iterator_tag;

            using value_type = T;

            using difference_type = std::ptrdiff_t;

            using pointer = T *;

            using reference = T &;

            using Slot = std::unique_ptr<T>;

            PointerVectorIterator(): m_slot(nullptr) {}

            explicit PointerVectorIterator(const Slot *slot): m_slot(slot) {}

            reference operator*() const
            {
                return **m_slot;
            }

            pointer operator->() const
            {
                return m_slot->get();
            }

            reference operator[](difference_type n) const
            {
                return *m_slot[n];
            }

            PointerVectorIterator &operator++()
            {
                ++m_slot;
                return *this;
            }

            PointerVectorIterator operator++(int)
            {
                PointerVectorIterator tmp(*this);
                ++m_slot;
                return tmp;
            }

            PointerVectorIterator &operator--()
            {
                --m_slot;
                return *this;
            }

            PointerVectorIterator operator--(int)
            {
                PointerVectorIterator tmp(*this);
                --m_slot;
                return tmp;
            }

            PointerVectorIterator &operator+=(difference_type n)
            {
                m_slot += n;
                return *this;
            }

            PointerVectorIterator &operator-=(difference_type n)
            {
                m_slot -= n;
                return *this;
            }

            friend PointerVectorIterator operator+(PointerVectorIterator it, difference_type n)
            {
                it += n;
                return it;
            }

            friend PointerVectorIterator operator+(difference_type n, PointerVectorIterator it)
            {
                it += n;
                return it;
            }

            friend PointerVectorIterator operator-(PointerVectorIterator it, difference_type n)
            {
                it -= n;
                return it;
            }

            friend difference_type operator-(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot - b.m_slot;
            }

            friend bool operator==(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot == b.m_slot;
            }

            friend bool operator!=(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot != b.m_slot;
            }

            friend bool operator<(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot < b.m_slot;
            }

            friend bool operator>(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot > b.m_slot;
            }

            friend bool operator<=(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot <= b.m_slot;
            }

            friend bool operator>=(const PointerVectorIterator &a, const PointerVectorIterator &b)
            {
                return a.m_slot >= b.m_slot;
            }

          private:
            const Slot *m_slot;
        };

        /* Forward iterator over an associative container whose mapped type is a
           pointer to the record, handing out references to the record. */
        template<typename T, typename MapIterator> class MappedPointerIterator
        {
          public:
            using iterator_category = std::forward_iterator_tag;

            using value_type = T;

            using difference_type = std::ptrdiff_t;

            using pointer = T *;

            using reference = T &;

            MappedPointerIterator() = default;

            explicit MappedPointerIterator(MapIterator it): m_it(it) {}

            reference operator*() const
            {
                return *m_it->second;
            }

            pointer operator->() const
            {
                return m_it->second;
            }

            MappedPointerIterator &operator++()
            {
                ++m_it;
                return *this;
            }

            MappedPointerIterator operator++(int)
            {
                MappedPointerIterator tmp(*this);
                ++m_it;
                return tmp;
            }

            const MapIterator &base() const
            {
                return m_it;
            }

            friend bool operator==(const MappedPointerIterator &a, const MappedPointerIterator &b)
            {
                return a.m_it == b.m_it;
            }

            friend bool operator!=(const MappedPointerIterator &a, const MappedPointerIterator &b)
            {
                return a.m_it != b.m_it;
            }

          private:
            MapIterator m_it {};
        };
    } // namespace WalletIndicesDetail

    /* Holds the sub wallets. It offers a random access view, a view keyed on the
       spend public key and a view keyed on the transfers container pointer.

       Records live in owning pointers so that every view can hold a raw pointer
       that stays valid across insertion and erasure. */
    class WalletsContainer
    {
      private:
        using Slot = std::unique_ptr<WalletRecord>;

        using KeyMap = std::unordered_map<Crypto::PublicKey, WalletRecord *>;

        using ContainerMap = std::unordered_map<const CryptoNote::ITransfersContainer *, WalletRecord *>;

      public:
        using value_type = WalletRecord;

        using iterator = WalletIndicesDetail::PointerVectorIterator<WalletRecord>;

        using const_iterator = iterator;

        using key_iterator = WalletIndicesDetail::MappedPointerIterator<WalletRecord, KeyMap::iterator>;

        using container_iterator = WalletIndicesDetail::MappedPointerIterator<WalletRecord, ContainerMap::iterator>;

        WalletsContainer() = default;

        WalletsContainer(const WalletsContainer &) = delete;

        WalletsContainer &operator=(const WalletsContainer &) = delete;

        /* Random access view, ordered by the order the sub wallets were added. */
        class SequenceView
        {
          public:
            using value_type = WalletRecord;

            using iterator = WalletsContainer::iterator;

            using const_iterator = WalletsContainer::iterator;

            explicit SequenceView(WalletsContainer *owner): m_owner(owner) {}

            iterator begin() const
            {
                return m_owner->begin();
            }

            iterator end() const
            {
                return m_owner->end();
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            WalletRecord &operator[](size_t index) const
            {
                return (*m_owner)[index];
            }

            bool push_back(WalletRecord &&record) const
            {
                return m_owner->pushBack(std::move(record));
            }

            template<typename Modifier> bool modify(iterator it, Modifier &&modifier) const
            {
                return m_owner->modifyRecord(&*it, std::forward<Modifier>(modifier));
            }

          private:
            WalletsContainer *m_owner;
        };

        /* View keyed on the sub wallet spend public key. */
        class KeysView
        {
          public:
            using value_type = WalletRecord;

            using iterator = key_iterator;

            using const_iterator = key_iterator;

            explicit KeysView(WalletsContainer *owner): m_owner(owner) {}

            key_iterator begin() const
            {
                return key_iterator(m_owner->m_byKey.begin());
            }

            key_iterator end() const
            {
                return key_iterator(m_owner->m_byKey.end());
            }

            key_iterator find(const Crypto::PublicKey &key) const
            {
                return key_iterator(m_owner->m_byKey.find(key));
            }

            size_t count(const Crypto::PublicKey &key) const
            {
                return m_owner->m_byKey.count(key);
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            /* The position hint is accepted and ignored, matching the hashed index
               this view replaces. */
            bool insert(key_iterator, WalletRecord &&record) const
            {
                return m_owner->pushBack(std::move(record));
            }

            bool insert(WalletRecord &&record) const
            {
                return m_owner->pushBack(std::move(record));
            }

            void erase(key_iterator it) const
            {
                m_owner->eraseRecord(&*it);
            }

            size_t erase(const Crypto::PublicKey &key) const
            {
                auto it = m_owner->m_byKey.find(key);

                if (it == m_owner->m_byKey.end())
                {
                    return 0;
                }

                m_owner->eraseRecord(it->second);
                return 1;
            }

            template<typename Modifier> bool modify(key_iterator it, Modifier &&modifier) const
            {
                return m_owner->modifyRecord(&*it, std::forward<Modifier>(modifier));
            }

          private:
            WalletsContainer *m_owner;
        };

        /* View keyed on the transfers container pointer. */
        class ContainersView
        {
          public:
            using value_type = WalletRecord;

            using iterator = container_iterator;

            using const_iterator = container_iterator;

            explicit ContainersView(WalletsContainer *owner): m_owner(owner) {}

            container_iterator begin() const
            {
                return container_iterator(m_owner->m_byContainer.begin());
            }

            container_iterator end() const
            {
                return container_iterator(m_owner->m_byContainer.end());
            }

            container_iterator find(const CryptoNote::ITransfersContainer *container) const
            {
                return container_iterator(m_owner->m_byContainer.find(container));
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            template<typename Modifier> bool modify(container_iterator it, Modifier &&modifier) const
            {
                return m_owner->modifyRecord(&*it, std::forward<Modifier>(modifier));
            }

          private:
            WalletsContainer *m_owner;
        };

        template<typename Tag> auto &get()
        {
            if constexpr (std::is_same_v<Tag, RandomAccessIndex>)
            {
                return m_sequenceView;
            }
            else if constexpr (std::is_same_v<Tag, KeysIndex>)
            {
                return m_keysView;
            }
            else
            {
                static_assert(std::is_same_v<Tag, TransfersContainerIndex>, "unknown wallet index tag");
                return m_containersView;
            }
        }

        template<typename Tag> const auto &get() const
        {
            return const_cast<WalletsContainer *>(this)->template get<Tag>();
        }

        /* Translate an iterator of one view into an iterator of another. */
        template<typename Tag> auto project(key_iterator it) const
        {
            return projectRecord<Tag>(&*it);
        }

        template<typename Tag> auto project(container_iterator it) const
        {
            return projectRecord<Tag>(&*it);
        }

        template<typename Tag> auto project(iterator it) const
        {
            return projectRecord<Tag>(&*it);
        }

        iterator begin() const
        {
            return iterator(m_records.data());
        }

        iterator end() const
        {
            return iterator(m_records.data() + m_records.size());
        }

        size_t size() const
        {
            return m_records.size();
        }

        bool empty() const
        {
            return m_records.empty();
        }

        WalletRecord &operator[](size_t index) const
        {
            assert(index < m_records.size());
            return *m_records[index];
        }

        bool push_back(WalletRecord &&record)
        {
            return pushBack(std::move(record));
        }

        void clear()
        {
            m_byKey.clear();
            m_byContainer.clear();
            m_records.clear();
        }

        template<typename Modifier> bool modify(iterator it, Modifier &&modifier)
        {
            return modifyRecord(&*it, std::forward<Modifier>(modifier));
        }

      private:
        template<typename Tag> auto projectRecord(WalletRecord *record) const
        {
            auto *self = const_cast<WalletsContainer *>(this);

            if constexpr (std::is_same_v<Tag, RandomAccessIndex>)
            {
                return begin() + static_cast<std::ptrdiff_t>(positionOf(record));
            }
            else if constexpr (std::is_same_v<Tag, KeysIndex>)
            {
                return key_iterator(self->m_byKey.find(record->spendPublicKey));
            }
            else
            {
                static_assert(std::is_same_v<Tag, TransfersContainerIndex>, "unknown wallet index tag");
                return container_iterator(self->m_byContainer.find(record->container));
            }
        }

        size_t positionOf(const WalletRecord *record) const
        {
            for (size_t i = 0; i < m_records.size(); ++i)
            {
                if (m_records[i].get() == record)
                {
                    return i;
                }
            }

            return m_records.size();
        }

        bool pushBack(WalletRecord &&record)
        {
            /* Both secondary views are unique, so a clash on either one rejects the
               insertion exactly as the hashed indexes used to. */
            if (m_byKey.count(record.spendPublicKey) != 0 || m_byContainer.count(record.container) != 0)
            {
                return false;
            }

            auto slot = std::make_unique<WalletRecord>(std::move(record));
            WalletRecord *raw = slot.get();

            m_records.push_back(std::move(slot));
            m_byKey.emplace(raw->spendPublicKey, raw);
            m_byContainer.emplace(raw->container, raw);

            return true;
        }

        void eraseRecord(WalletRecord *record)
        {
            const size_t position = positionOf(record);

            if (position == m_records.size())
            {
                return;
            }

            m_byKey.erase(record->spendPublicKey);
            m_byContainer.erase(record->container);
            m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(position));
        }

        template<typename Modifier> bool modifyRecord(WalletRecord *record, Modifier &&modifier)
        {
            const Crypto::PublicKey oldKey = record->spendPublicKey;
            const CryptoNote::ITransfersContainer *oldContainer = record->container;

            modifier(*record);

            bool consistent = true;

            if (!(record->spendPublicKey == oldKey))
            {
                m_byKey.erase(oldKey);

                if (!m_byKey.emplace(record->spendPublicKey, record).second)
                {
                    consistent = false;
                }
            }

            if (record->container != oldContainer)
            {
                m_byContainer.erase(oldContainer);

                if (!m_byContainer.emplace(record->container, record).second)
                {
                    consistent = false;
                }
            }

            return consistent;
        }

        std::vector<Slot> m_records;

        KeyMap m_byKey;

        ContainerMap m_byContainer;

        SequenceView m_sequenceView {this};

        KeysView m_keysView {this};

        ContainersView m_containersView {this};

        friend class SequenceView;

        friend class KeysView;

        friend class ContainersView;
    };

    struct UnlockTransactionJob
    {
        uint32_t blockHeight;
        CryptoNote::ITransfersContainer *container;
        Crypto::Hash transactionHash;
    };

    /* Holds the pending balance unlock jobs. The primary view is ordered on block
       height, the secondary view is keyed on the transaction hash, and neither one
       is unique. */
    class UnlockTransactionJobs
    {
      private:
        using HeightMap = std::multimap<uint32_t, UnlockTransactionJob>;

        using HashMap = std::unordered_multimap<Crypto::Hash, HeightMap::iterator>;

      public:
        using value_type = UnlockTransactionJob;

        class height_iterator
        {
          public:
            using iterator_category = std::bidirectional_iterator_tag;

            using value_type = UnlockTransactionJob;

            using difference_type = std::ptrdiff_t;

            using pointer = const UnlockTransactionJob *;

            using reference = const UnlockTransactionJob &;

            height_iterator() = default;

            explicit height_iterator(HeightMap::iterator it): m_it(it) {}

            reference operator*() const
            {
                return m_it->second;
            }

            pointer operator->() const
            {
                return &m_it->second;
            }

            height_iterator &operator++()
            {
                ++m_it;
                return *this;
            }

            height_iterator operator++(int)
            {
                height_iterator tmp(*this);
                ++m_it;
                return tmp;
            }

            height_iterator &operator--()
            {
                --m_it;
                return *this;
            }

            const HeightMap::iterator &base() const
            {
                return m_it;
            }

            friend bool operator==(const height_iterator &a, const height_iterator &b)
            {
                return a.m_it == b.m_it;
            }

            friend bool operator!=(const height_iterator &a, const height_iterator &b)
            {
                return a.m_it != b.m_it;
            }

          private:
            HeightMap::iterator m_it {};
        };

        class hash_iterator
        {
          public:
            using iterator_category = std::forward_iterator_tag;

            using value_type = UnlockTransactionJob;

            using difference_type = std::ptrdiff_t;

            using pointer = const UnlockTransactionJob *;

            using reference = const UnlockTransactionJob &;

            hash_iterator() = default;

            explicit hash_iterator(HashMap::iterator it): m_it(it) {}

            reference operator*() const
            {
                return m_it->second->second;
            }

            pointer operator->() const
            {
                return &m_it->second->second;
            }

            hash_iterator &operator++()
            {
                ++m_it;
                return *this;
            }

            hash_iterator operator++(int)
            {
                hash_iterator tmp(*this);
                ++m_it;
                return tmp;
            }

            friend bool operator==(const hash_iterator &a, const hash_iterator &b)
            {
                return a.m_it == b.m_it;
            }

            friend bool operator!=(const hash_iterator &a, const hash_iterator &b)
            {
                return a.m_it != b.m_it;
            }

          private:
            HashMap::iterator m_it {};
        };

        using iterator = height_iterator;

        using const_iterator = height_iterator;

        UnlockTransactionJobs() = default;

        UnlockTransactionJobs(const UnlockTransactionJobs &) = delete;

        UnlockTransactionJobs &operator=(const UnlockTransactionJobs &) = delete;

        class HeightView
        {
          public:
            using value_type = UnlockTransactionJob;

            using iterator = height_iterator;

            using const_iterator = height_iterator;

            explicit HeightView(UnlockTransactionJobs *owner): m_owner(owner) {}

            height_iterator begin() const
            {
                return height_iterator(m_owner->m_byHeight.begin());
            }

            height_iterator end() const
            {
                return height_iterator(m_owner->m_byHeight.end());
            }

            height_iterator lower_bound(uint32_t height) const
            {
                return height_iterator(m_owner->m_byHeight.lower_bound(height));
            }

            height_iterator upper_bound(uint32_t height) const
            {
                return height_iterator(m_owner->m_byHeight.upper_bound(height));
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            height_iterator insert(UnlockTransactionJob job) const
            {
                return m_owner->insertJob(std::move(job));
            }

            height_iterator erase(height_iterator it) const
            {
                return m_owner->eraseJob(it);
            }

            height_iterator erase(height_iterator first, height_iterator last) const
            {
                while (first != last)
                {
                    first = m_owner->eraseJob(first);
                }

                return last;
            }

          private:
            UnlockTransactionJobs *m_owner;
        };

        class HashView
        {
          public:
            using value_type = UnlockTransactionJob;

            using iterator = hash_iterator;

            using const_iterator = hash_iterator;

            explicit HashView(UnlockTransactionJobs *owner): m_owner(owner) {}

            hash_iterator begin() const
            {
                return hash_iterator(m_owner->m_byHash.begin());
            }

            hash_iterator end() const
            {
                return hash_iterator(m_owner->m_byHash.end());
            }

            hash_iterator find(const Crypto::Hash &hash) const
            {
                return hash_iterator(m_owner->m_byHash.find(hash));
            }

            size_t count(const Crypto::Hash &hash) const
            {
                return m_owner->m_byHash.count(hash);
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            void insert(UnlockTransactionJob job) const
            {
                m_owner->insertJob(std::move(job));
            }

            size_t erase(const Crypto::Hash &hash) const
            {
                return m_owner->eraseByHash(hash);
            }

          private:
            UnlockTransactionJobs *m_owner;
        };

        template<typename Tag> auto &get()
        {
            if constexpr (std::is_same_v<Tag, BlockHeightIndex>)
            {
                return m_heightView;
            }
            else
            {
                static_assert(std::is_same_v<Tag, TransactionHashIndex>, "unknown unlock job index tag");
                return m_hashView;
            }
        }

        template<typename Tag> const auto &get() const
        {
            return const_cast<UnlockTransactionJobs *>(this)->template get<Tag>();
        }

        height_iterator begin() const
        {
            return height_iterator(constCast()->m_byHeight.begin());
        }

        height_iterator end() const
        {
            return height_iterator(constCast()->m_byHeight.end());
        }

        size_t size() const
        {
            return m_byHeight.size();
        }

        bool empty() const
        {
            return m_byHeight.empty();
        }

        height_iterator insert(UnlockTransactionJob job)
        {
            return insertJob(std::move(job));
        }

        height_iterator erase(height_iterator it)
        {
            return eraseJob(it);
        }

        void clear()
        {
            m_byHash.clear();
            m_byHeight.clear();
        }

      private:
        UnlockTransactionJobs *constCast() const
        {
            return const_cast<UnlockTransactionJobs *>(this);
        }

        height_iterator insertJob(UnlockTransactionJob &&job)
        {
            const Crypto::Hash hash = job.transactionHash;
            const uint32_t height = job.blockHeight;
            auto it = m_byHeight.emplace(height, std::move(job));

            m_byHash.emplace(hash, it);

            return height_iterator(it);
        }

        height_iterator eraseJob(height_iterator it)
        {
            auto heightIt = it.base();
            eraseHashEntry(heightIt);

            return height_iterator(m_byHeight.erase(heightIt));
        }

        void eraseHashEntry(HeightMap::iterator heightIt)
        {
            auto range = m_byHash.equal_range(heightIt->second.transactionHash);

            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second == heightIt)
                {
                    m_byHash.erase(it);
                    return;
                }
            }
        }

        size_t eraseByHash(const Crypto::Hash &hash)
        {
            auto range = m_byHash.equal_range(hash);
            size_t erased = 0;

            for (auto it = range.first; it != range.second;)
            {
                m_byHeight.erase(it->second);
                it = m_byHash.erase(it);
                ++erased;
            }

            return erased;
        }

        HeightMap m_byHeight;

        HashMap m_byHash;

        HeightView m_heightView {this};

        HashView m_hashView {this};

        friend class HeightView;

        friend class HashView;
    };

    /* Holds the wallet transactions. It offers a random access view, a unique view
       keyed on the transaction hash and an ordered view keyed on the block height.

       Transactions are only ever appended or cleared wholesale, so the random
       access position of a transaction never changes while it is stored, and that
       position is what both secondary views record. */
    class WalletTransactions
    {
      private:
        using Slot = std::unique_ptr<CryptoNote::WalletTransaction>;

        using HashMap = std::unordered_map<Crypto::Hash, size_t>;

        using HeightMap = std::multimap<uint32_t, size_t>;

      public:
        using value_type = CryptoNote::WalletTransaction;

        using iterator = WalletIndicesDetail::PointerVectorIterator<CryptoNote::WalletTransaction>;

        using const_iterator = iterator;

        WalletTransactions() = default;

        WalletTransactions(const WalletTransactions &) = delete;

        WalletTransactions &operator=(const WalletTransactions &) = delete;

        /* Iterator over a secondary view. The mapped value is the random access
           position of the transaction. */
        template<typename MapIterator> class PositionIterator
        {
          public:
            using iterator_category = std::bidirectional_iterator_tag;

            using value_type = CryptoNote::WalletTransaction;

            using difference_type = std::ptrdiff_t;

            using pointer = CryptoNote::WalletTransaction *;

            using reference = CryptoNote::WalletTransaction &;

            PositionIterator() = default;

            PositionIterator(const WalletTransactions *owner, MapIterator it): m_owner(owner), m_it(it) {}

            reference operator*() const
            {
                return (*m_owner)[m_it->second];
            }

            pointer operator->() const
            {
                return &(*m_owner)[m_it->second];
            }

            PositionIterator &operator++()
            {
                ++m_it;
                return *this;
            }

            PositionIterator operator++(int)
            {
                PositionIterator tmp(*this);
                ++m_it;
                return tmp;
            }

            PositionIterator &operator--()
            {
                --m_it;
                return *this;
            }

            size_t position() const
            {
                return m_it->second;
            }

            friend bool operator==(const PositionIterator &a, const PositionIterator &b)
            {
                return a.m_it == b.m_it;
            }

            friend bool operator!=(const PositionIterator &a, const PositionIterator &b)
            {
                return a.m_it != b.m_it;
            }

          private:
            const WalletTransactions *m_owner {nullptr};

            MapIterator m_it {};
        };

        using hash_iterator = PositionIterator<HashMap::iterator>;

        using height_iterator = PositionIterator<HeightMap::iterator>;

        class SequenceView
        {
          public:
            using value_type = CryptoNote::WalletTransaction;

            using iterator = WalletTransactions::iterator;

            using const_iterator = WalletTransactions::iterator;

            explicit SequenceView(WalletTransactions *owner): m_owner(owner) {}

            iterator begin() const
            {
                return m_owner->begin();
            }

            iterator end() const
            {
                return m_owner->end();
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            void reserve(size_t count) const
            {
                m_owner->reserve(count);
            }

            CryptoNote::WalletTransaction &operator[](size_t index) const
            {
                return (*m_owner)[index];
            }

            bool push_back(CryptoNote::WalletTransaction &&transaction) const
            {
                return m_owner->pushBack(std::move(transaction));
            }

            bool push_back(const CryptoNote::WalletTransaction &transaction) const
            {
                CryptoNote::WalletTransaction copy = transaction;
                return m_owner->pushBack(std::move(copy));
            }

            iterator iterator_to(const CryptoNote::WalletTransaction &transaction) const
            {
                return m_owner->iteratorTo(transaction);
            }

            template<typename Modifier> bool modify(iterator it, Modifier &&modifier) const
            {
                return m_owner->modifyAt(
                    static_cast<size_t>(it - m_owner->begin()), std::forward<Modifier>(modifier));
            }

          private:
            WalletTransactions *m_owner;
        };

        class HashView
        {
          public:
            using value_type = CryptoNote::WalletTransaction;

            using iterator = hash_iterator;

            using const_iterator = hash_iterator;

            explicit HashView(WalletTransactions *owner): m_owner(owner) {}

            hash_iterator begin() const
            {
                return hash_iterator(m_owner, m_owner->m_byHash.begin());
            }

            hash_iterator end() const
            {
                return hash_iterator(m_owner, m_owner->m_byHash.end());
            }

            hash_iterator find(const Crypto::Hash &hash) const
            {
                return hash_iterator(m_owner, m_owner->m_byHash.find(hash));
            }

            size_t count(const Crypto::Hash &hash) const
            {
                return m_owner->m_byHash.count(hash);
            }

            size_t size() const
            {
                return m_owner->size();
            }

            template<typename Modifier> bool modify(hash_iterator it, Modifier &&modifier) const
            {
                return m_owner->modifyAt(it.position(), std::forward<Modifier>(modifier));
            }

          private:
            WalletTransactions *m_owner;
        };

        class HeightView
        {
          public:
            using value_type = CryptoNote::WalletTransaction;

            using iterator = height_iterator;

            using const_iterator = height_iterator;

            explicit HeightView(WalletTransactions *owner): m_owner(owner) {}

            height_iterator begin() const
            {
                return height_iterator(m_owner, m_owner->m_byHeight.begin());
            }

            height_iterator end() const
            {
                return height_iterator(m_owner, m_owner->m_byHeight.end());
            }

            height_iterator lower_bound(uint32_t height) const
            {
                return height_iterator(m_owner, m_owner->m_byHeight.lower_bound(height));
            }

            height_iterator upper_bound(uint32_t height) const
            {
                return height_iterator(m_owner, m_owner->m_byHeight.upper_bound(height));
            }

            size_t size() const
            {
                return m_owner->size();
            }

            template<typename Modifier> bool modify(height_iterator it, Modifier &&modifier) const
            {
                return m_owner->modifyAt(it.position(), std::forward<Modifier>(modifier));
            }

          private:
            WalletTransactions *m_owner;
        };

        template<typename Tag> auto &get()
        {
            if constexpr (std::is_same_v<Tag, RandomAccessIndex>)
            {
                return m_sequenceView;
            }
            else if constexpr (std::is_same_v<Tag, TransactionIndex>)
            {
                return m_hashView;
            }
            else
            {
                static_assert(std::is_same_v<Tag, BlockHeightIndex>, "unknown wallet transaction index tag");
                return m_heightView;
            }
        }

        template<typename Tag> const auto &get() const
        {
            return const_cast<WalletTransactions *>(this)->template get<Tag>();
        }

        template<typename Tag> auto project(hash_iterator it) const
        {
            return projectPosition<Tag>(it.position());
        }

        template<typename Tag> auto project(height_iterator it) const
        {
            return projectPosition<Tag>(it.position());
        }

        template<typename Tag> auto project(iterator it) const
        {
            return projectPosition<Tag>(static_cast<size_t>(it - begin()));
        }

        iterator begin() const
        {
            return iterator(m_transactions.data());
        }

        iterator end() const
        {
            return iterator(m_transactions.data() + m_transactions.size());
        }

        size_t size() const
        {
            return m_transactions.size();
        }

        bool empty() const
        {
            return m_transactions.empty();
        }

        void reserve(size_t count)
        {
            m_transactions.reserve(count);
            m_byHash.reserve(count);
        }

        CryptoNote::WalletTransaction &operator[](size_t index) const
        {
            assert(index < m_transactions.size());
            return *m_transactions[index];
        }

        bool push_back(const CryptoNote::WalletTransaction &transaction)
        {
            CryptoNote::WalletTransaction copy = transaction;
            return pushBack(std::move(copy));
        }

        bool push_back(CryptoNote::WalletTransaction &&transaction)
        {
            return pushBack(std::move(transaction));
        }

        template<typename Modifier> bool modify(iterator it, Modifier &&modifier)
        {
            return modifyAt(static_cast<size_t>(it - begin()), std::forward<Modifier>(modifier));
        }

        void clear()
        {
            m_byHash.clear();
            m_byHeight.clear();
            m_transactions.clear();
        }

      private:
        template<typename Tag> auto projectPosition(size_t position) const
        {
            auto *self = const_cast<WalletTransactions *>(this);

            if constexpr (std::is_same_v<Tag, RandomAccessIndex>)
            {
                return begin() + static_cast<std::ptrdiff_t>(position);
            }
            else if constexpr (std::is_same_v<Tag, TransactionIndex>)
            {
                return hash_iterator(this, self->m_byHash.find((*this)[position].hash));
            }
            else
            {
                static_assert(std::is_same_v<Tag, BlockHeightIndex>, "unknown wallet transaction index tag");
                return height_iterator(this, self->findHeightEntry(position));
            }
        }

        iterator iteratorTo(const CryptoNote::WalletTransaction &transaction) const
        {
            auto it = m_byHash.find(transaction.hash);

            if (it == m_byHash.end())
            {
                return end();
            }

            return begin() + static_cast<std::ptrdiff_t>(it->second);
        }

        bool pushBack(CryptoNote::WalletTransaction &&transaction)
        {
            /* The hash view is unique, so a repeated hash is rejected exactly as
               the hashed index used to reject it. */
            if (m_byHash.count(transaction.hash) != 0)
            {
                return false;
            }

            const size_t position = m_transactions.size();
            const Crypto::Hash hash = transaction.hash;
            const uint32_t blockHeight = transaction.blockHeight;

            m_transactions.push_back(std::make_unique<CryptoNote::WalletTransaction>(std::move(transaction)));
            m_byHash.emplace(hash, position);
            m_byHeight.emplace(blockHeight, position);

            return true;
        }

        HeightMap::iterator findHeightEntry(size_t position)
        {
            auto range = m_byHeight.equal_range((*this)[position].blockHeight);

            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second == position)
                {
                    return it;
                }
            }

            return m_byHeight.end();
        }

        template<typename Modifier> bool modifyAt(size_t position, Modifier &&modifier)
        {
            assert(position < m_transactions.size());

            CryptoNote::WalletTransaction &transaction = *m_transactions[position];

            const Crypto::Hash oldHash = transaction.hash;
            const uint32_t oldHeight = transaction.blockHeight;

            auto heightEntry = findHeightEntry(position);

            modifier(transaction);

            bool consistent = true;

            if (transaction.blockHeight != oldHeight)
            {
                if (heightEntry != m_byHeight.end())
                {
                    m_byHeight.erase(heightEntry);
                }

                m_byHeight.emplace(transaction.blockHeight, position);
            }

            if (!(transaction.hash == oldHash))
            {
                m_byHash.erase(oldHash);

                if (!m_byHash.emplace(transaction.hash, position).second)
                {
                    consistent = false;
                }
            }

            return consistent;
        }

        std::vector<Slot> m_transactions;

        HashMap m_byHash;

        HeightMap m_byHeight;

        SequenceView m_sequenceView {this};

        HashView m_hashView {this};

        HeightView m_heightView {this};

        friend class SequenceView;

        friend class HashView;

        friend class HeightView;
    };

    typedef Common::FileMappedVector<EncryptedWalletRecord> ContainerStorage;

    typedef std::pair<uint64_t, CryptoNote::WalletTransfer> TransactionTransferPair;

    typedef std::vector<TransactionTransferPair> WalletTransfers;

    typedef std::map<uint64_t, CryptoNote::Transaction> UncommitedTransactions;

    /* Holds the known block hashes with a random access view and a unique view
       keyed on the hash. The random access position of a hash is its height. */
    class BlockHashesContainer
    {
      private:
        using HashMap = std::unordered_map<Crypto::Hash, size_t>;

      public:
        using value_type = Crypto::Hash;

        using iterator = std::vector<Crypto::Hash>::const_iterator;

        using const_iterator = iterator;

        BlockHashesContainer() = default;

        BlockHashesContainer(const BlockHashesContainer &) = delete;

        BlockHashesContainer &operator=(const BlockHashesContainer &) = delete;

        class HashIterator
        {
          public:
            using iterator_category = std::forward_iterator_tag;

            using value_type = Crypto::Hash;

            using difference_type = std::ptrdiff_t;

            using pointer = const Crypto::Hash *;

            using reference = const Crypto::Hash &;

            HashIterator() = default;

            explicit HashIterator(HashMap::iterator it): m_it(it) {}

            reference operator*() const
            {
                return m_it->first;
            }

            pointer operator->() const
            {
                return &m_it->first;
            }

            HashIterator &operator++()
            {
                ++m_it;
                return *this;
            }

            HashIterator operator++(int)
            {
                HashIterator tmp(*this);
                ++m_it;
                return tmp;
            }

            size_t position() const
            {
                return m_it->second;
            }

            friend bool operator==(const HashIterator &a, const HashIterator &b)
            {
                return a.m_it == b.m_it;
            }

            friend bool operator!=(const HashIterator &a, const HashIterator &b)
            {
                return a.m_it != b.m_it;
            }

          private:
            HashMap::iterator m_it {};
        };

        using hash_iterator = HashIterator;

        class HeightView
        {
          public:
            using value_type = Crypto::Hash;

            using iterator = BlockHashesContainer::iterator;

            using const_iterator = BlockHashesContainer::iterator;

            explicit HeightView(BlockHashesContainer *owner): m_owner(owner) {}

            iterator begin() const
            {
                return m_owner->begin();
            }

            iterator end() const
            {
                return m_owner->end();
            }

            size_t size() const
            {
                return m_owner->size();
            }

            bool empty() const
            {
                return m_owner->empty();
            }

            const Crypto::Hash &operator[](size_t index) const
            {
                return (*m_owner)[index];
            }

            iterator erase(iterator first, iterator last) const
            {
                return m_owner->eraseRange(first, last);
            }

          private:
            BlockHashesContainer *m_owner;
        };

        class HashView
        {
          public:
            using value_type = Crypto::Hash;

            using iterator = hash_iterator;

            using const_iterator = hash_iterator;

            explicit HashView(BlockHashesContainer *owner): m_owner(owner) {}

            hash_iterator begin() const
            {
                return hash_iterator(m_owner->m_byHash.begin());
            }

            hash_iterator end() const
            {
                return hash_iterator(m_owner->m_byHash.end());
            }

            hash_iterator find(const Crypto::Hash &hash) const
            {
                return hash_iterator(m_owner->m_byHash.find(hash));
            }

            size_t count(const Crypto::Hash &hash) const
            {
                return m_owner->m_byHash.count(hash);
            }

            size_t size() const
            {
                return m_owner->size();
            }

          private:
            BlockHashesContainer *m_owner;
        };

        template<typename Tag> auto &get()
        {
            if constexpr (std::is_same_v<Tag, BlockHeightIndex>)
            {
                return m_heightView;
            }
            else
            {
                static_assert(std::is_same_v<Tag, BlockHashIndex>, "unknown block hash index tag");
                return m_hashView;
            }
        }

        template<typename Tag> const auto &get() const
        {
            return const_cast<BlockHashesContainer *>(this)->template get<Tag>();
        }

        template<typename Tag> auto project(hash_iterator it) const
        {
            static_assert(std::is_same_v<Tag, BlockHeightIndex>, "only the height view can be projected to");
            return begin() + static_cast<std::ptrdiff_t>(it.position());
        }

        iterator begin() const
        {
            return m_hashes.begin();
        }

        iterator end() const
        {
            return m_hashes.end();
        }

        size_t size() const
        {
            return m_hashes.size();
        }

        bool empty() const
        {
            return m_hashes.empty();
        }

        const Crypto::Hash &operator[](size_t index) const
        {
            assert(index < m_hashes.size());
            return m_hashes[index];
        }

        bool push_back(const Crypto::Hash &hash)
        {
            if (m_byHash.count(hash) != 0)
            {
                return false;
            }

            m_byHash.emplace(hash, m_hashes.size());
            m_hashes.push_back(hash);

            return true;
        }

        /* Appends the given hashes. The position argument keeps the call sites
           that read like a sequence container working; hashes are only ever
           appended, so it has to be the end. */
        template<typename InputIterator> void insert(iterator position, InputIterator first, InputIterator last)
        {
            assert(position == end());
            (void)position;

            for (auto it = first; it != last; ++it)
            {
                push_back(*it);
            }
        }

        void clear()
        {
            m_byHash.clear();
            m_hashes.clear();
        }

      private:
        /* Only a tail of the chain is ever dropped, which keeps the recorded
           position of every surviving hash correct. */
        iterator eraseRange(iterator first, iterator last)
        {
            assert(last == end());
            (void)last;

            const size_t from = static_cast<size_t>(first - begin());

            for (size_t i = from; i < m_hashes.size(); ++i)
            {
                m_byHash.erase(m_hashes[i]);
            }

            m_hashes.erase(m_hashes.begin() + static_cast<std::ptrdiff_t>(from), m_hashes.end());

            return end();
        }

        std::vector<Crypto::Hash> m_hashes;

        HashMap m_byHash;

        HeightView m_heightView {this};

        HashView m_hashView {this};

        friend class HeightView;

        friend class HashView;
    };

} // namespace CryptoNote
