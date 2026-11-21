// Copyright (c) 2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#include "snapshot/SnapshotCodec.h"

#include "common/StringTools.h"

#include <cryptopp/sha.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>

namespace CryptoNote::Snapshot
{
    namespace
    {
        constexpr uint16_t SUPPORTED_FORMAT_VERSION = 1;
        constexpr uint16_t SUPPORTED_MIN_READER_VERSION = 1;
        constexpr uint8_t HASH_SIZE = 32;

        const uint8_t MANIFEST_MAGIC[8] = {'D', 'G', 'S', 'N', 'A', 'P', 'M', '1'};
        const uint8_t OUTPUT_MAGIC[8] = {'D', 'G', 'O', 'U', 'T', '0', '0', '1'};
        const uint8_t KEY_IMAGE_MAGIC[8] = {'D', 'G', 'K', 'I', '0', '0', '0', '1'};
        const uint8_t CONSENSUS_MAGIC[8] = {'D', 'G', 'C', 'W', '0', '0', '0', '1'};

        void append(Bytes &out, uint8_t value)
        {
            out.push_back(value);
        }

        void appendLe16(Bytes &out, uint16_t value)
        {
            out.push_back(static_cast<uint8_t>(value & 0xff));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        }

        void appendLe32(Bytes &out, uint32_t value)
        {
            for (uint8_t i = 0; i < 4; ++i)
            {
                out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
            }
        }

        void appendLe64(Bytes &out, uint64_t value)
        {
            for (uint8_t i = 0; i < 8; ++i)
            {
                out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
            }
        }

        template<typename T> void appendPod(Bytes &out, const T &pod)
        {
            const auto *ptr = reinterpret_cast<const uint8_t *>(&pod);
            out.insert(out.end(), ptr, ptr + sizeof(T));
        }

        void appendMagic(Bytes &out, const uint8_t (&magic)[8])
        {
            out.insert(out.end(), std::begin(magic), std::end(magic));
        }

        void appendString(Bytes &out, const std::string &value, uint16_t maxSize, const char *field)
        {
            if (value.size() > maxSize || value.size() > std::numeric_limits<uint16_t>::max())
            {
                throw FormatError(std::string(field) + " is too long");
            }

            appendLe16(out, static_cast<uint16_t>(value.size()));
            out.insert(out.end(), value.begin(), value.end());
        }

        void appendHash(Bytes &out, const Crypto::Hash &hash)
        {
            appendPod(out, hash);
        }

        void appendPublicKey(Bytes &out, const Crypto::PublicKey &key)
        {
            appendPod(out, key);
        }

        void appendKeyImage(Bytes &out, const Crypto::KeyImage &keyImage)
        {
            appendPod(out, keyImage);
        }

        class Reader
        {
          public:
            explicit Reader(const Bytes &bytes) : m_bytes(bytes) {}

            void expectMagic(const uint8_t (&magic)[8])
            {
                require(8, "truncated magic");
                if (std::memcmp(m_bytes.data() + m_offset, magic, 8) != 0)
                {
                    throw FormatError("unsupported record magic");
                }
                m_offset += 8;
            }

            uint8_t readU8()
            {
                require(1, "truncated uint8");
                return m_bytes[m_offset++];
            }

            uint16_t readLe16()
            {
                require(2, "truncated uint16");
                const uint16_t result =
                    static_cast<uint16_t>(m_bytes[m_offset]) |
                    (static_cast<uint16_t>(m_bytes[m_offset + 1]) << 8);
                m_offset += 2;
                return result;
            }

            uint32_t readLe32()
            {
                require(4, "truncated uint32");
                uint32_t result = 0;
                for (uint8_t i = 0; i < 4; ++i)
                {
                    result |= static_cast<uint32_t>(m_bytes[m_offset + i]) << (8 * i);
                }
                m_offset += 4;
                return result;
            }

