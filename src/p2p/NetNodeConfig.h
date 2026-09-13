// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The CyprusCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "P2pProtocolTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#ifdef __MINGW32__
#undef interface
#endif

namespace CryptoNote
{
    class NetNodeConfig
    {
      public:
        NetNodeConfig();

        bool init(
            const std::string interface,
            const int port,
            const int external,
            const bool localIp,
            const bool hidePort,
            const std::string dataDir,
            const std::vector<std::string> addPeers,
            const std::vector<std::string> addExclusiveNodes,
            const std::vector<std::string> addPriorityNodes,
            const std::vector<std::string> addSeedNodes,
            const bool p2pResetPeerState,
            const uint32_t outPeersCount,
            const uint32_t inPeersCount);

        std::string getP2pStateFilename() const;

        bool getP2pStateReset() const;

        std::string getBindIp() const;

        uint16_t getBindPort() const;

        uint16_t getExternalPort() const;

        bool getAllowLocalIp() const;

        std::vector<PeerlistEntry> getPeers() const;

        std::vector<NetworkAddress> getPriorityNodes() const;

        std::vector<NetworkAddress> getExclusiveNodes() const;

        std::vector<NetworkAddress> getSeedNodes() const;

        /* --seed-node exactly as it was typed. Kept unresolved so a hostname
           can be looked up again while the node runs, and so one that does not
           resolve at startup is not lost for the rest of the process. */
        std::vector<std::string> getSeedNodeAddresses() const;

        bool getHideMyPort() const;

        std::string getConfigFolder() const;

        uint32_t getOutPeers() const;

        uint32_t getInPeers() const;

      private:
        std::string bindIp;

        uint16_t bindPort;

        uint16_t externalPort;

        bool allowLocalIp;

        std::vector<PeerlistEntry> peers;

        std::vector<NetworkAddress> priorityNodes;

        std::vector<NetworkAddress> exclusiveNodes;

        std::vector<NetworkAddress> seedNodes;

        std::vector<std::string> seedNodeAddresses;

        bool hideMyPort;

        std::string configFolder;

        std::string p2pStateFilename;

        bool p2pStateReset;

        /* How many outgoing connections the connection maker aims for, and how
           many incoming ones the listener accepts before turning peers away. */
        uint32_t outPeers;

        uint32_t inPeers;
    };

} // namespace CryptoNote
