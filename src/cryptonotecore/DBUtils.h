// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "common/StdInputStream.h"
#include "common/StdOutputStream.h"
#include "cryptonotecore/CryptoNoteFormatUtils.h"
#include "serialization/CryptoNoteSerialization.h"
#include "serialization/KVBinaryInputStreamSerializer.h"
#include "serialization/KVBinaryOutputStreamSerializer.h"
#include "serialization/SerializationOverloads.h"

#include <sstream>
#include <string>
#include <vector>

namespace CryptoNote::DB
{
    /* Walks the two parallel vectors a raw database read returns: the
       serialized value for each key, and whether that key was found.

       The deserializeValues helpers below each consume as many entries as the
       container they fill, advancing this cursor as they go, so the caller
       hands the same cursor to each one in turn. This replaces a zipped range
       built with boost::combine, whose elements were boost tuples. */
    class RawResultCursor
    {
      public:
        RawResultCursor(const std::vector<std::string> &values, const std::vector<bool> &found):
            m_values(values),
            m_found(found)
        {
        }

        const std::string &value() const
        {
            return m_values.at(m_index);
        }

        bool found() const
        {
            return m_found.at(m_index);
        }

        RawResultCursor &operator++()
        {
            ++m_index;
            return *this;
        }

        /* True once every value handed in has been consumed. */
        bool exhausted() const
        {
            return m_index == m_values.size();
        }

      private:
        const std::vector<std::string> &m_values;

        const std::vector<bool> &m_found;

        std::size_t m_index = 0;
    };

    const std::string BLOCK_INDEX_TO_KEY_IMAGE_PREFIX = "0";
    const std::string BLOCK_INDEX_TO_TX_HASHES_PREFIX = "1";
    const std::string BLOCK_INDEX_TO_TRANSACTION_INFO_PREFIX = "2";
    const std::string BLOCK_INDEX_TO_RAW_BLOCK_PREFIX = "4";
    const std::string BLOCK_HASH_TO_BLOCK_INDEX_PREFIX = "5";
    const std::string BLOCK_INDEX_TO_BLOCK_INFO_PREFIX = "6";
    const std::string KEY_IMAGE_TO_BLOCK_INDEX_PREFIX = "7";
    const std::string BLOCK_INDEX_TO_BLOCK_HASH_PREFIX = "8";
    const std::string TRANSACTION_HASH_TO_TRANSACTION_INFO_PREFIX = "a";
    const std::string KEY_OUTPUT_AMOUNT_PREFIX = "b";
    const std::string CLOSEST_TIMESTAMP_BLOCK_INDEX_PREFIX = "e";
    const std::string PAYMENT_ID_TO_TX_HASH_PREFIX = "f";
    const std::string TIMESTAMP_TO_BLOCKHASHES_PREFIX = "g";
    const std::string KEY_OUTPUT_AMOUNTS_COUNT_PREFIX = "h";
    const std::string KEY_OUTPUT_KEY_PREFIX = "j";
    const std::string PRUNE_FLOOR_PREFIX = "p";

    /* Maps tx hash → transaction public key (from tx extra).
       Stored at block-push time so it survives raw-block pruning. */
    const std::string TX_HASH_TO_PUBLIC_KEY_PREFIX = "k";

    /* Compact WalletBlockInfo record stored at block-push time (before any pruning).
       Contains everything a wallet needs to scan a block: tx public keys, output
       keys + amounts + global indexes, key images of inputs, and payment IDs.
       Never deleted by the prune pass — only BLOCK_INDEX_TO_RAW_BLOCK_PREFIX ("4")
       is pruned.  At ~200-500 bytes per block vs ~10-50 KB for raw blocks, this
       costs ~2% of the raw-block storage and survives forever. */
    const std::string BLOCK_INDEX_TO_WALLET_SYNC_PREFIX = "w";

    const std::string LAST_BLOCK_INDEX_KEY = "last_block_index";
    const std::string KEY_OUTPUT_AMOUNTS_COUNT_KEY = "key_amounts_count";
    const std::string TRANSACTIONS_COUNT_KEY = "txs_count";
    const std::string PRUNE_FLOOR_KEY = "prune_floor";

    template<class Value> std::string serialize(const Value &value, const std::string &name)
    {
        CryptoNote::KVBinaryOutputStreamSerializer serializer;
        std::stringstream ss;
        Common::StdOutputStream stream(ss);

        serializer(const_cast<Value &>(value), name);
        serializer.dump(stream);

        return ss.str();
    }

    std::string serialize(const RawBlock &value, const std::string &name);

    template<class Key, class Value>
    std::pair<std::string, std::string> serialize(const std::string &keyPrefix, const Key &key, const Value &value)
    {
        return {DB::serialize(std::make_pair(keyPrefix, key), keyPrefix), DB::serialize(value, keyPrefix)};
    }

    template<class Key> std::string serializeKey(const std::string &keyPrefix, const Key &key)
    {
        return DB::serialize(std::make_pair(keyPrefix, key), keyPrefix);
    }

    template<class Value> void deserialize(const std::string &serialized, Value &value, const std::string &name)
    {
        std::stringstream ss(serialized);
        Common::StdInputStream stream(ss);
        CryptoNote::KVBinaryInputStreamSerializer serializer(stream);
        serializer(value, name);
    }

    void deserialize(const std::string &serialized, RawBlock &value, const std::string &name);

    template<class Key, class Value>
    void serializeKeys(std::vector<std::string> &rawKeys,
                       const std::string keyPrefix,
                       const std::unordered_map<Key, Value> &map)
    {
        for (const std::pair<Key, Value> &kv : map)
        {
            rawKeys.emplace_back(DB::serializeKey(keyPrefix, kv.first));
        }
    }

    template<class Key, class Value, class Iterator>
    void deserializeValues(std::unordered_map<Key, Value> &map, Iterator &serializedValuesIter, const std::string &name)
    {
        for (auto iter = map.begin(); iter != map.end(); ++serializedValuesIter)
        {
            if (serializedValuesIter.found())
            {
                DB::deserialize(serializedValuesIter.value(), iter->second, name);
                ++iter;
            }
            else
            {
                iter = map.erase(iter);
            }
        }
    }

    template<class Value, class Iterator>
    void deserializeValue(std::pair<Value, bool> &pair, Iterator &serializedValuesIter, const std::string &name)
    {
        if (pair.second)
        {
            if (serializedValuesIter.found())
            {
                DB::deserialize(serializedValuesIter.value(), pair.first, name);
            }
            else
            {
                pair = {Value {}, false};
            }
            ++serializedValuesIter;
        }
    }
} // namespace CryptoNote::DB

