// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "CryptoNote.h"
#include "CryptoTypes.h"
#include "common/StringTools.h"
#include "crypto/crypto.h"
#include "snapshot/SnapshotCodec.h"

#include <assert.h>
#include <chrono>
#include <config/CliHeader.h>
#include <cxxopts.hpp>
#include <functional>
#include <iostream>

#define PERFORMANCE_ITERATIONS 1000
#define PERFORMANCE_ITERATIONS_LONG_MULTIPLIER 10

using namespace Crypto;
using namespace CryptoNote;

const std::string INPUT_DATA = "0100fb8e8ac805899323371bb790db19218afd8db8e3755d8b90f39b3d5506a9abce4fa912244500000000e"
                               "e8146d49fa93ee724deb57d12cbc6c6f3b924d946127c7a97418f9348828f0f02";

const std::string CN_FAST_HASH = "b542df5b6e7f5f05275c98e7345884e2ac726aeeb07e03e44e0389eb86cd05f0";

const std::string CN_SLOW_HASH_V0 = "1b606a3f4a07d6489a1bcd07697bd16696b61c8ae982f61a90160f4e52828a7f";

const std::string CN_SLOW_HASH_V1 = "c9fae8425d8688dc236bcdbc42fdb42d376c6ec190501aa84b04a4b4cf1ee122";

const std::string CN_SLOW_HASH_V2 = "871fcd6823f6a879bb3f33951c8e8e891d4043880b02dfa1bb3be498b50e7578";

const std::string CN_LITE_SLOW_HASH_V0 = "28a22bad3f93d1408fca472eb5ad1cbe75f21d053c8ce5b3af105a57713e21dd";

const std::string CN_LITE_SLOW_HASH_V1 = "87c4e570653eb4c2b42b7a0d546559452dfab573b82ec52f152b7ff98e79446f";

const std::string CN_LITE_SLOW_HASH_V2 = "b7e78fab22eb19cb8c9c3afe034fb53390321511bab6ab4915cd538a630c3c62";

const std::string CN_DARK_SLOW_HASH_V0 = "bea42eadd78614f875e55bb972aa5ec54a5edf2dd7068220fda26bf4b1080fb8";

const std::string CN_DARK_SLOW_HASH_V1 = "d18cb32bd5b465e5a7ba4763d60f88b5792f24e513306f1052954294b737e871";

const std::string CN_DARK_SLOW_HASH_V2 = "a18a14d94efea108757a42633a1b4d4dc11838084c3c4347850d39ab5211a91f";

const std::string CN_DARK_LITE_SLOW_HASH_V0 = "faa7884d9c08126eb164814aeba6547b5d6064277a09fb6b414f5dbc9d01eb2b";

const std::string CN_DARK_LITE_SLOW_HASH_V1 = "c75c010780fffd9d5e99838eb093b37c0dd015101c9d298217866daa2993d277";

const std::string CN_DARK_LITE_SLOW_HASH_V2 = "fdceb794c1055977a955f31c576a8be528a0356ee1b0a1f9b7f09e20185cda28";

const std::string CN_TURTLE_SLOW_HASH_V0 = "546c3f1badd7c1232c7a3b88cdb013f7f611b7bd3d1d2463540fccbd12997982";

const std::string CN_TURTLE_SLOW_HASH_V1 = "29e7831780a0ab930e0fe3b965f30e8a44d9b3f9ad2241d67cfbfea3ed62a64e";

const std::string CN_TURTLE_SLOW_HASH_V2 = "fc67dfccb5fc90d7855ae903361eabd76f1e40a22a72ad3ef2d6ad27b5a60ce5";

const std::string CN_TURTLE_LITE_SLOW_HASH_V0 = "5e1891a15d5d85c09baf4a3bbe33675cfa3f77229c8ad66c01779e590528d6d3";

const std::string CN_TURTLE_LITE_SLOW_HASH_V1 = "ae7f864a7a2f2b07dcef253581e60a014972b9655a152341cb989164761c180a";

const std::string CN_TURTLE_LITE_SLOW_HASH_V2 = "b2172ec9466e1aee70ec8572a14c233ee354582bcb93f869d429744de5726a26";

const std::string CN_UPX = "38591572f820d4de253cf55a2192b622b0289e2e5c3616e61e787a8fe462ec5a";

