// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The CyprusCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#include "DaemonConfiguration.h"

#include "common/PathTools.h"
#include "common/Util.h"
#include "config/CliHeader.h"
#include "config/CryptoNoteConfig.h"
#include "config/SyncBootstrapCheckpoints.h"

#include <cxxopts.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <logging/ILogger.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

using namespace rapidjson;

namespace DaemonConfig
{
    namespace
    {
        uint32_t clampPruneDepth(const uint32_t depth, const std::string &source)
        {
            if (depth >= DaemonConfiguration::MIN_PRUNE_DEPTH)
            {
                return depth;
            }

            std::cout << CryptoNote::getProjectCLIHeader() << "The configured prune depth (" << depth
                      << ") from " << source << " is below the enforced minimum (" << DaemonConfiguration::MIN_PRUNE_DEPTH
                      << ", about " << DaemonConfiguration::MIN_PRUNE_DEPTH_DAYS
                      << " days). Using the minimum." << std::endl;

            return DaemonConfiguration::MIN_PRUNE_DEPTH;
        }

        std::string normalizeDaemonMode(const std::string &rawMode)
        {
            std::string mode = rawMode;
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (mode == DaemonConfiguration::DAEMON_MODE_STANDARD || mode == DaemonConfiguration::DAEMON_MODE_EXPLORER)
            {
                return mode;
            }

            throw std::runtime_error(
                "Invalid daemon-mode: '" + rawMode + "'. Allowed values are 'standard' or 'explorer'.");
        }
    } // namespace

    DaemonConfiguration initConfiguration(const char *path)
    {
        DaemonConfiguration config;
        config.logFile = Common::ReplaceExtenstion(Common::NativePathToGeneric(path), ".log");
        return config;
    }