            uint64_t readLe64()
            {
                require(8, "truncated uint64");
                uint64_t result = 0;
                for (uint8_t i = 0; i < 8; ++i)
                {
                    result |= static_cast<uint64_t>(m_bytes[m_offset + i]) << (8 * i);
                }
                m_offset += 8;
                return result;
            }

            template<typename T> T readPod(const char *field)
            {
                require(sizeof(T), std::string("truncated ") + field);
                T result {};
                std::memcpy(&result, m_bytes.data() + m_offset, sizeof(T));
                m_offset += sizeof(T);
                return result;
            }

            std::string readString(uint16_t maxSize, const char *field)
            {
                const uint16_t size = readLe16();
                if (size > maxSize)
                {
                    throw FormatError(std::string(field) + " exceeds parser limit");
                }

                require(size, std::string("truncated ") + field);
                std::string result(reinterpret_cast<const char *>(m_bytes.data() + m_offset), size);
                m_offset += size;
                return result;
            }

            void finish() const
            {
                if (m_offset != m_bytes.size())
                {
                    throw FormatError("trailing bytes after record");
                }
            }

          private:
            void require(size_t size, const std::string &message) const
            {
                if (size > m_bytes.size() - m_offset)
                {
                    throw FormatError(message);
                }
            }

            const Bytes &m_bytes;
            size_t m_offset = 0;
        };

        bool hasPathTraversal(const std::string &path)
        {
            if (path.empty() || path[0] == '/' || path.find('\\') != std::string::npos)
            {
                return true;
            }

            size_t start = 0;
            while (start <= path.size())
            {
                const size_t end = path.find('/', start);
                const std::string component = path.substr(start, end == std::string::npos ? end : end - start);
                if (component.empty() || component == "." || component == "..")
                {
                    return true;
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }

            return false;
        }

        bool chunkLess(const ChunkDescriptor &left, const ChunkDescriptor &right)
        {
            if (left.type != right.type)
            {
                return static_cast<uint8_t>(left.type) < static_cast<uint8_t>(right.type);
            }
            return left.index < right.index;
        }

        void appendU64Vector(Bytes &out, const std::vector<uint64_t> &values, const Limits &limits, const char *field)
        {
            if (values.size() > limits.maxWindowItems)
            {
                throw FormatError(std::string(field) + " exceeds parser limit");
            }

            appendLe16(out, static_cast<uint16_t>(values.size()));
            for (const uint64_t value : values)
            {
                appendLe64(out, value);
            }
        }

        std::vector<uint64_t> readU64Vector(Reader &reader, const Limits &limits, const char *field)
        {
            const uint16_t count = reader.readLe16();
            if (count > limits.maxWindowItems)
            {
                throw FormatError(std::string(field) + " exceeds parser limit");
            }

            std::vector<uint64_t> values;
            values.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                values.push_back(reader.readLe64());
            }

            return values;
        }
    } // namespace

    FormatError::FormatError(const std::string &message) : std::runtime_error(message) {}