const std::string CN_SOFT_SHELL_V0[] = {"5e1891a15d5d85c09baf4a3bbe33675cfa3f77229c8ad66c01779e590528d6d3",
                                        "e1239347694df77cab780b7ec8920ec6f7e48ecef1d8c368e06708c08e1455f1",
                                        "118a03801c564d12f7e68972419303fe06f7a54ab8f44a8ce7deafbc6b1b5183",
                                        "8be48f7955eb3f9ac2275e445fe553f3ef359ea5c065cde98ff83011f407a0ec",
                                        "d33da3541960046e846530dcc9872b1914a62c09c7d732bff03bec481866ae48",
                                        "8be48f7955eb3f9ac2275e445fe553f3ef359ea5c065cde98ff83011f407a0ec",
                                        "118a03801c564d12f7e68972419303fe06f7a54ab8f44a8ce7deafbc6b1b5183",
                                        "e1239347694df77cab780b7ec8920ec6f7e48ecef1d8c368e06708c08e1455f1",
                                        "5e1891a15d5d85c09baf4a3bbe33675cfa3f77229c8ad66c01779e590528d6d3",
                                        "e1239347694df77cab780b7ec8920ec6f7e48ecef1d8c368e06708c08e1455f1",
                                        "118a03801c564d12f7e68972419303fe06f7a54ab8f44a8ce7deafbc6b1b5183",
                                        "8be48f7955eb3f9ac2275e445fe553f3ef359ea5c065cde98ff83011f407a0ec",
                                        "d33da3541960046e846530dcc9872b1914a62c09c7d732bff03bec481866ae48",
                                        "8be48f7955eb3f9ac2275e445fe553f3ef359ea5c065cde98ff83011f407a0ec",
                                        "118a03801c564d12f7e68972419303fe06f7a54ab8f44a8ce7deafbc6b1b5183",
                                        "e1239347694df77cab780b7ec8920ec6f7e48ecef1d8c368e06708c08e1455f1",
                                        "5e1891a15d5d85c09baf4a3bbe33675cfa3f77229c8ad66c01779e590528d6d3"};

const std::string CN_SOFT_SHELL_V1[] = {"ae7f864a7a2f2b07dcef253581e60a014972b9655a152341cb989164761c180a",
                                        "ce8687bdd08c49bd1da3a6a74bf28858670232c1a0173ceb2466655250f9c56d",
                                        "ddb6011d400ac8725995fb800af11646bb2fef0d8b6136b634368ad28272d7f4",
                                        "02576f9873dc9c8b1b0fc14962982734dfdd41630fc936137a3562b8841237e1",
                                        "d37e2785ab7b3d0a222940bf675248e7b96054de5c82c5f0b141014e136eadbc",
                                        "02576f9873dc9c8b1b0fc14962982734dfdd41630fc936137a3562b8841237e1",
                                        "ddb6011d400ac8725995fb800af11646bb2fef0d8b6136b634368ad28272d7f4",
                                        "ce8687bdd08c49bd1da3a6a74bf28858670232c1a0173ceb2466655250f9c56d",
                                        "ae7f864a7a2f2b07dcef253581e60a014972b9655a152341cb989164761c180a",
                                        "ce8687bdd08c49bd1da3a6a74bf28858670232c1a0173ceb2466655250f9c56d",
                                        "ddb6011d400ac8725995fb800af11646bb2fef0d8b6136b634368ad28272d7f4",
                                        "02576f9873dc9c8b1b0fc14962982734dfdd41630fc936137a3562b8841237e1",
                                        "d37e2785ab7b3d0a222940bf675248e7b96054de5c82c5f0b141014e136eadbc",
                                        "02576f9873dc9c8b1b0fc14962982734dfdd41630fc936137a3562b8841237e1",
                                        "ddb6011d400ac8725995fb800af11646bb2fef0d8b6136b634368ad28272d7f4",
                                        "ce8687bdd08c49bd1da3a6a74bf28858670232c1a0173ceb2466655250f9c56d",
                                        "ae7f864a7a2f2b07dcef253581e60a014972b9655a152341cb989164761c180a"};

