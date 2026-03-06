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

#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <sstream>
#include <string>

namespace CryptoNote::DB
{
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
            if (boost::get<1>(*serializedValuesIter))
            {
                DB::deserialize(boost::get<0>(*serializedValuesIter), iter->second, name);
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
            if (boost::get<1>(*serializedValuesIter))
            {
                DB::deserialize(boost::get<0>(*serializedValuesIter), pair.first, name);
            }
            else
            {
                pair = {Value {}, false};
            }
            ++serializedValuesIter;
        }
    }

    namespace V2
    {
        const std::string RAW_BLOCKS_CF = "RawBlocks";
        const std::string SPENT_KEY_IMAGES_CF = "SpentKeyImages";
        const std::string PAYMENT_ID_TXS_CF = "PaymentIdTxs";
        const std::string TRANSACTIONS_CF = "Transactions";
        const std::string BLOCKS_CF = "Blocks";

        constexpr uint32_t SERIALIZATION_FLAGS =
            boost::archive::archive_flags::no_header | boost::archive::archive_flags::no_tracking;

        template<class Key> std::string serializeKey(const Key &key)
        {
            std::stringstream ss;
            ss << key;
            return ss.str();
        }

        template<class Key> std::string serializeKey(const std::string &keyPrefix, const Key &key)
        {
            std::stringstream ss;
            ss << keyPrefix << "_" << key;
            return ss.str();
        }

        template<class Value> std::string serializeValue(const Value &value)
        {
            std::stringstream ss;

            {
                boost::archive::binary_oarchive o {ss, SERIALIZATION_FLAGS};
                o << value;
            }

            return ss.str();
        }

        template<class Value> void deserializeValue(const std::string &data, Value &value)
        {
            std::stringstream ss;
            ss << data;

            boost::archive::binary_iarchive i {ss, SERIALIZATION_FLAGS};
            i >> value;
        }

        template<class Key, class Value>
        std::pair<std::string, std::string> serialize(const Key &key, const Value &value)
        {
            return std::make_pair(V2::serializeKey(key), V2::serializeValue(value));
        }

        template<class Key, class Value>
        std::pair<std::string, std::string> serialize(const std::string &keyPrefix, const Key &key, const Value &value)
        {
            return std::make_pair(V2::serializeKey(keyPrefix, key), V2::serializeValue(value));
        }

        template<class Key, class Value>
        void serializeKeys(std::vector<std::string> &rawKeys,
                           const std::string keyPrefix,
                           const std::unordered_map<Key, Value> &map)
        {
            for (const std::pair<Key, Value> &kv : map)
            {
                rawKeys.emplace_back(V2::serializeKey(keyPrefix, kv.first));
            }
        }

        template<class Key, class Value, class Iterator>
        void deserializeValues(std::unordered_map<Key, Value> &map, Iterator &serializedValuesIter)
        {
            for (auto iter = map.begin(); iter != map.end(); ++serializedValuesIter)
            {
                if (boost::get<1>(*serializedValuesIter))
                {
                    V2::deserializeValue(boost::get<0>(*serializedValuesIter), iter->second);
                    ++iter;
                }
                else
                {
                    iter = map.erase(iter);
                }
            }
        }

        template<class Value, class Iterator>
        void deserializeValue(std::pair<Value, bool> &pair, Iterator &serializedValuesIter)
        {
            if (pair.second)
            {
                if (boost::get<1>(*serializedValuesIter))
                {
                    V2::deserializeValue(boost::get<0>(*serializedValuesIter), pair.first);
                }
                else
                {
                    pair = {Value {}, false};
                }
                ++serializedValuesIter;
            }
        }
    } // namespace V2
} // namespace CryptoNote::DB