    void handleSettings(const int argc, char *argv[], DaemonConfiguration &config)
    {
        cxxopts::Options options(argv[0], CryptoNote::getProjectCLIHeader());

        // clang-format off
        options.add_options("Core")
            ("help", "Display this help message.", cxxopts::value<bool>(config.help))
            ("version", "Output daemon version information.", cxxopts::value<bool>(config.version))
            ("os-version", "Output Operating System version information.", cxxopts::value<bool>(config.osVersion))
            ("resync", "Forces the daemon to delete the blockchain data and start resyncing.", cxxopts::value<bool>(config.resync))
            ("prune", "Enable pruned-node mode.", cxxopts::value<bool>(config.prune)->default_value(config.prune ? "true" : "false"))
            ("background-prune", "Enable periodic background prune task.",
             cxxopts::value<bool>(config.backgroundPrune)->default_value(config.backgroundPrune ? "true" : "false"))
            ("prune-depth", "When prune mode is enabled, retain at least this many recent blocks locally.",
             cxxopts::value<uint32_t>(config.pruneDepth), "<blocks>")
            ("lite", "Lite-node mode: store full block data only from --lite-height upward. Permanent for this database, and cannot be combined with --prune or --daemon-mode explorer.",
             cxxopts::value<bool>(config.lite))
            ("lite-height", "Height at and above which a lite node stores full block data. Required with --lite.",
             cxxopts::value<uint32_t>(config.liteHeight), "<height>")
            ("rewind-to-height", "Rewinds the local blockchain cache to the specified height.", cxxopts::value<uint32_t>(config.rewindToHeight), "<height>")
            ("sync-from-height", "Skip downloading blocks below <height> by bootstrapping from a trusted checkpoint state. "
             "Must be used on a fresh data directory (or combined with --resync). "
             "The height must match one of the pre-computed entries in SyncBootstrapCheckpoints.h "
             "and must also appear in the built-in checkpoint list.",
             cxxopts::value<uint32_t>(config.syncFromHeight), "<height>");

        options.add_options("Import / Export")
            ("import-blockchain", "Import blockchain from dump file.", cxxopts::value<bool>(config.importChain))
            ("export-blockchain", "Export blockchain to a dump file.", cxxopts::value<bool>(config.exportChain))
            ("max-export-blocks", "Maximum number of blocks for export to dump file.", cxxopts::value<uint32_t>(config.exportNumBlocks), "<blocks>");

        options.add_options("Genesis Block")
            ("print-genesis-tx", "Print the genesis block transaction hex and exits.", cxxopts::value<bool>(config.printGenesisTx));

        options.add_options("Daemon")
            ("c,config-file", "Specify the <path> to a configuration file", cxxopts::value<std::string>(config.configFile), "<path>")
            ("data-dir", "Specify the <path> to the Blockchain data directory", cxxopts::value<std::string>(config.dataDirectory), "<path>")
            ("dump-config", "Prints the current configuration to the screen", cxxopts::value<bool>(config.dumpConfig))
            ("daemon-mode", "Daemon RPC mode: standard or explorer",
             cxxopts::value<std::string>(config.daemonMode), "<standard|explorer>")
            ("load-checkpoints", "Specify a file <path> containing a CSV of Blockchain checkpoints for faster sync. A value of 'default' uses the built-in checkpoints.", cxxopts::value<std::string>(config.checkPoints), "<path>")
            ("log-file", "Specify the <path> to the log file", cxxopts::value<std::string>(config.logFile), "<path>")
            ("log-level", "Specify log level", cxxopts::value<int>(config.logLevel))
            ("no-console", "Disable daemon console commands", cxxopts::value<bool>(config.noConsole))
            ("save-config", "Save the configuration to the specified <file>", cxxopts::value<std::string>(config.outputFile), "<file>");

        options.add_options("RPC")
            ("enable-cors", "Adds header 'Access-Control-Allow-Origin' to the RPC responses using the <domain>. Uses the value specified as the domain. Use * for all.", cxxopts::value<std::string>(config.enableCors), "<domain>")
            ("fee-address", "Sets the convenience charge <address> for light wallets that use the daemon", cxxopts::value<std::string>(config.feeAddress), "<address>")
            ("fee-amount", "Sets the convenience charge amount for light wallets that use the daemon", cxxopts::value<int>(config.feeAmount))
            ("rpc-ipc-path", "Also serve the RPC on a local socket at <path>, whose file permissions decide who may connect. POSIX only", cxxopts::value<std::string>(config.rpcIpcPath), "<path>")
            ("rpc-ipc-mode", "Permissions for the RPC socket file, in octal. The default allows only the user running the daemon", cxxopts::value<std::string>(config.rpcIpcMode), "<octal>")
            ("rpc-ipc-group", "Group to own the RPC socket file, so members of that group may connect", cxxopts::value<std::string>(config.rpcIpcGroup), "<group>");

        options.add_options("Mining")
            ("stratum-bind-ip", "Interface the built-in stratum server listens on. Loopback by default: the port has no authentication, so anyone who can reach it can mine to their own address using this node", cxxopts::value<std::string>(config.stratumBindIp), "<ip>")
            ("stratum-bind-port", "Port for the built-in stratum server, so a miner can mine straight to this node. 0 disables it", cxxopts::value<uint16_t>(config.stratumBindPort), "#")
            ("stratum-share-difficulty", "Difficulty stratum miners are given. 0 uses the network difficulty, so a miner only reports when it has found a block; a lower value makes it report progress as well", cxxopts::value<uint64_t>(config.stratumShareDifficulty), "#")
            ("stratum-max-connections", "Miners allowed on the stratum port at once", cxxopts::value<size_t>(config.stratumMaxConnections), "#");

        options.add_options("Console")
            ("attach", "Attach an interactive console to a daemon already running, over its RPC socket at <path>, instead of starting a node", cxxopts::value<std::string>(config.attachSocket), "<path>");

        options.add_options("Notifications")
            ("block-notify", "Run a command or POST to an http(s):// URL for each new main-chain block. Command placeholders: %s block hash, %h height (no shell; quotes group arguments)", cxxopts::value<std::string>(config.blockNotify), "<cmd|url>")
            ("reorg-notify", "Run a command or POST to an http(s):// URL on every chain reorganisation. Placeholders: %s split height, %h new height, %n new blocks, %d discarded blocks", cxxopts::value<std::string>(config.reorgNotify), "<cmd|url>")
            ("tx-notify", "Run a command or POST to an http(s):// URL for each transaction entering the pool. Placeholders: %s transaction hash", cxxopts::value<std::string>(config.txNotify), "<cmd|url>")
            ("notify-during-sync", "Also fire the *-notify hooks while the node is still synchronizing (default: suppressed)", cxxopts::value<bool>(config.notifyDuringSync));

        options.add_options("Network")
            ("allow-local-ip", "Allow the local IP to be added to the peer list", cxxopts::value<bool>(config.localIp))
            ("hide-my-port", "Do not announce yourself as a peerlist candidate", cxxopts::value<bool>(config.hideMyPort))
            ("p2p-bind-ip", "Interface IP address for the P2P service", cxxopts::value<std::string>(config.p2pInterface), "<ip>")
            ("p2p-bind-port", "TCP port for the P2P service", cxxopts::value<int>(config.p2pPort), "#")
            ("p2p-external-port", "External TCP port for the P2P service (NAT port forward)", cxxopts::value<int>(config.p2pExternalPort), "#")
            ("p2p-reset-peerstate", "Generate a new peer ID and remove known peers saved previously", cxxopts::value<bool>(config.p2pResetPeerstate))
            ("out-peers", "Maximum number of outgoing P2P connections to maintain", cxxopts::value<uint32_t>(config.outPeers), "#")
            ("in-peers", "Maximum number of incoming P2P connections to accept. 0 makes this node outbound only", cxxopts::value<uint32_t>(config.inPeers), "#")
            ("sync-batch-min", "Smallest number of blocks to request from a peer at once", cxxopts::value<uint32_t>(config.syncBatchMin), "#")
            ("sync-batch-max", "Largest number of blocks to request from a peer at once", cxxopts::value<uint32_t>(config.syncBatchMax), "#")
            ("block-sync-bytes", "Approximate ceiling on the bytes one block request may pull back", cxxopts::value<uint64_t>(config.blockSyncBytes), "<bytes>")
            ("rpc-bind-ip", "Interface IP address for the RPC service", cxxopts::value<std::string>(config.rpcInterface), "<ip>")
            ("rpc-bind-port", "TCP port for the RPC service", cxxopts::value<int>(config.rpcPort), "#");

        options.add_options("Peer")
            ("add-exclusive-node", "Manually add a peer to the local peer list ONLY attempt connections to it. [ip:port]", cxxopts::value<std::vector<std::string>>(config.exclusiveNodes), "<ip:port>")
            ("add-peer", "Manually add a peer to the local peer list", cxxopts::value<std::vector<std::string>>(config.peers), "<ip:port>")
            ("add-priority-node", "Manually add a peer to the local peer list and attempt to maintain a connection to it [ip:port]", cxxopts::value<std::vector<std::string>>(config.priorityNodes), "<ip:port>")
            ("seed-node", "Connect to a node to retrieve the peer list and then disconnect", cxxopts::value<std::vector<std::string>>(config.seedNodes), "<host:port>");

        const std::string maxOpenFiles =
            "(default: " + std::to_string(CryptoNote::ROCKSDB_MAX_OPEN_FILES) + ")";

        const std::string readCache =
            "(default: " + std::to_string(CryptoNote::ROCKSDB_READ_BUFFER_MB) + ")";

        const std::string writeBuffer =
            "(default: " + std::to_string(CryptoNote::ROCKSDB_WRITE_BUFFER_MB) + ")";

        options.add_options("Database")
            ("db-enable-compression", "Enable database compression", cxxopts::value<bool>(config.enableDbCompression)->default_value(config.enableDbCompression ? "true" : "false"))
            ("db-max-open-files", "Number of files that can be used by the database at one time " + maxOpenFiles, cxxopts::value<int>())
            ("db-read-buffer-size", "Size of the database read cache in megabytes (MB) " + readCache, cxxopts::value<int>())
            ("db-threads", "Number of background threads used for compaction and flush operations (RocksDB only) (default: number of CPU cores, currently " + std::to_string(config.dbThreads) + ")", cxxopts::value<int>(config.dbThreads))
            ("db-write-buffer-size", "Size of the database write buffer in megabytes (MB) " + writeBuffer, cxxopts::value<int>())
            ("db-optimize", "Optimize database and close", cxxopts::value<bool>(config.dbOptimize));

        options.add_options("Syncing")
            ("transaction-validation-threads", "Number of threads to use to validate a transaction's inputs in parallel.", cxxopts::value<uint32_t>(config.transactionValidationThreads));

        // clang-format on

        try
        {
            const auto cli = options.parse(argc, argv);

            if (cli.count("rewind-to-height") > 0 && config.rewindToHeight == 0)
            {
                std::cout << CryptoNote::getProjectCLIHeader()
                          << "Please use the `--resync` option instead of `--rewind-to-height 0` to completely "
                             "reset the synchronization state."
                          << std::endl;
                exit(1);
            }

            if (cli.count("sync-from-height") > 0 && config.syncFromHeight > 0)
            {
                /* Verify the height has a bootstrap checkpoint entry. */
                bool found = false;
                for (size_t i = 0; i < CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS_COUNT; ++i)
                {
                    if (CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS[i].height == config.syncFromHeight)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    std::cout << CryptoNote::getProjectCLIHeader()
                              << "Error: --sync-from-height=" << config.syncFromHeight
                              << " has no bootstrap checkpoint entry in SyncBootstrapCheckpoints.h.\n"
                              << "Available heights: ";
                    for (size_t i = 0; i < CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS_COUNT; ++i)
                    {
                        if (i > 0) std::cout << ", ";
                        std::cout << CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS[i].height;
                    }
                    std::cout << "\nYou can add a new entry by running: export_bootstrap_state <height>\n"
                              << "on a fully-synced node and updating src/config/SyncBootstrapCheckpoints.h."
                              << std::endl;
                    exit(1);
                }
            }

            if (cli.count("max-export-blocks") > 0 && config.exportNumBlocks == 0)
            {
                std::cout << CryptoNote::getProjectCLIHeader() << "`--max-export-blocks` can not be 0." << std::endl;
                exit(1);
            }

            config.dbMaxOpenFiles = cli.count("db-max-open-files") > 0 ? cli["db-max-open-files"].as<int>()
                                                                       : CryptoNote::ROCKSDB_MAX_OPEN_FILES;
            config.dbReadCacheSizeMB = cli.count("db-read-buffer-size") > 0 ? cli["db-read-buffer-size"].as<int>()
                                                                            : CryptoNote::ROCKSDB_READ_BUFFER_MB;
            config.dbWriteBufferSizeMB = cli.count("db-write-buffer-size") > 0
                                           ? cli["db-write-buffer-size"].as<int>()
                                           : CryptoNote::ROCKSDB_WRITE_BUFFER_MB;

            if (cli.count("daemon-mode") > 0)
            {
                config.daemonMode = normalizeDaemonMode(cli["daemon-mode"].as<std::string>());
            }

            if (cli.count("prune-depth") > 0)
            {
                config.pruneDepth = clampPruneDepth(cli["prune-depth"].as<uint32_t>(), "CLI");
            }

            if (config.help) // Do we want to display the help message?
            {
                std::cout << options.help() << std::endl;
                exit(0);
            }

            if (config.version) // Do we want to display the software version?
            {
                std::cout << CryptoNote::getProjectCLIHeader() << std::endl;
                exit(0);
            }

            if (config.osVersion) // Do we want to display the OS version information?
            {
                std::cout << CryptoNote::getProjectCLIHeader() << "OS: " << Tools::get_os_version_string() << std::endl;
                exit(0);
            }
        }
        catch (const cxxopts::exceptions::exception &e)
        {
            std::cout << "Error: Unable to parse command line argument options: " << e.what() << std::endl
                      << std::endl
                      << options.help() << std::endl;
            exit(1);
        }
    }