const std::string CN_SOFT_SHELL_V2[] = {"b2172ec9466e1aee70ec8572a14c233ee354582bcb93f869d429744de5726a26",
                                        "b2623a2b041dc5ae3132b964b75e193558c7095e725d882a3946aae172179cf1",
                                        "141878a7b58b0f57d00b8fc2183cce3517d9d68becab6fee52abb3c1c7d0805b",
                                        "4646f9919791c28f0915bc0005ed619bee31d42359f7a8af5de5e1807e875364",
                                        "3fedc7ab0f8d14122fc26062de1af7a6165755fcecdf0f12fa3ccb3ff63629d0",
                                        "4646f9919791c28f0915bc0005ed619bee31d42359f7a8af5de5e1807e875364",
                                        "141878a7b58b0f57d00b8fc2183cce3517d9d68becab6fee52abb3c1c7d0805b",
                                        "b2623a2b041dc5ae3132b964b75e193558c7095e725d882a3946aae172179cf1",
                                        "b2172ec9466e1aee70ec8572a14c233ee354582bcb93f869d429744de5726a26",
                                        "b2623a2b041dc5ae3132b964b75e193558c7095e725d882a3946aae172179cf1",
                                        "141878a7b58b0f57d00b8fc2183cce3517d9d68becab6fee52abb3c1c7d0805b",
                                        "4646f9919791c28f0915bc0005ed619bee31d42359f7a8af5de5e1807e875364",
                                        "3fedc7ab0f8d14122fc26062de1af7a6165755fcecdf0f12fa3ccb3ff63629d0",
                                        "4646f9919791c28f0915bc0005ed619bee31d42359f7a8af5de5e1807e875364",
                                        "141878a7b58b0f57d00b8fc2183cce3517d9d68becab6fee52abb3c1c7d0805b",
                                        "b2623a2b041dc5ae3132b964b75e193558c7095e725d882a3946aae172179cf1",
                                        "b2172ec9466e1aee70ec8572a14c233ee354582bcb93f869d429744de5726a26"};

static inline bool CompareHashes(const Hash leftHash, const std::string right)
{
    Hash rightHash = Hash();
    if (!Common::podFromHex(right, rightHash))
    {
        return false;
    }

    return (leftHash == rightHash);
}

/* Hacky way to check if we're testing a v1 hash and thus should skip data
   < 43 bytes */
bool need43BytesOfData(std::string hashFunctionName)
{
    return hashFunctionName.find("v1") != std::string::npos;
}

/* Bit of hackery so we can get the variable name of the passed in function.
   This way we can print the test we are currently performing. */
#define TEST_HASH_FUNCTION(hashFunction, expectedOutput) \
    testHashFunction(hashFunction, expectedOutput, #hashFunction, -1)

#define TEST_HASH_FUNCTION_WITH_HEIGHT(hashFunction, expectedOutput, height) \
    testHashFunction(hashFunction, expectedOutput, #hashFunction, height, height)

template<typename T, typename... Args>
void testHashFunction(
    T hashFunction,
    std::string expectedOutput,
    std::string hashFunctionName,
    int64_t height,
    Args &&... args)
{
    const BinaryArray &rawData = Common::fromHex(INPUT_DATA);

    if (need43BytesOfData(hashFunctionName) && rawData.size() < 43)
    {
        return;
    }

    Hash hash = Hash();

    /* Perform the hash, with a height if given */
    hashFunction(rawData.data(), rawData.size(), hash, std::forward<Args>(args)...);

    if (height == -1)
    {
        std::cout << hashFunctionName << ": " << hash << std::endl;
    }
    else
    {
        std::cout << hashFunctionName << " (" << height << "): " << hash << std::endl;
    }

    /* Verify the hash is as expected */
    if (!CompareHashes(hash, expectedOutput))
    {
        std::cout << "Hashes are not equal!\n"
                  << "Expected: " << expectedOutput << "\nActual: " << hash << "\nTerminating.";

        exit(1);
    }
}

/* Bit of hackery so we can get the variable name of the passed in function.
   This way we can print the test we are currently performing. */
#define BENCHMARK(hashFunction, iterations) benchmark(hashFunction, #hashFunction, iterations)

template<typename T> void benchmark(T hashFunction, std::string hashFunctionName, uint64_t iterations)
{
    const BinaryArray &rawData = Common::fromHex(INPUT_DATA);

    if (need43BytesOfData(hashFunctionName) && rawData.size() < 43)
    {
        return;
    }

    Hash hash = Hash();

    auto startTimer = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < iterations; i++)
    {
        hashFunction(rawData.data(), rawData.size(), hash);
    }

    auto elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;

    std::cout << hashFunctionName << ": "
              << (iterations / std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count()) << " H/s\n";
}

void benchmarkUnderivePublicKey()
{
    Crypto::KeyDerivation derivation;

    Crypto::PublicKey txPublicKey;
    Common::podFromHex("f235acd76ee38ec4f7d95123436200f9ed74f9eb291b1454fbc30742481be1ab", txPublicKey);

    Crypto::SecretKey privateViewKey;
    Common::podFromHex("89df8c4d34af41a51cfae0267e8254cadd2298f9256439fa1cfa7e25ee606606", privateViewKey);

    Crypto::generate_key_derivation(txPublicKey, privateViewKey, derivation);

    const uint64_t loopIterations = 600000;

    auto startTimer = std::chrono::high_resolution_clock::now();

    Crypto::PublicKey spendKey;

    Crypto::PublicKey outputKey;
    Common::podFromHex("4a078e76cd41a3d3b534b83dc6f2ea2de500b653ca82273b7bfad8045d85a400", outputKey);

    for (uint64_t i = 0; i < loopIterations; i++)
    {
        /* Use i as output index to prevent optimization */
        Crypto::underive_public_key(derivation, i, outputKey, spendKey);
    }

    auto elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;

    /* Need to use microseconds here then divide by 1000 - otherwise we'll just get '0' */
    const auto timePerDerivation =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsedTime).count() / loopIterations;

    std::cout << "Time to perform underivePublicKey: " << timePerDerivation / 1000.0 << " ms" << std::endl;
}

