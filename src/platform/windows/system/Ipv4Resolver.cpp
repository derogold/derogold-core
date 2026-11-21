// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "Ipv4Resolver.h"

#include <cassert>
#include <random>
#include <stdexcept>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <system/Dispatcher.h>
#include <system/ErrorMessage.h>
#include <system/InterruptedException.h>
#include <system/Ipv4Address.h>
#include <ws2tcpip.h>

namespace System
{
    Ipv4Resolver::Ipv4Resolver(): dispatcher(nullptr) {}

    Ipv4Resolver::Ipv4Resolver(Dispatcher &dispatcher): dispatcher(&dispatcher) {}

    Ipv4Resolver::Ipv4Resolver(Ipv4Resolver &&other): dispatcher(other.dispatcher)
    {
        if (dispatcher != nullptr)
        {
            other.dispatcher = nullptr;
        }
    }

    Ipv4Resolver::~Ipv4Resolver() {}

    Ipv4Resolver &Ipv4Resolver::operator=(Ipv4Resolver &&other)
    {
        dispatcher = other.dispatcher;
        if (dispatcher != nullptr)
        {
            other.dispatcher = nullptr;
        }

        return *this;
    }

    std::vector<Ipv4Address> Ipv4Resolver::resolveAll(const std::string &host)
    {
        /* Deliberately no dispatcher use: the seed resolver calls this from a
           helper thread. getaddrinfo blocks on either thread anyway. */
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo *addressInfos = nullptr;
        int result = getaddrinfo(host.c_str(), NULL, &hints, &addressInfos);
        if (result != 0)
        {
            throw std::runtime_error("Ipv4Resolver::resolveAll, getaddrinfo failed, " + errorMessage(result));
        }

        std::vector<Ipv4Address> addresses;

        for (addrinfo *addressInfo = addressInfos; addressInfo != nullptr; addressInfo = addressInfo->ai_next)
        {
            if (addressInfo->ai_family != AF_INET)
            {
                continue;
            }

            addresses.emplace_back(ntohl(reinterpret_cast<sockaddr_in *>(addressInfo->ai_addr)->sin_addr.S_un.S_addr));
        }

        /* The whole list, from the head. The old code passed the randomly
           chosen node instead, which leaked everything before it. */
        freeaddrinfo(addressInfos);

        return addresses;
    }

    Ipv4Address Ipv4Resolver::resolve(const std::string &host)
    {
        assert(dispatcher != nullptr);
        if (dispatcher->interrupted())
        {
            throw InterruptedException();
        }

        std::vector<Ipv4Address> addresses = resolveAll(host);
        if (addresses.empty())
        {
            throw std::runtime_error("Ipv4Resolver::resolve, no IPv4 address for host " + host);
        }

        std::mt19937 generator {std::random_device()()};
        return addresses[std::uniform_int_distribution<std::size_t>(0, addresses.size() - 1)(generator)];
    }

} // namespace System