    void handleSettings(const std::string &configFile, DaemonConfiguration &config)
    {
        std::ifstream data(configFile);

        if (!data.good())
        {
            throw std::runtime_error(
                "The --config-file you specified does not exist, please check the filename and try again.");
        }

        IStreamWrapper isw(data);

        Document j;
        j.ParseStream(isw);

        // Daemon Options

        if (j.HasMember("data-dir"))
        {
            config.dataDirectory = j["data-dir"].GetString();
        }

        if (j.HasMember("load-checkpoints"))
        {
            config.checkPoints = j["load-checkpoints"].GetString();
        }

        if (j.HasMember("log-file"))
        {
            config.logFile = j["log-file"].GetString();
        }

        if (j.HasMember("log-level"))
        {
            config.logLevel = j["log-level"].GetInt();
        }

        if (j.HasMember("no-console"))
        {
            config.noConsole = j["no-console"].GetBool();
        }

        // RPC Options

        if (j.HasMember("enable-cors"))
        {
            config.enableCors = j["enable-cors"].GetString();
        }

        if (j.HasMember("fee-address"))
        {
            config.feeAddress = j["fee-address"].GetString();
        }

        if (j.HasMember("fee-amount"))
        {
            config.feeAmount = j["fee-amount"].GetInt();
        }

        if (j.HasMember("daemon-mode"))
        {
            config.daemonMode = normalizeDaemonMode(j["daemon-mode"].GetString());
        }
        else if (j.HasMember("enable-blockexplorer-detailed") && j["enable-blockexplorer-detailed"].GetBool())
        {
            config.daemonMode = DaemonConfiguration::DAEMON_MODE_EXPLORER;
        }
        else if (j.HasMember("enable-blockexplorer") && j["enable-blockexplorer"].GetBool())
        {
            config.daemonMode = DaemonConfiguration::DAEMON_MODE_EXPLORER;
        }

        // Mining Options

        if (j.HasMember("stratum-bind-ip"))
        {
            config.stratumBindIp = j["stratum-bind-ip"].GetString();
        }

        if (j.HasMember("stratum-bind-port"))
        {
            config.stratumBindPort = static_cast<uint16_t>(j["stratum-bind-port"].GetUint());
        }

        if (j.HasMember("stratum-share-difficulty"))
        {
            config.stratumShareDifficulty = j["stratum-share-difficulty"].GetUint64();
        }

        if (j.HasMember("stratum-max-connections"))
        {
            config.stratumMaxConnections = j["stratum-max-connections"].GetUint64();
        }

        if (j.HasMember("lite"))
        {
            config.lite = j["lite"].GetBool();
        }

        if (j.HasMember("lite-height"))
        {
            config.liteHeight = j["lite-height"].GetUint();
        }

        if (j.HasMember("sync-batch-min"))
        {
            config.syncBatchMin = j["sync-batch-min"].GetUint();
        }

        if (j.HasMember("sync-batch-max"))
        {
            config.syncBatchMax = j["sync-batch-max"].GetUint();
        }

        if (j.HasMember("block-sync-bytes"))
        {
            config.blockSyncBytes = j["block-sync-bytes"].GetUint64();
        }

        if (j.HasMember("out-peers"))
        {
            config.outPeers = j["out-peers"].GetUint();
        }

        if (j.HasMember("in-peers"))
        {
            config.inPeers = j["in-peers"].GetUint();
        }

        if (j.HasMember("rpc-ipc-path"))
        {
            config.rpcIpcPath = j["rpc-ipc-path"].GetString();
        }

        if (j.HasMember("rpc-ipc-mode"))
        {
            config.rpcIpcMode = j["rpc-ipc-mode"].GetString();
        }

        if (j.HasMember("rpc-ipc-group"))
        {
            config.rpcIpcGroup = j["rpc-ipc-group"].GetString();
        }

        // Notification Options

        if (j.HasMember("block-notify"))
        {
            config.blockNotify = j["block-notify"].GetString();
        }

        if (j.HasMember("reorg-notify"))
        {
            config.reorgNotify = j["reorg-notify"].GetString();
        }

        if (j.HasMember("tx-notify"))
        {
            config.txNotify = j["tx-notify"].GetString();
        }

        if (j.HasMember("notify-during-sync"))
        {
            config.notifyDuringSync = j["notify-during-sync"].GetBool();
        }

        // Network Options

        if (j.HasMember("allow-local-ip"))
        {
            config.localIp = j["allow-local-ip"].GetBool();
        }

        if (j.HasMember("hide-my-port"))
        {
            config.hideMyPort = j["hide-my-port"].GetBool();
        }

        if (j.HasMember("p2p-bind-ip"))
        {
            config.p2pInterface = j["p2p-bind-ip"].GetString();
        }

        if (j.HasMember("p2p-bind-port"))
        {
            config.p2pPort = j["p2p-bind-port"].GetInt();
        }

        if (j.HasMember("p2p-external-port"))
        {
            config.p2pExternalPort = j["p2p-external-port"].GetInt();
        }

        if (j.HasMember("p2p-reset-peerstate"))
        {
            config.p2pResetPeerstate = j["p2p-reset-peerstate"].GetBool();
        }

        if (j.HasMember("rpc-bind-ip"))
        {
            config.rpcInterface = j["rpc-bind-ip"].GetString();
        }

        if (j.HasMember("rpc-bind-port"))
        {
            config.rpcPort = j["rpc-bind-port"].GetInt();
        }

        // Peer Options

        if (j.HasMember("add-exclusive-node"))
        {
            const Value &va = j["add-exclusive-node"];

            for (auto &v : va.GetArray())
            {
                config.exclusiveNodes.emplace_back(v.GetString());
            }
        }

        if (j.HasMember("add-peer"))
        {
            const Value &va = j["add-peer"];

            for (auto &v : va.GetArray())
            {
                config.peers.emplace_back(v.GetString());
            }
        }

        if (j.HasMember("add-priority-node"))
        {
            const Value &va = j["add-priority-node"];

            for (auto &v : va.GetArray())
            {
                config.priorityNodes.emplace_back(v.GetString());
            }
        }

        if (j.HasMember("seed-node"))
        {
            const Value &va = j["seed-node"];

            for (auto &v : va.GetArray())
            {
                config.seedNodes.emplace_back(v.GetString());
            }
        }

        // Database Options
        config.dbMaxOpenFiles = CryptoNote::ROCKSDB_MAX_OPEN_FILES;
        config.dbReadCacheSizeMB = CryptoNote::ROCKSDB_READ_BUFFER_MB;
        config.dbWriteBufferSizeMB = CryptoNote::ROCKSDB_WRITE_BUFFER_MB;
        config.dbThreads = CryptoNote::ROCKSDB_BACKGROUND_THREADS;

        if (j.HasMember("db-enable-compression"))
        {
            config.enableDbCompression = j["db-enable-compression"].GetBool();
        }

        if (j.HasMember("db-max-open-files"))
        {
            config.dbMaxOpenFiles = j["db-max-open-files"].GetInt();
        }

        if (j.HasMember("db-read-buffer-size"))
        {
            config.dbReadCacheSizeMB = j["db-read-buffer-size"].GetInt();
        }

        if (j.HasMember("db-threads"))
        {
            config.dbThreads = j["db-threads"].GetInt();
        }

        if (j.HasMember("db-write-buffer-size"))
        {
            config.dbWriteBufferSizeMB = j["db-write-buffer-size"].GetInt();
        }

        if (j.HasMember("prune"))
        {
            config.prune = j["prune"].GetBool();
        }

        if (j.HasMember("background-prune"))
        {
            config.backgroundPrune = j["background-prune"].GetBool();
        }

        if (j.HasMember("prune-depth"))
        {
            config.pruneDepth = clampPruneDepth(j["prune-depth"].GetUint(), "config file");
        }

        // Syncing Options

        if (j.HasMember("transaction-validation-threads"))
        {
            config.transactionValidationThreads = j["transaction-validation-threads"].GetInt();
        }

        if (j.HasMember("sync-from-height"))
        {
            config.syncFromHeight = j["sync-from-height"].GetUint();
        }
    }