void benchmarkGenerateKeyDerivation()
{
    Crypto::KeyDerivation derivation;

    Crypto::PublicKey txPublicKey;
    Common::podFromHex("f235acd76ee38ec4f7d95123436200f9ed74f9eb291b1454fbc30742481be1ab", txPublicKey);

    Crypto::SecretKey privateViewKey;
    Common::podFromHex("89df8c4d34af41a51cfae0267e8254cadd2298f9256439fa1cfa7e25ee606606", privateViewKey);

    const uint64_t loopIterations = 60000;

    auto startTimer = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < loopIterations; i++)
    {
        Crypto::generate_key_derivation(txPublicKey, privateViewKey, derivation);
    }

    auto elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;

    const auto timePerDerivation =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsedTime).count() / loopIterations;

    std::cout << "Time to perform generateKeyDerivation: " << timePerDerivation / 1000.0 << " ms" << std::endl;
}

template<typename Pod> Pod podFromSequentialBytes(uint8_t first)
{
    Pod pod {};
    auto *bytes = reinterpret_cast<uint8_t *>(&pod);
    for (size_t i = 0; i < sizeof(Pod); ++i)
    {
        bytes[i] = static_cast<uint8_t>(first + i);
    }
    return pod;
}

template<typename Pod> Pod podFromHexOrExit(const std::string &hex)
{
    Pod pod {};
    if (!Common::podFromHex(hex, pod))
    {
        std::cout << "Failed to parse test hex: " << hex << std::endl;
        exit(1);
    }
    return pod;
}

void requireEqual(const std::string &label, const std::string &actual, const std::string &expected)
{
    if (actual != expected)
    {
        std::cout << label << " mismatch\nExpected: " << expected << "\nActual:   " << actual << std::endl;
        exit(1);
    }
}

void requireThrows(const std::string &label, const std::function<void()> &fn)
{
    try
    {
        fn();
    }
    catch (const CryptoNote::Snapshot::FormatError &)
    {
        return;
    }

    std::cout << label << " did not fail closed" << std::endl;
    exit(1);
}

