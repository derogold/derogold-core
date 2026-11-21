// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

////////////////////////////////
#include <utilities/Utilities.h>
////////////////////////////////

#include <atomic>
#include <common/Base58.h>
#include <config/CryptoNoteConfig.h>
#include <thread>
#include <utilities/String.h>

namespace Utilities
{
    uint64_t getTransactionSum(const std::vector<std::pair<std::string, uint64_t>> destinations)
    {
        uint64_t amountSum = 0;

        for (const auto &[destination, amount] : destinations)
        {
            amountSum += amount;
        }

        return amountSum;
    }

    /* Round value to the nearest multiple (rounding down) */
    uint64_t getLowerBound(const uint64_t val, const uint64_t nearestMultiple)
    {
        uint64_t remainder = val % nearestMultiple;

        return val - remainder;
    }

    /* Round value to the nearest multiple (rounding down) */
    uint64_t getUpperBound(const uint64_t val, const uint64_t nearestMultiple)
    {
        return getLowerBound(val, nearestMultiple) + nearestMultiple;
    }

    bool isInputUnlocked(const uint64_t unlockTime, const uint64_t currentHeight)
    {
        /* Might as well return fast with the case that is true for nearly all
           transactions (excluding coinbase) */
        if (unlockTime == 0)
        {
            return true;
        }

        /* if unlockTime is greater than this amount, we treat it as a
           timestamp, otherwise we treat it as a block height */
        if (unlockTime >= CryptoNote::parameters::CRYPTONOTE_MAX_BLOCK_NUMBER)
        {
            const uint64_t currentTimeAdjusted = static_cast<uint64_t>(std::time(nullptr))
                                                 + CryptoNote::parameters::CRYPTONOTE_LOCKED_TX_ALLOWED_DELTA_SECONDS;

            return currentTimeAdjusted >= unlockTime;
        }

        const uint64_t currentHeightAdjusted =
            currentHeight + CryptoNote::parameters::CRYPTONOTE_LOCKED_TX_ALLOWED_DELTA_BLOCKS;

        return currentHeightAdjusted >= unlockTime;
    }

    /* The formula for the block size is as follows. Calculate the
       maxBlockCumulativeSize. This is equal to:
       100,000 + ((height * 102,400) / 1,051,200)
       At a block height of 400k, this gives us a size of 138,964.
       The constants this calculation arise from can be seen below, or in
       src/CryptoNoteCore/Currency.cpp::maxBlockCumulativeSize(). Call this value
       x.

       Next, calculate the median size of the last 100 blocks. Take the max of
       this value, and 100,000. Multiply this value by 1.25. Call this value y.

       Finally, return the minimum of x and y.

       Or, in short: min(140k (slowly rising), 1.25 * max(100k, median(last 100 blocks size)))
       Block size will always be 125k or greater (Assuming non testnet)

       To get the max transaction size, remove 600 from this value, for the
       reserved miner transaction.

       We are going to ignore the median(last 100 blocks size), as it is possible
       for a transaction to be valid for inclusion in a block when it is submitted,
       but not when it actually comes to be mined, for example if the median
       block size suddenly decreases. This gives a bit of a lower cap of max
       tx sizes, but prevents anything getting stuck in the pool.

    */
    uint64_t getMaxTxSize(const uint64_t currentHeight)
    {
        const uint64_t numerator = currentHeight * CryptoNote::parameters::MAX_BLOCK_SIZE_GROWTH_SPEED_NUMERATOR;
        const uint64_t denominator = CryptoNote::parameters::MAX_BLOCK_SIZE_GROWTH_SPEED_DENOMINATOR;

        const uint64_t growth = numerator / denominator;

        const uint64_t x = CryptoNote::parameters::MAX_BLOCK_SIZE_INITIAL + growth;

        const uint64_t y = 125000;

        /* Need space for the miner transaction */
        return std::min(x, y) - CryptoNote::parameters::CRYPTONOTE_COINBASE_BLOB_RESERVED_SIZE;
    }

    /* Sleep for approximately duration, unless condition is true. This lets us
       not bother the node too often, but makes shutdown times still quick. */
    void sleepUnlessStopping(const std::chrono::milliseconds duration, std::atomic<bool> &condition)
    {
        auto sleptFor = std::chrono::milliseconds::zero();

        /* 0.5 seconds max per iteration */
        const auto maxSleepDuration = std::chrono::milliseconds(500);

        while (!condition && sleptFor < duration)
        {
            const auto remaining = duration - sleptFor;
            const auto sleepDuration = std::min(remaining, maxSleepDuration);

            std::this_thread::sleep_for(sleepDuration);

            sleptFor += sleepDuration;
        }
    }

