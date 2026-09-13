// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

////////////////////////////////////////////////
#include <walletbackend/SynchronizationStatus.h>
////////////////////////////////////////////////

#include <walletbackend/Constants.h>

#include <algorithm>

/////////////////////
/* CLASS FUNCTIONS */
/////////////////////

uint64_t SynchronizationStatus::getHeight() const
{
    std::scoped_lock lock(m_mutex);

    return m_lastKnownBlockHeight;
}

void SynchronizationStatus::storeBlockHash(const Crypto::Hash hash, const uint64_t height)
{
    std::scoped_lock lock(m_mutex);

    m_lastKnownBlockHeight = height;

    /* Already added this hash. Newest is at the front, so this compares
       against the front - a synced wallet re-stores the top block about once a
       second, and each of those that got through evicted a real hash. */
    if (!m_lastKnownBlockHashes.empty() && m_lastKnownBlockHashes.front().hash == hash)
    {
        return;
    }

    const KnownBlock block {height, hash, true};

    /* If we're at a checkpoint height, add the hash to the infrequent
       checkpoints (at the beginning of the queue) */
    if (m_lastSavedCheckpointAt + Constants::BLOCK_HASH_CHECKPOINTS_INTERVAL < height)
    {
        m_lastSavedCheckpointAt = height;
        m_blockHashCheckpoints.push_front(block);

        /* Bounded, unlike before. Dropping the oldest costs reach the wallet
           could not use anyway: a reorg half a million blocks deep is not a
           thing that happens, and a wallet sharing no block at all with a
           daemon is now told so rather than left to guess. */
        while (m_blockHashCheckpoints.size() > Constants::BLOCK_HASH_CHECKPOINTS_MAX)
        {
            m_blockHashCheckpoints.pop_back();
        }
    }

    m_lastKnownBlockHashes.push_front(block);

    /* If we're exceeding capacity, remove the last (oldest) hash */
    while (m_lastKnownBlockHashes.size() > Constants::RECENT_BLOCK_HASHES_SIZE)
    {
        m_lastKnownBlockHashes.pop_back();
    }
}

void SynchronizationStatus::forgetBlocksFrom(const uint64_t height)
{
    std::scoped_lock lock(m_mutex);

    /* Entries loaded from a wallet written before heights were recorded cannot
       be placed, so they are left alone rather than guessed at. */
    const auto orphaned = [height](const KnownBlock &block) {
        return block.heightKnown && block.height >= height;
    };

    m_lastKnownBlockHashes.erase(
        std::remove_if(m_lastKnownBlockHashes.begin(), m_lastKnownBlockHashes.end(), orphaned),
        m_lastKnownBlockHashes.end());

    m_blockHashCheckpoints.erase(
        std::remove_if(m_blockHashCheckpoints.begin(), m_blockHashCheckpoints.end(), orphaned),
        m_blockHashCheckpoints.end());

    if (m_lastKnownBlockHeight >= height)
    {
        m_lastKnownBlockHeight = height == 0 ? 0 : height - 1;
    }

    /* So the next block at or past the interval is checkpointed again, rather
       than the marker sitting above the chain and suppressing them. */
    if (m_lastSavedCheckpointAt >= height)
    {
        m_lastSavedCheckpointAt = m_lastKnownBlockHeight;
    }
}

std::optional<Crypto::Hash> SynchronizationStatus::getHashAtHeight(const uint64_t height) const
{
    std::scoped_lock lock(m_mutex);

    for (const auto &block : m_lastKnownBlockHashes)
    {
        if (block.heightKnown && block.height == height)
        {
            return block.hash;
        }
    }

    for (const auto &block : m_blockHashCheckpoints)
    {
        if (block.heightKnown && block.height == height)
        {
            return block.hash;
        }
    }

    return std::nullopt;
}

std::vector<Crypto::Hash> SynchronizationStatus::getLocator() const
{
    std::scoped_lock lock(m_mutex);

    std::vector<Crypto::Hash> result;

    /* Dense over the newest blocks, then at doubling gaps. A reorg is nearly
       always a block or two, and naming those exactly is what decides whether
       the wallet resumes where it left off or falls back to a checkpoint
       thousands of blocks behind. Further back precision buys nothing while
       every entry costs request size, so the gaps grow.

       Two hundred blocks of history therefore travel as around twenty
       hashes. */
    size_t step = 1;
    size_t stepsAtThisSize = 0;

    for (size_t i = 0; i < m_lastKnownBlockHashes.size(); i += step)
    {
        result.push_back(m_lastKnownBlockHashes[i].hash);

        if (i + 1 >= Constants::LOCATOR_DENSE_COUNT)
        {
            stepsAtThisSize++;

            /* Two at each gap before doubling, so the thinning is gradual
               rather than jumping straight past most of the window. */
            if (stepsAtThisSize >= 2)
            {
                step *= 2;
                stepsAtThisSize = 0;
            }
        }
    }

    /* Then the deep ones, for a fork past everything above. */
    for (const auto &block : m_blockHashCheckpoints)
    {
        result.push_back(block.hash);
    }

    return result;
}