void runSnapshotCodecTests()
{
    using namespace CryptoNote::Snapshot;

    const std::string expectedOutputHex =
        "44474f55543030312a0000000000000007000000101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e2f404142434445464748494a4b4c4d4e4f505152"
        "535455565758595a5b5c5d5e5f02005b33290000000000";
    const std::string expectedOutputSha =
        "806ecad704f3df2f2248c54d109d5b23da9ce3ecbf2c938dd775b8ea98412b8f";

    OutputRecord output;
    output.amount = 42;
    output.globalIndex = 7;
    output.publicKey = podFromSequentialBytes<Crypto::PublicKey>(0x10);
    output.transactionHash = podFromSequentialBytes<Crypto::Hash>(0x40);
    output.transactionOutputIndex = 2;
    output.unlockTime = 2700123;

    const Bytes outputBytes = encodeOutputRecord(output);
    requireEqual("snapshot output bytes", Common::toHex(outputBytes), expectedOutputHex);
    requireEqual("snapshot output sha256", hashToHex(sha256(outputBytes)), expectedOutputSha);
    const OutputRecord parsedOutput = parseOutputRecord(outputBytes);
    requireEqual("snapshot output round trip", Common::toHex(encodeOutputRecord(parsedOutput)), expectedOutputHex);

    const std::string expectedKeyImageHex =
        "44474b4930303031a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7"
        "b8b9babbbcbdbebfe0322900";
    const std::string expectedKeyImageSha =
        "c01cada63061278450bd9772c8a03a379c9400c1d41def1a5a8b2113b8ea6e10";

    KeyImageRecord keyImage;
    keyImage.keyImage = podFromSequentialBytes<Crypto::KeyImage>(0xa0);
    keyImage.spendingBlockHeight = 2700000;

    const Bytes keyImageBytes = encodeKeyImageRecord(keyImage);
    requireEqual("snapshot key-image bytes", Common::toHex(keyImageBytes), expectedKeyImageHex);
    requireEqual("snapshot key-image sha256", hashToHex(sha256(keyImageBytes)), expectedKeyImageSha);
    const KeyImageRecord parsedKeyImage = parseKeyImageRecord(keyImageBytes);
    requireEqual("snapshot key-image round trip", Common::toHex(encodeKeyImageRecord(parsedKeyImage)), expectedKeyImageHex);

    const std::string expectedConsensusHex =
        "4447435730303031e03229000200f062796500000000d76379650000000002005381bd"
        "e3c4510000ef872ce4c451000002006e000000000000007800000000000000010203"
        "0405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
    const std::string expectedConsensusSha =
        "16cd60bfd9e297ef809d14b82a6ce45e46d32a1bff931946b9e193925fceb742";

    ConsensusWindowRecord consensus;
    consensus.anchorHeight = 2700000;
    consensus.timestamps = {1702454000, 1702454231};
    consensus.cumulativeDifficulties = {89906076287315, 89906083563503};
    consensus.blockSizes = {110, 120};
    consensus.anchorRawBlockHash = podFromSequentialBytes<Crypto::Hash>(0x01);

    const Bytes consensusBytes = encodeConsensusWindowRecord(consensus);
    requireEqual("snapshot consensus bytes", Common::toHex(consensusBytes), expectedConsensusHex);
    requireEqual("snapshot consensus sha256", hashToHex(sha256(consensusBytes)), expectedConsensusSha);
    const ConsensusWindowRecord parsedConsensus = parseConsensusWindowRecord(consensusBytes);
    requireEqual(
        "snapshot consensus round trip",
        Common::toHex(encodeConsensusWindowRecord(parsedConsensus)),
        expectedConsensusHex);

    const std::string expectedRoot =
        "69e2b73e99a06f0ccf1c5a93756354befbd03f0c5a1af5dd6ad417b43fcdd3f0";
    const std::string expectedManifestSha =
        "c6adf5775b8867fb2c016af799aac0b9a2e6604a773300974c2e0715e3108383";
    const std::string expectedManifestHex =
        "4447534e41504d310100010007006d61696e6e6574202122232425262728292a2b2c"
        "2d2e2f303132333435363738393a3b3c3d3e3fe0322900088438ac255023638252"
        "a926c0ff4177a90224a90453d5764902d1f844e3394c55565758595a5b5c5d5e"
        "5f606162636465666768696a6b6c6d6e6f7071727374d7637965000000000400ef"
        "872ce4c4510000fe8d1299bb7f06004d09dd0100000000100030313233343536"
        "373839616263646566030000000100000000000000010000000000000001000000"
        "00000000030000000000000001000000005e000000000000006900000000000000"
        "806ecad704f3df2f2248c54d109d5b23da9ce3ecbf2c938dd775b8ea98412b8f"
        "fc0d24384c1c3e0af8333804e614f6009a5171a2c75bddb02b117983cb2cd2da"
        "1d006368756e6b732f6f7574707574732d3030303030302e62696e2e7a737402"
        "000000002c000000000000003700000000000000c01cada63061278450bd9772c8"
        "a03a379c9400c1d41def1a5a8b2113b8ea6e10ba41478c9557eee279b4a2ba0d"
        "f1e0e21e94881315a877706df43bb70ae3d14720006368756e6b732f6b65792d"
        "696d616765732d3030303030302e62696e2e7a73740300000000620000000000"
        "00006d0000000000000016cd60bfd9e297ef809d14b82a6ce45e46d32a1bff93"
        "1946b9e193925fceb742bbf1c81a28da74d61131849b3bf9eba3caa31214a74c"
        "b32b5282fdd14fd20e611f006368756e6b732f636f6e73656e7375732d303030"
        "3030302e62696e2e7a737469e2b73e99a06f0ccf1c5a93756354befbd03f0c"
        "5a1af5dd6ad417b43fcdd3f0";

    Manifest manifest;
    manifest.network = "mainnet";
    manifest.genesisHash = podFromSequentialBytes<Crypto::Hash>(0x20);
    manifest.anchorHeight = 2700000;
    manifest.anchorHash = podFromHexOrExit<Crypto::Hash>(
        "088438ac255023638252a926c0ff4177a90224a90453d5764902d1f844e3394c");
    manifest.anchorPreviousHash = podFromSequentialBytes<Crypto::Hash>(0x55);
    manifest.anchorTimestamp = 1702454231;
    manifest.anchorMajorVersion = 4;
    manifest.anchorMinorVersion = 0;
    manifest.cumulativeDifficulty = 89906083563503;
    manifest.alreadyGeneratedCoins = 1829293564005886;
    manifest.alreadyGeneratedTransactions = 31263053;
    manifest.sourceCommit = "0123456789abcdef";
    manifest.sourceDbSchemaVersion = 3;
    manifest.outputCount = 1;
    manifest.keyImageCount = 1;
    manifest.consensusRecordCount = 1;
    manifest.chunks = {
        {ChunkType::OutputRecords,
         0,
         outputBytes.size(),
         outputBytes.size() + 11,
         sha256(outputBytes),
         podFromHexOrExit<Crypto::Hash>("fc0d24384c1c3e0af8333804e614f6009a5171a2c75bddb02b117983cb2cd2da"),
         "chunks/outputs-000000.bin.zst"},
        {ChunkType::KeyImageRecords,
         0,
         keyImageBytes.size(),
         keyImageBytes.size() + 11,
         sha256(keyImageBytes),
         podFromHexOrExit<Crypto::Hash>("ba41478c9557eee279b4a2ba0df1e0e21e94881315a877706df43bb70ae3d147"),
         "chunks/key-images-000000.bin.zst"},
        {ChunkType::ConsensusWindowRecords,
         0,
         consensusBytes.size(),
         consensusBytes.size() + 11,
         sha256(consensusBytes),
         podFromHexOrExit<Crypto::Hash>("bbf1c81a28da74d61131849b3bf9eba3caa31214a74cb32b5282fdd14fd20e61"),
         "chunks/consensus-000000.bin.zst"}};

    manifest.logicalStateRoot = logicalStateRoot(manifest, {output}, {keyImage}, {consensus});
    requireEqual("snapshot logical root", hashToHex(manifest.logicalStateRoot), expectedRoot);
    requireEqual("snapshot logical root repeat", hashToHex(logicalStateRoot(manifest, {output}, {keyImage}, {consensus})), expectedRoot);

    const Bytes manifestBytes = encodeManifest(manifest);
    requireEqual("snapshot manifest bytes", Common::toHex(manifestBytes), expectedManifestHex);
    requireEqual("snapshot manifest sha256", hashToHex(sha256(manifestBytes)), expectedManifestSha);
    const Manifest parsedManifest = parseManifest(manifestBytes);
    requireEqual("snapshot manifest round trip", Common::toHex(encodeManifest(parsedManifest)), expectedManifestHex);

    Bytes truncatedOutput = outputBytes;
    truncatedOutput.pop_back();
    requireThrows("snapshot truncated output", [&] { parseOutputRecord(truncatedOutput); });

    Bytes unsupportedManifest = manifestBytes;
    unsupportedManifest[8] = 2;
    requireThrows("snapshot unsupported manifest version", [&] { parseManifest(unsupportedManifest); });

    Limits strictLimits;
    strictLimits.maxOutputs = 0;
    requireThrows("snapshot manifest count limit", [&] { parseManifest(manifestBytes, strictLimits); });

    auto duplicateChunks = manifest.chunks;
    duplicateChunks[1].type = ChunkType::OutputRecords;
    duplicateChunks[1].index = 0;
    requireThrows("snapshot duplicate chunks", [&] { validateCanonicalChunks(duplicateChunks); });

    auto pathTraversalChunks = manifest.chunks;
    pathTraversalChunks[0].path = "../outputs.bin.zst";
    requireThrows("snapshot path traversal", [&] { validateCanonicalChunks(pathTraversalChunks); });

    auto unsortedOutputs = std::vector<OutputRecord> {output, output};
    unsortedOutputs[0].globalIndex = 8;
    unsortedOutputs[1].globalIndex = 7;
    requireThrows("snapshot unsorted outputs", [&] { validateCanonicalOutputs(unsortedOutputs); });

    auto duplicateKeyImages = std::vector<KeyImageRecord> {keyImage, keyImage};
    requireThrows("snapshot duplicate key images", [&] { validateCanonicalKeyImages(duplicateKeyImages); });

    auto duplicateConsensus = std::vector<ConsensusWindowRecord> {consensus, consensus};
    requireThrows("snapshot duplicate consensus windows", [&] { validateCanonicalConsensusWindows(duplicateConsensus); });

    std::cout << "Snapshot codec deterministic tests passed" << std::endl << std::endl;
}