    Bytes encodeManifest(const Manifest &manifest, bool includeRoot)
    {
        const Limits limits;
        if (manifest.formatVersion != SUPPORTED_FORMAT_VERSION ||
            manifest.minReaderVersion > SUPPORTED_MIN_READER_VERSION)
        {
            throw FormatError("unsupported manifest version");
        }

        validateCanonicalChunks(manifest.chunks, limits);

        Bytes out;
        appendMagic(out, MANIFEST_MAGIC);
        appendLe16(out, manifest.formatVersion);
        appendLe16(out, manifest.minReaderVersion);
        appendString(out, manifest.network, limits.maxStringBytes, "network");
        appendHash(out, manifest.genesisHash);
        appendLe32(out, manifest.anchorHeight);
        appendHash(out, manifest.anchorHash);
        appendHash(out, manifest.anchorPreviousHash);
        appendLe64(out, manifest.anchorTimestamp);
        append(out, manifest.anchorMajorVersion);
        append(out, manifest.anchorMinorVersion);
        appendLe64(out, manifest.cumulativeDifficulty);
        appendLe64(out, manifest.alreadyGeneratedCoins);
        appendLe64(out, manifest.alreadyGeneratedTransactions);
        appendString(out, manifest.sourceCommit, limits.maxStringBytes, "source commit");
        appendLe32(out, manifest.sourceDbSchemaVersion);
        appendLe64(out, manifest.outputCount);
        appendLe64(out, manifest.keyImageCount);
        appendLe64(out, manifest.consensusRecordCount);
        appendLe64(out, static_cast<uint64_t>(manifest.chunks.size()));

        for (const auto &chunk : manifest.chunks)
        {
            append(out, static_cast<uint8_t>(chunk.type));
            appendLe32(out, chunk.index);
            appendLe64(out, chunk.uncompressedBytes);
            appendLe64(out, chunk.compressedBytes);
            appendHash(out, chunk.logicalHash);
            appendHash(out, chunk.compressedHash);
            appendString(out, chunk.path, limits.maxPathBytes, "chunk path");
        }

        if (includeRoot)
        {
            appendHash(out, manifest.logicalStateRoot);
        }

        return out;
    }

    Manifest parseManifest(const Bytes &bytes, const Limits &limits)
    {
        if (bytes.size() > limits.maxManifestBytes)
        {
            throw FormatError("manifest exceeds parser limit");
        }

        Reader reader(bytes);
        Manifest manifest;
        reader.expectMagic(MANIFEST_MAGIC);
        manifest.formatVersion = reader.readLe16();
        manifest.minReaderVersion = reader.readLe16();
        if (manifest.formatVersion != SUPPORTED_FORMAT_VERSION ||
            manifest.minReaderVersion > SUPPORTED_MIN_READER_VERSION)
        {
            throw FormatError("unsupported manifest version");
        }

        manifest.network = reader.readString(limits.maxStringBytes, "network");
        manifest.genesisHash = reader.readPod<Crypto::Hash>("genesis hash");
        manifest.anchorHeight = reader.readLe32();
        manifest.anchorHash = reader.readPod<Crypto::Hash>("anchor hash");
        manifest.anchorPreviousHash = reader.readPod<Crypto::Hash>("anchor previous hash");
        manifest.anchorTimestamp = reader.readLe64();
        manifest.anchorMajorVersion = reader.readU8();
        manifest.anchorMinorVersion = reader.readU8();
        manifest.cumulativeDifficulty = reader.readLe64();
        manifest.alreadyGeneratedCoins = reader.readLe64();
        manifest.alreadyGeneratedTransactions = reader.readLe64();
        manifest.sourceCommit = reader.readString(limits.maxStringBytes, "source commit");
        manifest.sourceDbSchemaVersion = reader.readLe32();
        manifest.outputCount = reader.readLe64();
        manifest.keyImageCount = reader.readLe64();
        manifest.consensusRecordCount = reader.readLe64();
        const uint64_t chunkCount = reader.readLe64();

        if (manifest.outputCount > limits.maxOutputs ||
            manifest.keyImageCount > limits.maxKeyImages ||
            manifest.consensusRecordCount > limits.maxConsensusRecords ||
            chunkCount > limits.maxChunks)
        {
            throw FormatError("manifest count exceeds parser limit");
        }

        manifest.chunks.reserve(static_cast<size_t>(chunkCount));
        for (uint64_t i = 0; i < chunkCount; ++i)
        {
            ChunkDescriptor chunk;
            const uint8_t type = reader.readU8();
            if (type < static_cast<uint8_t>(ChunkType::OutputRecords) ||
                type > static_cast<uint8_t>(ChunkType::ConsensusWindowRecords))
            {
                throw FormatError("unsupported chunk type");
            }

            chunk.type = static_cast<ChunkType>(type);
            chunk.index = reader.readLe32();
            chunk.uncompressedBytes = reader.readLe64();
            chunk.compressedBytes = reader.readLe64();
            chunk.logicalHash = reader.readPod<Crypto::Hash>("logical chunk hash");
            chunk.compressedHash = reader.readPod<Crypto::Hash>("compressed chunk hash");
            chunk.path = reader.readString(limits.maxPathBytes, "chunk path");
            manifest.chunks.push_back(chunk);
        }

        manifest.logicalStateRoot = reader.readPod<Crypto::Hash>("logical state root");
        reader.finish();

        validateCanonicalChunks(manifest.chunks, limits);
        return manifest;
    }

