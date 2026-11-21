// Copyright (c) 2026, The DeroGold Developers
//
// Release compatibility tests for policy that must remain stable across
// wallet and daemon changes.

#include "config/CryptoNoteConfig.h"
#include "cryptonotecore/Currency.h"
#include "cryptonotecore/TransactionPoolPolicy.h"
#include "logging/ConsoleLogger.h"
#include "zedwallet++/SweepMath.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check(const bool condition, const std::string &description)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }

    Crypto::Hash hashWithFirstByte(const uint8_t value)
    {
        Crypto::Hash hash {};
        reinterpret_cast<uint8_t *>(&hash)[0] = value;
        return hash;
    }

    void testSweepMath()
    {
        using Sweep::calculateChunk;

        check(!calculateChunk(1000, 1000, 10, 1, 0, true).possible, "sweep rejects divider zero");
        check(!calculateChunk(10, 10, 10, 1, 1, true).possible, "sweep rejects fee-only balance");

        const auto exactSweep = calculateChunk(1000, 1000, 100, 900, 1, true);
        check(exactSweep.possible && exactSweep.amount == 900 && exactSweep.walletCost == 1000,
              "sweep deducts fee from the amount at the exact minimum");

        const auto partialSweep = calculateChunk(2000, 1000, 100, 1, 2, true);
        check(partialSweep.possible && partialSweep.amount == 400 && partialSweep.walletCost == 500,
              "sweep respects spendable balance and divider");

        const auto send = calculateChunk(900, 1000, 100, 1, 1, false);
        check(send.possible && send.amount == 900 && send.walletCost == 1000, "normal send pays fee on top");
        check(!calculateChunk(901, 1000, 100, 901, 1, false).possible,
              "normal send rejects an amount below minimum after reserving fee");
    }

    void testTransactionPoolPolicy()
    {
        const Crypto::Hash lowHash = hashWithFirstByte(1);
        const Crypto::Hash highHash = hashWithFirstByte(2);

        check(CryptoNote::TransactionPoolPolicy::hasHigherPriority(20, 200, highHash, 10, 100, lowHash),
              "absolute fee outranks receive time");
        check(!CryptoNote::TransactionPoolPolicy::hasHigherPriority(10, 100, lowHash, 20, 200, highHash),
              "priority comparison is asymmetric by fee");
        check(CryptoNote::TransactionPoolPolicy::hasHigherPriority(10, 100, highHash, 10, 200, lowHash),
              "older equal-fee transaction wins");
        check(CryptoNote::TransactionPoolPolicy::hasHigherPriority(10, 100, lowHash, 10, 100, highHash),
              "hash gives deterministic equal-fee/equal-time order");
        check(!CryptoNote::TransactionPoolPolicy::hasHigherPriority(10, 100, lowHash, 10, 100, lowHash),
              "priority comparison is irreflexive");

        check(!CryptoNote::TransactionPoolPolicy::isExpired(99, 100, 10), "future receive time is not expired");
        check(!CryptoNote::TransactionPoolPolicy::isExpired(109, 100, 10), "transaction lives before timeout");
        check(CryptoNote::TransactionPoolPolicy::isExpired(110, 100, 10), "transaction expires at timeout");
        check(CryptoNote::TransactionPoolPolicy::isRecentlyDeleted(99, 100, 10),
              "clock rollback keeps deletion quarantine");
        check(CryptoNote::TransactionPoolPolicy::isRecentlyDeleted(109, 100, 10),
              "deletion quarantine remains before timeout");
        check(!CryptoNote::TransactionPoolPolicy::isRecentlyDeleted(110, 100, 10),
              "deletion quarantine ends at timeout");
    }

    void testHistoricalFusionBoundaries()
    {
        auto logger = std::make_shared<Logging::ConsoleLogger>(Logging::FATAL);
        const CryptoNote::Currency currency = CryptoNote::CurrencyBuilder(logger).currency();
        const std::vector<uint64_t> inputs(12, 100000);
        const std::vector<uint64_t> noFeeOutputs {200000, 1000000};
        const std::vector<uint64_t> feeWindowOutputs {200000};
        const size_t smallTransaction = 1000;

        const uint32_t feeStart = CryptoNote::parameters::FUSION_FEE_V1_HEIGHT;
        const uint32_t feeEnd = CryptoNote::parameters::FUSION_FEE_V1_EXIT_HEIGHT;

        check(currency.isFusionTransaction(inputs, noFeeOutputs, smallTransaction, feeStart - 1),
              "historical fusion validates immediately before fee window");
        check(currency.isFusionTransaction(inputs, feeWindowOutputs, smallTransaction, feeStart),
              "historical fusion validates at fee-window start");
        check(currency.isFusionTransaction(inputs, feeWindowOutputs, smallTransaction, feeEnd - 1),
              "historical fusion validates at final fee-window block");
        check(currency.isFusionTransaction(inputs, noFeeOutputs, smallTransaction, feeEnd),
              "historical fusion validates at fee-window exit");
        check(!currency.isFusionTransaction(inputs, feeWindowOutputs, smallTransaction, feeEnd),
              "fee-window interpretation does not leak past exit height");
    }

    void testConfiguredPowMap()
    {
        using namespace CryptoNote::parameters;
        check(CryptoNote::HASHING_ALGORITHMS_BY_BLOCK_VERSION.size() == 7, "PoW map covers all seven block versions");

        const auto &input = Common::fromHex("0100fb8e8ac805899323371bb790db19218afd8db8e3755d8b90f39b3d5506a9abce4fa912244500000000ee8146d49fa93ee724deb57d12cbc6c6f3b924d946127c7a97418f9348828f0f02");
        const std::vector<std::pair<uint8_t, std::string>> expected = {
            {1, "1b606a3f4a07d6489a1bcd07697bd16696b61c8ae982f61a90160f4e52828a7f"},
            {2, "1b606a3f4a07d6489a1bcd07697bd16696b61c8ae982f61a90160f4e52828a7f"},
            {3, "1b606a3f4a07d6489a1bcd07697bd16696b61c8ae982f61a90160f4e52828a7f"},
            {4, "87c4e570653eb4c2b42b7a0d546559452dfab573b82ec52f152b7ff98e79446f"},
            {5, "b2172ec9466e1aee70ec8572a14c233ee354582bcb93f869d429744de5726a26"},
            {6, "b2172ec9466e1aee70ec8572a14c233ee354582bcb93f869d429744de5726a26"},
            {7, "38591572f820d4de253cf55a2192b622b0289e2e5c3616e61e787a8fe462ec5a"},
        };

        for (const auto &[version, expectedHex] : expected)
        {
            const auto algorithm = CryptoNote::HASHING_ALGORITHMS_BY_BLOCK_VERSION.find(version);
            check(algorithm != CryptoNote::HASHING_ALGORITHMS_BY_BLOCK_VERSION.end(), "PoW algorithm exists for version " + std::to_string(version));
            if (algorithm == CryptoNote::HASHING_ALGORITHMS_BY_BLOCK_VERSION.end())
            {
                continue;
            }
            Crypto::Hash actual {};
            algorithm->second(input.data(), input.size(), actual);
            check(Common::podToHex(actual) == expectedHex, "configured PoW vector for version " + std::to_string(version));
        }
    }
}

int main()
{
    testSweepMath();
    testTransactionPoolPolicy();
    testHistoricalFusionBoundaries();
    testConfiguredPowMap();

    if (failures != 0)
    {
        std::cerr << failures << " release compatibility test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All release compatibility tests passed\n";
    return EXIT_SUCCESS;
}
