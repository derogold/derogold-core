// Copyright (c) 2018-2026, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <random>
#include <ostream>
#include <sstream>
#include <string>

namespace Common
{
    /* A 16 byte identifier, used for the network id and for per-connection
       ids. This replaces boost::uuids::uuid and is deliberately laid out the
       same way: a bare array of sixteen bytes, no padding and no other
       members.
       That matters because the P2P handshake serialises it as raw bytes,
       s.binary(&v, sizeof(v), name), so the type's layout is part of the wire
       format. Anything else here would silently break compatibility with
       every other node on the network. */
    struct Uuid
    {
        uint8_t data[16];

        bool operator==(const Uuid &other) const
        {
            return std::memcmp(data, other.data, sizeof(data)) == 0;
        }

        bool operator!=(const Uuid &other) const
        {
            return !(*this == other);
        }

        bool operator<(const Uuid &other) const
        {
            return std::memcmp(data, other.data, sizeof(data)) < 0;
        }
    };

    static_assert(sizeof(Uuid) == 16, "Uuid must stay 16 bytes; it is serialised by size onto the wire.");

    /* Canonical 8-4-4-4-12 hex form, matching what boost's uuid_io printed, so
       existing log lines keep their shape. */
    inline std::ostream &operator<<(std::ostream &os, const Uuid &uuid)
    {
        std::ostringstream formatted;

        formatted << std::hex << std::setfill('0');

        for (std::size_t i = 0; i < sizeof(uuid.data); i++)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
            {
                formatted << '-';
            }

            formatted << std::setw(2) << static_cast<unsigned int>(uuid.data[i]);
        }

        return os << formatted.str();
    }

    inline std::string toString(const Uuid &uuid)
    {
        std::ostringstream s;
        s << uuid;
        return s.str();
    }

    /* Replaces boost::uuids::random_generator. These identify a connection
       locally and are never used as a secret, so an ordinary PRNG seeded from
       the system entropy source is enough. */
    inline Uuid randomUuid()
    {
        static thread_local std::mt19937_64 engine(std::random_device {}());

        Uuid uuid {};

        for (std::size_t i = 0; i < sizeof(uuid.data); i += sizeof(uint64_t))
        {
            const uint64_t block = engine();
            std::memcpy(uuid.data + i, &block, sizeof(block));
        }

        return uuid;
    }
} // namespace Common

namespace std
{
    template<> struct hash<Common::Uuid>
    {
        std::size_t operator()(const Common::Uuid &uuid) const
        {
            /* The bytes are already random, so take a machine word of them
               rather than mixing all sixteen. */
            std::size_t result = 0;
            std::memcpy(&result, uuid.data, sizeof(result));
            return result;
        }
    };
} // namespace std