void SynchronizationStatus::fromJSON(const JSONObject &j)
{
    std::scoped_lock lock(m_mutex);

    m_lastKnownBlockHeight = getUint64FromJSON(j, "lastKnownBlockHeight");

    /* The hashes have always been stored; their heights have not. A wallet
       written before they were still opens, and its entries simply say they do
       not know their own height - good enough to offer a daemon as a resume
       point, which is all they were ever used for, and they age out as the
       wallet syncs on. */
    const auto readBlocks = [&j](const char *hashKey, const char *heightKey, std::deque<KnownBlock> &into) {
        std::vector<uint64_t> heights;

        if (hasMember(j, heightKey))
        {
            for (const auto &x : getArrayFromJSON(j, heightKey))
            {
                heights.push_back(x.GetUint64());
            }
        }

        size_t index = 0;

        for (const auto &x : getArrayFromJSON(j, hashKey))
        {
            KnownBlock block;

            block.hash.fromString(getStringFromJSONString(x));

            if (index < heights.size())
            {
                block.height = heights[index];
                block.heightKnown = true;
            }

            into.push_back(block);

            index++;
        }
    };

    readBlocks("blockHashCheckpoints", "blockHashCheckpointHeights", m_blockHashCheckpoints);
    readBlocks("lastKnownBlockHashes", "lastKnownBlockHashHeights", m_lastKnownBlockHashes);

    /* Trimmed here as well as on the way in. An existing wallet arrives
       carrying every checkpoint it ever made - hundreds of them - and storing
       one only happens once per checkpoint interval, so without this it would
       go on sending all of them for another five thousand blocks. Oldest go
       first: they are the least likely to be the resume point. */
    while (m_blockHashCheckpoints.size() > Constants::BLOCK_HASH_CHECKPOINTS_MAX)
    {
        m_blockHashCheckpoints.pop_back();
    }

    while (m_lastKnownBlockHashes.size() > Constants::RECENT_BLOCK_HASHES_SIZE)
    {
        m_lastKnownBlockHashes.pop_back();
    }

    /* Optional: wallets written before this field existed do not carry it.
       Without it the marker restarted at zero on every open, so the next block
       processed always looked like a checkpoint height and appended another
       entry to a list that is never trimmed. */
    if (hasMember(j, "lastSavedCheckpointAt"))
    {
        m_lastSavedCheckpointAt = getUint64FromJSON(j, "lastSavedCheckpointAt");
    }
    else
    {
        m_lastSavedCheckpointAt = m_lastKnownBlockHeight;
    }
}

void SynchronizationStatus::toJSON(rapidjson::Writer<rapidjson::StringBuffer> &writer) const
{
    std::scoped_lock lock(m_mutex);

    /* Hashes and heights go in side by side, in matching order, so a wallet
       written here still opens in a build that only knows about the hashes. */
    const auto writeBlocks =
        [&writer](const char *hashKey, const char *heightKey, const std::deque<KnownBlock> &blocks) {
            writer.Key(hashKey);
            writer.StartArray();
            for (const auto &block : blocks)
            {
                block.hash.toJSON(writer);
            }
            writer.EndArray();

            writer.Key(heightKey);
            writer.StartArray();
            for (const auto &block : blocks)
            {
                writer.Uint64(block.height);
            }
            writer.EndArray();
        };

    writer.StartObject();

    writeBlocks("blockHashCheckpoints", "blockHashCheckpointHeights", m_blockHashCheckpoints);
    writeBlocks("lastKnownBlockHashes", "lastKnownBlockHashHeights", m_lastKnownBlockHashes);

    writer.Key("lastKnownBlockHeight");
    writer.Uint64(m_lastKnownBlockHeight);

    writer.Key("lastSavedCheckpointAt");
    writer.Uint64(m_lastSavedCheckpointAt);

    writer.EndObject();
}
