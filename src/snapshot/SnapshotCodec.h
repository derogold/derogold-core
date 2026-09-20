// Copyright (c) 2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "CryptoTypes.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CryptoNote::Snapshot
{
    using Bytes = std::vector<uint8_t>;

    struct FormatError : public std::runtime_error
    {
        explicit FormatError(const std::string &message);
    };

    struct Limits
    {
        uint64_t maxManifestBytes = 1024 * 1024;
        uint64_t maxChunks = 16384;
        uint64_t maxOutputs = 500000000;
        uint64_t maxKeyImages = 500000000;
        uint64_t maxConsensusRecords = 1024;
        uint64_t maxRecordBytes = 65536;
        uint16_t maxStringBytes = 256;
        uint16_t maxPathBytes = 240;
        uint16_t maxWindowItems = 512;
    };

    enum class ChunkType : uint8_t
    {
        OutputRecords = 1,
        KeyImageRecords = 2,
        ConsensusWindowRecords = 3
    };

    struct ChunkDescriptor
    {
        ChunkType type = ChunkType::OutputRecords;
        uint32_t index = 0;
        uint64_t uncompressedBytes = 0;
        uint64_t compressedBytes = 0;
        Crypto::Hash logicalHash {};
        Crypto::Hash compressedHash {};
        std::string path;
    };

    struct Manifest
    {
        uint16_t formatVersion = 1;
        uint16_t minReaderVersion = 1;
        std::string network;
        Crypto::Hash genesisHash {};
        uint32_t anchorHeight = 0;
        Crypto::Hash anchorHash {};
        Crypto::Hash anchorPreviousHash {};
        uint64_t anchorTimestamp = 0;
        uint8_t anchorMajorVersion = 0;
        uint8_t anchorMinorVersion = 0;
        uint64_t cumulativeDifficulty = 0;
        uint64_t alreadyGeneratedCoins = 0;
        uint64_t alreadyGeneratedTransactions = 0;
        std::string sourceCommit;
        uint32_t sourceDbSchemaVersion = 0;
        uint64_t outputCount = 0;
        uint64_t keyImageCount = 0;
        uint64_t consensusRecordCount = 0;
        std::vector<ChunkDescriptor> chunks;
        Crypto::Hash logicalStateRoot {};
    };

    struct OutputRecord
    {
        uint64_t amount = 0;
        uint32_t globalIndex = 0;
        Crypto::PublicKey publicKey {};
        Crypto::Hash transactionHash {};
        uint16_t transactionOutputIndex = 0;
        uint64_t unlockTime = 0;
    };

    struct KeyImageRecord
    {
        Crypto::KeyImage keyImage {};
        uint32_t spendingBlockHeight = 0;
    };

    struct ConsensusWindowRecord
    {
        uint32_t anchorHeight = 0;
        std::vector<uint64_t> timestamps;
        std::vector<uint64_t> cumulativeDifficulties;
        std::vector<uint64_t> blockSizes;
        Crypto::Hash anchorRawBlockHash {};
    };

    Bytes encodeManifest(const Manifest &manifest, bool includeRoot = true);
    Manifest parseManifest(const Bytes &bytes, const Limits &limits = Limits {});

    Bytes encodeOutputRecord(const OutputRecord &record);
    OutputRecord parseOutputRecord(const Bytes &bytes, const Limits &limits = Limits {});

    Bytes encodeKeyImageRecord(const KeyImageRecord &record);
    KeyImageRecord parseKeyImageRecord(const Bytes &bytes, const Limits &limits = Limits {});

    Bytes encodeConsensusWindowRecord(const ConsensusWindowRecord &record);
    ConsensusWindowRecord parseConsensusWindowRecord(const Bytes &bytes, const Limits &limits = Limits {});

    void validateCanonicalChunks(const std::vector<ChunkDescriptor> &chunks, const Limits &limits = Limits {});
    void validateCanonicalOutputs(const std::vector<OutputRecord> &records);
    void validateCanonicalKeyImages(const std::vector<KeyImageRecord> &records);
    void validateCanonicalConsensusWindows(const std::vector<ConsensusWindowRecord> &records);

    Crypto::Hash sha256(const Bytes &bytes);
    Crypto::Hash logicalStateRoot(
        const Manifest &manifest,
        const std::vector<OutputRecord> &outputs,
        const std::vector<KeyImageRecord> &keyImages,
        const std::vector<ConsensusWindowRecord> &consensusWindows);
    std::string hashToHex(const Crypto::Hash &hash);
} // namespace CryptoNote::Snapshot