int main(int argc, char **argv)
{
    bool o_help = false, o_version = false, o_benchmark = false;
    int o_iterations = PERFORMANCE_ITERATIONS;

    cxxopts::Options options(argv[0], getProjectCLIHeader());

    options.add_options("Core")(
        "h,help", "Display this help message", cxxopts::value<bool>(o_help)->implicit_value("true"))(
        "v,version",
        "Output software version information",
        cxxopts::value<bool>(o_version)->default_value("false")->implicit_value("true"));

    options.add_options("Performance Testing")(
        "b,benchmark",
        "Run quick performance benchmark",
        cxxopts::value<bool>(o_benchmark)->default_value("false")->implicit_value("true"))(
        "i,iterations",
        "The number of iterations for the benchmark test. Minimum of 1,000 iterations required.",
        cxxopts::value<int>(o_iterations)->default_value(std::to_string(PERFORMANCE_ITERATIONS)),
        "#");

    try
    {
        auto result = options.parse(argc, argv);
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        std::cout << "Error: Unable to parse command line argument options: " << e.what() << std::endl << std::endl;
        std::cout << options.help({}) << std::endl;
        exit(1);
    }

    if (o_help) // Do we want to display the help message?
    {
        std::cout << options.help({}) << std::endl;
        exit(0);
    }
    else if (o_version) // Do we want to display the software version?
    {
        std::cout << getProjectCLIHeader() << std::endl;
        exit(0);
    }

    if (o_iterations < 1000 && o_benchmark)
    {
        std::cout << std::endl
                  << "Error: The number of --iterations should be at least 1,000 for reasonable accuracy" << std::endl;
        exit(1);
    }

    int o_iterations_long = o_iterations * PERFORMANCE_ITERATIONS_LONG_MULTIPLIER;

    try
    {
        std::cout << getProjectCLIHeader() << std::endl;

        std::cout << "Input: " << INPUT_DATA << std::endl << std::endl;

        TEST_HASH_FUNCTION(cn_slow_hash_v0, CN_SLOW_HASH_V0);
        TEST_HASH_FUNCTION(cn_slow_hash_v1, CN_SLOW_HASH_V1);
        TEST_HASH_FUNCTION(cn_slow_hash_v2, CN_SLOW_HASH_V2);

        std::cout << std::endl;

        TEST_HASH_FUNCTION(cn_lite_slow_hash_v0, CN_LITE_SLOW_HASH_V0);
        TEST_HASH_FUNCTION(cn_lite_slow_hash_v1, CN_LITE_SLOW_HASH_V1);
        TEST_HASH_FUNCTION(cn_lite_slow_hash_v2, CN_LITE_SLOW_HASH_V2);

        std::cout << std::endl;

        TEST_HASH_FUNCTION(cn_dark_slow_hash_v0, CN_DARK_SLOW_HASH_V0);
        TEST_HASH_FUNCTION(cn_dark_slow_hash_v1, CN_DARK_SLOW_HASH_V1);
        TEST_HASH_FUNCTION(cn_dark_slow_hash_v2, CN_DARK_SLOW_HASH_V2);

        std::cout << std::endl;

        TEST_HASH_FUNCTION(cn_dark_lite_slow_hash_v0, CN_DARK_LITE_SLOW_HASH_V0);
        TEST_HASH_FUNCTION(cn_dark_lite_slow_hash_v1, CN_DARK_LITE_SLOW_HASH_V1);
        TEST_HASH_FUNCTION(cn_dark_lite_slow_hash_v2, CN_DARK_LITE_SLOW_HASH_V2);

        std::cout << std::endl;

        TEST_HASH_FUNCTION(cn_turtle_slow_hash_v0, CN_TURTLE_SLOW_HASH_V0);
        TEST_HASH_FUNCTION(cn_turtle_slow_hash_v1, CN_TURTLE_SLOW_HASH_V1);
        TEST_HASH_FUNCTION(cn_turtle_slow_hash_v2, CN_TURTLE_SLOW_HASH_V2);

        std::cout << std::endl;

        TEST_HASH_FUNCTION(cn_turtle_lite_slow_hash_v0, CN_TURTLE_LITE_SLOW_HASH_V0);
        TEST_HASH_FUNCTION(cn_turtle_lite_slow_hash_v1, CN_TURTLE_LITE_SLOW_HASH_V1);
        TEST_HASH_FUNCTION(cn_turtle_lite_slow_hash_v2, CN_TURTLE_LITE_SLOW_HASH_V2);

        std::cout << std::endl;

        TEST_HASH_FUNCTION(cn_upx, CN_UPX);

        std::cout << std::endl;

        for (uint64_t height = 0; height <= 8192; height += 512)
        {
            TEST_HASH_FUNCTION_WITH_HEIGHT(cn_soft_shell_slow_hash_v0, CN_SOFT_SHELL_V0[height / 512], height);
        }

        std::cout << std::endl;

        for (uint64_t height = 0; height <= 8192; height += 512)
        {
            TEST_HASH_FUNCTION_WITH_HEIGHT(cn_soft_shell_slow_hash_v1, CN_SOFT_SHELL_V1[height / 512], height);
        }

        std::cout << std::endl;

        for (uint64_t height = 0; height <= 8192; height += 512)
        {
            TEST_HASH_FUNCTION_WITH_HEIGHT(cn_soft_shell_slow_hash_v2, CN_SOFT_SHELL_V2[height / 512], height);
        }

        std::cout << std::endl;
        runSnapshotCodecTests();

        if (o_benchmark)
        {
            std::cout << "\nPerformance Tests: Please wait, this may take a while depending on your system...\n\n";

            benchmarkUnderivePublicKey();
            benchmarkGenerateKeyDerivation();

            BENCHMARK(cn_slow_hash_v0, o_iterations);
            BENCHMARK(cn_slow_hash_v1, o_iterations);
            BENCHMARK(cn_slow_hash_v2, o_iterations);

            BENCHMARK(cn_lite_slow_hash_v0, o_iterations);
            BENCHMARK(cn_lite_slow_hash_v1, o_iterations);
            BENCHMARK(cn_lite_slow_hash_v2, o_iterations);

            BENCHMARK(cn_dark_slow_hash_v0, o_iterations);
            BENCHMARK(cn_dark_slow_hash_v1, o_iterations);
            BENCHMARK(cn_dark_slow_hash_v2, o_iterations);

            BENCHMARK(cn_dark_lite_slow_hash_v0, o_iterations);
            BENCHMARK(cn_dark_lite_slow_hash_v1, o_iterations);
            BENCHMARK(cn_dark_lite_slow_hash_v2, o_iterations);

            BENCHMARK(cn_turtle_slow_hash_v0, o_iterations_long);
            BENCHMARK(cn_turtle_slow_hash_v1, o_iterations_long);
            BENCHMARK(cn_turtle_slow_hash_v2, o_iterations_long);

            BENCHMARK(cn_turtle_lite_slow_hash_v0, o_iterations_long);
            BENCHMARK(cn_turtle_lite_slow_hash_v1, o_iterations_long);
            BENCHMARK(cn_turtle_lite_slow_hash_v2, o_iterations_long);

            BENCHMARK(cn_upx, o_iterations_long);
        }
    }
    catch (std::exception &e)
    {
        std::cout << "Something went terribly wrong...\n" << e.what() << "\n\n";
    }
}