    /* Converts a height to a timestamp */
    uint64_t scanHeightToTimestamp(const uint64_t scanHeight)
    {
        if (scanHeight == 0)
        {
            return 0;
        }

        uint64_t secondsSinceLaunch = 0;

        /* How many seconds have passed since DIFFICULTY_TARGET_V3_HEIGHT */
        if (scanHeight >= CryptoNote::parameters::DIFFICULTY_TARGET_V3_HEIGHT)
        {
            /* How many blocks since the scan height with v3 diff */
            const uint64_t numBlocksWithV3Diff = scanHeight - CryptoNote::parameters::DIFFICULTY_TARGET_V3_HEIGHT;

            /* Number of seconds in this time period is just the number of blocks
             * multiplied by the average time per block */
            secondsSinceLaunch += numBlocksWithV3Diff * CryptoNote::parameters::DIFFICULTY_TARGET_V3;
        }

        /* How many seconds have passed between DIFFICULTY_TARGET_V2_HEIGHT and min(DIFFICULTY_TARGET_V3_HEIGHT, currentHeight) */
        if (scanHeight >= CryptoNote::parameters::DIFFICULTY_TARGET_V2_HEIGHT)
        {
            const uint64_t maxV2Block = std::min(scanHeight, CryptoNote::parameters::DIFFICULTY_TARGET_V3_HEIGHT - 1);

            /* How many blocks since the scan height with v2 diff */
            const uint64_t numBlocksWithV2Diff = maxV2Block - CryptoNote::parameters::DIFFICULTY_TARGET_V2_HEIGHT;

            secondsSinceLaunch += numBlocksWithV2Diff * CryptoNote::parameters::DIFFICULTY_TARGET_V2;
        }

        const uint64_t numBlocksWithV1Diff = std::min(scanHeight, CryptoNote::parameters::DIFFICULTY_TARGET_V2_HEIGHT - 1);

        /* How many seconds passed between start height and min(scanHeight, DIFFICULTY_TARGET_V2) */
        secondsSinceLaunch += numBlocksWithV1Diff * CryptoNote::parameters::DIFFICULTY_TARGET;
        
        /* Get the genesis block timestamp and add the time since launch */
        uint64_t timestamp = CryptoNote::parameters::GENESIS_BLOCK_TIMESTAMP + secondsSinceLaunch;

        /* Don't make timestamp too large or daemon throws an error */
        if (timestamp >= getCurrentTimestampAdjusted())
        {
            return getCurrentTimestampAdjusted();
        }

        return timestamp;
    }

    /* Converts a timestamp to a height. The inverse of scanHeightToTimestamp
       above, and it has to walk the same three eras to be one: dividing the
       whole span by DIFFICULTY_TARGET, as this did, values every block of the
       last two and a half million at ten seconds when the chain has been
       spending three hundred on each of them. In 2026 that answered roughly
       24,000,000 for a chain 2,900,000 blocks long, and a wallet told to start
       scanning there waits for a height that will not arrive for a century -
       it sits at the tip reporting itself fully synced and finds nothing. */
    uint64_t timestampToScanHeight(const uint64_t timestamp)
    {
        if (timestamp == 0)
        {
            return 0;
        }

        /* Timestamp is before the chain launched! */
        if (timestamp <= CryptoNote::parameters::GENESIS_BLOCK_TIMESTAMP)
        {
            return 0;
        }

        /* Find the amount of seconds between launch and the timestamp */
        uint64_t remaining = timestamp - CryptoNote::parameters::GENESIS_BLOCK_TIMESTAMP;

        uint64_t height = 0;

        /* Spend the span on each era in turn, at that era's block time, for as
           long as the era lasted. The block counts match the ones
           scanHeightToTimestamp charges for, so the two agree. */
        const uint64_t v1Blocks = CryptoNote::parameters::DIFFICULTY_TARGET_V2_HEIGHT - 1;
        const uint64_t v1Seconds = v1Blocks * CryptoNote::parameters::DIFFICULTY_TARGET;

        if (remaining < v1Seconds)
        {
            height = remaining / CryptoNote::parameters::DIFFICULTY_TARGET;
        }
        else
        {
            remaining -= v1Seconds;

            const uint64_t v2Blocks = CryptoNote::parameters::DIFFICULTY_TARGET_V3_HEIGHT - 1
                                      - CryptoNote::parameters::DIFFICULTY_TARGET_V2_HEIGHT;
            const uint64_t v2Seconds = v2Blocks * CryptoNote::parameters::DIFFICULTY_TARGET_V2;

            if (remaining < v2Seconds)
            {
                height = CryptoNote::parameters::DIFFICULTY_TARGET_V2_HEIGHT
                         + remaining / CryptoNote::parameters::DIFFICULTY_TARGET_V2;
            }
            else
            {
                remaining -= v2Seconds;

                height = CryptoNote::parameters::DIFFICULTY_TARGET_V3_HEIGHT
                         + remaining / CryptoNote::parameters::DIFFICULTY_TARGET_V3;
            }
        }

        /* Blocks do not arrive at exactly their target rate, so start earlier
           than the estimate says. Missing a transaction costs a wallet its
           balance; scanning too far back costs it time. */
        const uint64_t slack = 10000;

        return height > slack ? height - slack : 0;
    }