    Document asJSON(const DaemonConfiguration &config)
    {
        Document j;
        Document::AllocatorType &alloc = j.GetAllocator();

        j.SetObject();

        j.AddMember("data-dir", config.dataDirectory, alloc);
        j.AddMember("load-checkpoints", config.checkPoints, alloc);
        j.AddMember("log-file", config.logFile, alloc);
        j.AddMember("log-level", config.logLevel, alloc);
        j.AddMember("no-console", config.noConsole, alloc);

        j.AddMember("daemon-mode", config.daemonMode, alloc);
        j.AddMember("enable-cors", config.enableCors, alloc);
        j.AddMember("fee-address", config.feeAddress, alloc);
        j.AddMember("fee-amount", config.feeAmount, alloc);

        j.AddMember("lite", config.lite, alloc);
        j.AddMember("lite-height", config.liteHeight, alloc);

        j.AddMember("sync-batch-min", config.syncBatchMin, alloc);
        j.AddMember("sync-batch-max", config.syncBatchMax, alloc);
        j.AddMember("block-sync-bytes", config.blockSyncBytes, alloc);

        j.AddMember("out-peers", config.outPeers, alloc);
        j.AddMember("in-peers", config.inPeers, alloc);

        j.AddMember("stratum-bind-ip", config.stratumBindIp, alloc);
        j.AddMember("stratum-bind-port", config.stratumBindPort, alloc);
        j.AddMember("stratum-share-difficulty", config.stratumShareDifficulty, alloc);
        j.AddMember("stratum-max-connections", static_cast<uint64_t>(config.stratumMaxConnections), alloc);

        j.AddMember("rpc-ipc-path", config.rpcIpcPath, alloc);
        j.AddMember("rpc-ipc-mode", config.rpcIpcMode, alloc);
        j.AddMember("rpc-ipc-group", config.rpcIpcGroup, alloc);

        j.AddMember("block-notify", config.blockNotify, alloc);
        j.AddMember("reorg-notify", config.reorgNotify, alloc);
        j.AddMember("tx-notify", config.txNotify, alloc);
        j.AddMember("notify-during-sync", config.notifyDuringSync, alloc);

        j.AddMember("allow-local-ip", config.localIp, alloc);
        j.AddMember("hide-my-port", config.hideMyPort, alloc);
        j.AddMember("p2p-bind-ip", config.p2pInterface, alloc);
        j.AddMember("p2p-bind-port", config.p2pPort, alloc);
        j.AddMember("p2p-external-port", config.p2pExternalPort, alloc);
        j.AddMember("p2p-reset-peerstate", config.p2pResetPeerstate, alloc);
        j.AddMember("rpc-bind-ip", config.rpcInterface, alloc);
        j.AddMember("rpc-bind-port", config.rpcPort, alloc);

        {
            Value arr(kArrayType);
            for (const auto &v : config.exclusiveNodes)
            {
                arr.PushBack(Value().SetString(StringRef(v.c_str())), alloc);
            }
            j.AddMember("add-exclusive-node", arr, alloc);
        }

        {
            Value arr(kArrayType);
            for (const auto &v : config.peers)
            {
                arr.PushBack(Value().SetString(StringRef(v.c_str())), alloc);
            }
            j.AddMember("add-peer", arr, alloc);
        }

        {
            Value arr(kArrayType);
            for (const auto &v : config.priorityNodes)
            {
                arr.PushBack(Value().SetString(StringRef(v.c_str())), alloc);
            }
            j.AddMember("add-priority-node", arr, alloc);
        }

        {
            Value arr(kArrayType);
            for (const auto &v : config.seedNodes)
            {
                arr.PushBack(Value().SetString(StringRef(v.c_str())), alloc);
            }
            j.AddMember("seed-node", arr, alloc);
        }

        j.AddMember("db-enable-compression", config.enableDbCompression, alloc);
        j.AddMember("db-max-open-files", config.dbMaxOpenFiles, alloc);
        j.AddMember("db-read-buffer-size", config.dbReadCacheSizeMB, alloc);
        j.AddMember("db-threads", config.dbThreads, alloc);
        j.AddMember("db-write-buffer-size", config.dbWriteBufferSizeMB, alloc);
        j.AddMember("prune", config.prune, alloc);
        j.AddMember("background-prune", config.backgroundPrune, alloc);
        j.AddMember("prune-depth", config.pruneDepth, alloc);

        j.AddMember("transaction-validation-threads", config.transactionValidationThreads, alloc);
        j.AddMember("sync-from-height", config.syncFromHeight, alloc);

        return j;
    }

    std::string asString(const DaemonConfiguration &config)
    {
        StringBuffer stringBuffer;
        PrettyWriter writer(stringBuffer);
        asJSON(config).Accept(writer);
        return stringBuffer.GetString();
    }

    void asFile(const DaemonConfiguration &config, const std::string &filename)
    {
        std::ofstream data(filename);
        OStreamWrapper osw(data);
        PrettyWriter writer(osw);
        asJSON(config).Accept(writer);
    }
} // namespace DaemonConfig