    Bytes encodeOutputRecord(const OutputRecord &record)
    {
        Bytes out;
        appendMagic(out, OUTPUT_MAGIC);
        appendLe64(out, record.amount);
        appendLe32(out, record.globalIndex);
        appendPublicKey(out, record.publicKey);
        appendHash(out, record.transactionHash);
        appendLe16(out, record.transactionOutputIndex);
        appendLe64(out, record.unlockTime);
        return out;
    }

    OutputRecord parseOutputRecord(const Bytes &bytes, const Limits &limits)
    {
        if (bytes.size() > limits.maxRecordBytes)
        {
            throw FormatError("output record exceeds parser limit");
        }

        Reader reader(bytes);
        OutputRecord record;
        reader.expectMagic(OUTPUT_MAGIC);
        record.amount = reader.readLe64();
        record.globalIndex = reader.readLe32();
        record.publicKey = reader.readPod<Crypto::PublicKey>("public key");
        record.transactionHash = reader.readPod<Crypto::Hash>("transaction hash");
        record.transactionOutputIndex = reader.readLe16();
        record.unlockTime = reader.readLe64();
        reader.finish();
        return record;
    }

    Bytes encodeKeyImageRecord(const KeyImageRecord &record)
    {
        Bytes out;
        appendMagic(out, KEY_IMAGE_MAGIC);
        appendKeyImage(out, record.keyImage);
        appendLe32(out, record.spendingBlockHeight);
        return out;
    }

    KeyImageRecord parseKeyImageRecord(const Bytes &bytes, const Limits &limits)
    {
        if (bytes.size() > limits.maxRecordBytes)
        {
            throw FormatError("key-image record exceeds parser limit");
        }

        Reader reader(bytes);
        KeyImageRecord record;
        reader.expectMagic(KEY_IMAGE_MAGIC);
        record.keyImage = reader.readPod<Crypto::KeyImage>("key image");
        record.spendingBlockHeight = reader.readLe32();
        reader.finish();
        return record;
    }

    Bytes encodeConsensusWindowRecord(const ConsensusWindowRecord &record)
    {
        const Limits limits;
        Bytes out;
        appendMagic(out, CONSENSUS_MAGIC);
        appendLe32(out, record.anchorHeight);
        appendU64Vector(out, record.timestamps, limits, "timestamps");
        appendU64Vector(out, record.cumulativeDifficulties, limits, "cumulative difficulties");
        appendU64Vector(out, record.blockSizes, limits, "block sizes");
        appendHash(out, record.anchorRawBlockHash);
        return out;
    }

    ConsensusWindowRecord parseConsensusWindowRecord(const Bytes &bytes, const Limits &limits)
    {
        if (bytes.size() > limits.maxRecordBytes)
        {
            throw FormatError("consensus-window record exceeds parser limit");
        }

        Reader reader(bytes);
        ConsensusWindowRecord record;
        reader.expectMagic(CONSENSUS_MAGIC);
        record.anchorHeight = reader.readLe32();
        record.timestamps = readU64Vector(reader, limits, "timestamps");
        record.cumulativeDifficulties = readU64Vector(reader, limits, "cumulative difficulties");
        record.blockSizes = readU64Vector(reader, limits, "block sizes");
        record.anchorRawBlockHash = reader.readPod<Crypto::Hash>("anchor raw block hash");
        reader.finish();
        return record;
    }