    uint64_t getCurrentTimestampAdjusted()
    {
        /* Get the current time as a unix timestamp */
        std::time_t time = std::time(nullptr);

        /* Take the amount of time a block can potentially be in the past/future */
        std::initializer_list<uint64_t> limits = {CryptoNote::parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT};

        /* Get the largest adjustment possible */
        uint64_t adjust = std::max(limits);

        /* Take the earliest timestamp that will include all possible blocks */
        return time - adjust;
    }

    bool parseDaemonAddressFromString(std::string &host, uint16_t &port, std::string address)
    {
        /* Lets users enter url's instead of host:port */
        address = Utilities::removePrefix(address, "https://");
        address = Utilities::removePrefix(address, "http://");

        std::vector<std::string> parts = Utilities::split(address, ':');

        if (parts.empty())
        {
            return false;
        }
        else if (parts.size() >= 2)
        {
            try
            {
                host = parts.at(0);
                port = std::stoi(parts.at(1));
                return true;
            }
            catch (const std::invalid_argument &)
            {
                return false;
            }
        }

        host = parts.at(0);
        port = CryptoNote::RPC_DEFAULT_PORT;

        return true;
    }

    size_t
        getApproximateMaximumInputCount(const size_t transactionSize, const size_t outputCount, const size_t mixinCount)
    {
        const size_t KEY_IMAGE_SIZE = sizeof(Crypto::KeyImage);
        const size_t OUTPUT_KEY_SIZE = sizeof(decltype(CryptoNote::KeyOutput::key));
        const size_t AMOUNT_SIZE = sizeof(uint64_t) + 2; // varint
        const size_t GLOBAL_INDEXES_VECTOR_SIZE_SIZE = sizeof(uint8_t); // varint
        const size_t GLOBAL_INDEXES_INITIAL_VALUE_SIZE = sizeof(uint32_t); // varint
        const size_t GLOBAL_INDEXES_DIFFERENCE_SIZE = sizeof(uint32_t); // varint
        const size_t SIGNATURE_SIZE = sizeof(Crypto::Signature);
        const size_t EXTRA_TAG_SIZE = sizeof(uint8_t);
        const size_t INPUT_TAG_SIZE = sizeof(uint8_t);
        const size_t OUTPUT_TAG_SIZE = sizeof(uint8_t);
        const size_t PUBLIC_KEY_SIZE = sizeof(Crypto::PublicKey);
        const size_t TRANSACTION_VERSION_SIZE = sizeof(uint8_t);
        const size_t TRANSACTION_UNLOCK_TIME_SIZE = sizeof(uint64_t);

        const size_t outputsSize = outputCount * (OUTPUT_TAG_SIZE + OUTPUT_KEY_SIZE + AMOUNT_SIZE);
        const size_t headerSize =
            TRANSACTION_VERSION_SIZE + TRANSACTION_UNLOCK_TIME_SIZE + EXTRA_TAG_SIZE + PUBLIC_KEY_SIZE;
        const size_t inputSize = INPUT_TAG_SIZE + AMOUNT_SIZE + KEY_IMAGE_SIZE + SIGNATURE_SIZE
                                 + GLOBAL_INDEXES_VECTOR_SIZE_SIZE + GLOBAL_INDEXES_INITIAL_VALUE_SIZE
                                 + mixinCount * (GLOBAL_INDEXES_DIFFERENCE_SIZE + SIGNATURE_SIZE);

        return (transactionSize - headerSize - outputsSize) / inputSize;
    }

} // namespace Utilities
