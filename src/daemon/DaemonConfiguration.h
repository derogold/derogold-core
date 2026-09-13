// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The CyprusCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "common/Util.h"
#include "config/CryptoNoteConfig.h"
#include "logging/ILogger.h"

#include <rapidjson/document.h>
#include <thread>

namespace DaemonConfig
{
    struct DaemonConfiguration
    {
        static constexpr uint32_t MIN_PRUNE_DEPTH_DAYS = 7;
        static constexpr uint32_t MIN_PRUNE_DEPTH =
            CryptoNote::parameters::EXPECTED_NUMBER_OF_BLOCKS_PER_DAY * MIN_PRUNE_DEPTH_DAYS;
        static constexpr uint32_t DEFAULT_PRUNE_DEPTH = MIN_PRUNE_DEPTH;
        static constexpr const char *DAEMON_MODE_STANDARD = "standard";
        static constexpr const char *DAEMON_MODE_EXPLORER = "explorer";

        bool help = false;
        bool version = false;
        bool osVersion = false;
        bool resync = false;
        uint32_t rewindToHeight = 0;

        /* Height to begin syncing from. The node will skip downloading blocks
           below this height and instead inject a bootstrap anchor state.  Must
           be combined with --resync (or used on a fresh data directory) and
           must match one of the heights in SyncBootstrapCheckpoints.h that
           also appears in CryptoNoteCheckpoints.h.  Set to 0 to disable. */
        uint32_t syncFromHeight = 0;

        bool importChain = false;
        bool exportChain = false;
        uint32_t exportNumBlocks = 0;
        bool printGenesisTx = false;

        std::string configFile;
        std::string dataDirectory = Tools::getDefaultDataDirectory();
        bool dumpConfig = false;
        std::string checkPoints = "default";
        std::string logFile;
        int logLevel = Logging::WARNING;
        bool noConsole = false;
        std::string daemonMode = DAEMON_MODE_STANDARD;
        std::string outputFile;

        std::string enableCors;
        std::string feeAddress;
        int feeAmount = 0;

        /* Serve the RPC on a local AF_UNIX socket as well as the TCP port.
           Empty leaves it off. The mode on the socket file is the entire
           access control, so it is owner-only unless widened deliberately.
           POSIX only. */
        std::string rpcIpcPath;
        std::string rpcIpcMode = "0600";
        std::string rpcIpcGroup;

        /* Attach an interactive console to a daemon that is already running,
           over its RPC socket, instead of starting a node. */
        std::string attachSocket;

        bool localIp = false;
        bool hideMyPort = false;
        std::string p2pInterface = "0.0.0.0";
        int p2pPort = CryptoNote::P2P_DEFAULT_PORT;
        int p2pExternalPort = 0;
        bool p2pResetPeerstate = false;
        std::string rpcInterface = "127.0.0.1";
        int rpcPort = CryptoNote::RPC_DEFAULT_PORT;

        std::vector<std::string> exclusiveNodes;
        std::vector<std::string> peers;
        std::vector<std::string> priorityNodes;
        std::vector<std::string> seedNodes;

        bool enableDbCompression = true;
        uint64_t dbMaxOpenFiles = CryptoNote::ROCKSDB_MAX_OPEN_FILES;
        uint64_t dbReadCacheSizeMB = CryptoNote::ROCKSDB_READ_BUFFER_MB;
        int dbThreads = static_cast<int>(std::thread::hardware_concurrency());
        uint64_t dbWriteBufferSizeMB = CryptoNote::ROCKSDB_WRITE_BUFFER_MB;
        bool dbOptimize = false;

        /* Opt-in. A node that prunes cannot serve historical blocks to peers,
           and nothing in the P2P handshake advertises that, so a requester just
           sees missing objects and drops the connection. Defaulting this on
           made every upgrading node silently delete all but the most recent
           blocks on first launch, with a full resync as the only way back. */
        bool prune = false;
        bool backgroundPrune = true;
        uint32_t pruneDepth = DEFAULT_PRUNE_DEPTH;

        /* Lite node: store full block data only from liteHeight upward, keeping
           just the indexes later blocks read below it. Permanent for the
           database once chosen, and mutually exclusive with prune and explorer
           mode. Zero leaves the node a full node. */
        bool lite = false;
        uint32_t liteHeight = 0;

        uint32_t transactionValidationThreads = std::thread::hardware_concurrency();

        /* Bounds on the per-peer block request batch. The node adapts inside
           this range from the throughput it actually measures on each peer. */
        uint32_t syncBatchMin = 20;
        uint32_t syncBatchMax = CryptoNote::BLOCKS_IDS_SYNCHRONIZING_DEFAULT_COUNT;

        /* Approximate ceiling on the bytes one block request may pull back.
           Block sizes vary by orders of magnitude across the chain, so a count
           alone does not bound the response. */
        uint64_t blockSyncBytes = 16 * 1024 * 1024;

        /* How many P2P connections to keep. Outgoing is what the connection
           maker aims for; incoming is what the listener accepts before turning
           peers away. Zero incoming makes the node outbound only. */
        uint32_t outPeers = CryptoNote::P2P_DEFAULT_CONNECTIONS_COUNT;
        uint32_t inPeers = CryptoNote::P2P_DEFAULT_CONNECTIONS_COUNT;

        /* Built-in stratum server, so a stock miner can point straight at this
           node with no pool and no bridge. Port 0 leaves it off. */
        std::string stratumBindIp = "127.0.0.1";
        uint16_t stratumBindPort = 0;

        /* 0 hands miners the network difficulty, so a miner only reports when
           it has actually found a block. */
        uint64_t stratumShareDifficulty = 0;
        size_t stratumMaxConnections = 32;

        /* Monero-style notification hooks. Each is either a command template
           or an http(s):// URL; empty leaves the hook off. */
        std::string blockNotify;
        std::string reorgNotify;
        std::string txNotify;

        /* Hooks stay quiet until the node has caught up, so a node syncing
           from zero does not fire one per block for the whole chain. */
        bool notifyDuringSync = false;

        DaemonConfiguration()
        {
            std::stringstream logfile;
            logfile << CryptoNote::CRYPTONOTE_NAME << "d.log";
            logFile = logfile.str();
        }
    };

    DaemonConfiguration initConfiguration(const char *path);

    void handleSettings(int argc, char *argv[], DaemonConfiguration &config);

    void handleSettings(const std::string &configFile, DaemonConfiguration &config);

    void asFile(const DaemonConfiguration &config, const std::string &filename);

    std::string asString(const DaemonConfiguration &config);

    rapidjson::Document asJSON(const DaemonConfiguration &config);
} // namespace DaemonConfig