    void validateCanonicalChunks(const std::vector<ChunkDescriptor> &chunks, const Limits &limits)
    {
        if (chunks.size() > limits.maxChunks)
        {
            throw FormatError("too many chunks");
        }

        std::set<std::pair<uint8_t, uint32_t>> seen;
        for (size_t i = 0; i < chunks.size(); ++i)
        {
            const auto &chunk = chunks[i];
            if (hasPathTraversal(chunk.path))
            {
                throw FormatError("chunk path is not portable");
            }

            const auto key = std::make_pair(static_cast<uint8_t>(chunk.type), chunk.index);
            if (!seen.insert(key).second)
            {
                throw FormatError("duplicate chunk descriptor");
            }

            if (i > 0 && !chunkLess(chunks[i - 1], chunk))
            {
                throw FormatError("chunk descriptors are not in canonical order");
            }
        }
    }

    void validateCanonicalOutputs(const std::vector<OutputRecord> &records)
    {
        for (size_t i = 1; i < records.size(); ++i)
        {
            const auto &left = records[i - 1];
            const auto &right = records[i];
            if (left.amount > right.amount ||
                (left.amount == right.amount && left.globalIndex >= right.globalIndex))
            {
                throw FormatError("output records are not in canonical order");
            }
        }
    }

    void validateCanonicalKeyImages(const std::vector<KeyImageRecord> &records)
    {
        for (size_t i = 1; i < records.size(); ++i)
        {
            if (std::memcmp(&records[i - 1].keyImage, &records[i].keyImage, sizeof(Crypto::KeyImage)) >= 0)
            {
                throw FormatError("key-image records are not in canonical order");
            }
        }
    }

    void validateCanonicalConsensusWindows(const std::vector<ConsensusWindowRecord> &records)
    {
        for (size_t i = 1; i < records.size(); ++i)
        {
            if (records[i - 1].anchorHeight >= records[i].anchorHeight)
            {
                throw FormatError("consensus-window records are not in canonical order");
            }
        }
    }

    Crypto::Hash sha256(const Bytes &bytes)
    {
        Crypto::Hash result {};
        CryptoPP::SHA256 hash;
        if (!bytes.empty())
        {
            hash.Update(bytes.data(), bytes.size());
        }
        hash.Final(reinterpret_cast<CryptoPP::byte *>(&result));
        return result;
    }

    Crypto::Hash logicalStateRoot(
        const Manifest &manifest,
        const std::vector<OutputRecord> &outputs,
        const std::vector<KeyImageRecord> &keyImages,
        const std::vector<ConsensusWindowRecord> &consensusWindows)
    {
        validateCanonicalOutputs(outputs);
        validateCanonicalKeyImages(keyImages);
        validateCanonicalConsensusWindows(consensusWindows);

        Bytes bytes;
        const std::string domain = "DeroGold consensus snapshot v1 logical root";
        bytes.insert(bytes.end(), domain.begin(), domain.end());
        bytes.push_back(0);

        const Bytes manifestBytes = encodeManifest(manifest, false);
        bytes.insert(bytes.end(), manifestBytes.begin(), manifestBytes.end());

        for (const auto &record : outputs)
        {
            const Bytes encoded = encodeOutputRecord(record);
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        }
        for (const auto &record : keyImages)
        {
            const Bytes encoded = encodeKeyImageRecord(record);
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        }
        for (const auto &record : consensusWindows)
        {
            const Bytes encoded = encodeConsensusWindowRecord(record);
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        }

        return sha256(bytes);
    }

    std::string hashToHex(const Crypto::Hash &hash)
    {
        return Common::toHex(&hash, HASH_SIZE);
    }
} // namespace CryptoNote::Snapshot
