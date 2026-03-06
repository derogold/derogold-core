// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018, The Karai Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The CyprusCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#include "DaemonCommandsHandler.h"
#include "DaemonConfiguration.h"
#include "common/CryptoNoteTools.h"
#include "common/FileSystemShim.h"
#include "common/PathTools.h"
#include "common/ScopeExit.h"
#include "common/SignalHandler.h"
#include "common/StdOutputStream.h"
#include "common/Util.h"
#include "config/CliHeader.h"
#include "config/CryptoNoteCheckpoints.h"
#include "config/SyncBootstrapCheckpoints.h"
#include "cryptonotecore/Core.h"
#include "cryptonotecore/Currency.h"
#include "cryptonotecore/DBUtils.h"
#include "cryptonotecore/DatabaseBlockchainCache.h"
#include "cryptonotecore/DatabaseBlockchainCacheFactory.h"
#include "cryptonotecore/RocksDBWrapper.h"
#include "cryptonoteprotocol/CryptoNoteProtocolHandler.h"
#include "logger/Logger.h"
#include "logging/LoggerManager.h"
#include "p2p/NetNode.h"
#include "p2p/NetNodeConfig.h"
#include "rpc/RpcServer.h"

#if defined(WIN32)
    #undef ERROR
    #include <crtdbg.h>
#else
    #include <unistd.h>
#endif
#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <thread>

using Common::JsonValue;
using namespace CryptoNote;
using namespace Logging;
using namespace DaemonConfig;

void print_genesis_tx_hex(const bool blockExplorerMode, const std::shared_ptr<LoggerManager> &logManager)
{
    CryptoNote::CurrencyBuilder currencyBuilder(logManager);
    currencyBuilder.isBlockexplorer(blockExplorerMode);

    CryptoNote::Currency currency = currencyBuilder.currency();

    const auto transaction = CryptoNote::CurrencyBuilder(logManager).generateGenesisTransaction();

    std::string transactionHex = Common::toHex(CryptoNote::toBinaryArray(transaction));
    std::cout << getProjectCLIHeader() << std::endl
              << std::endl
              << "Replace the current GENESIS_COINBASE_TX_HEX line in src/config/CryptoNoteConfig.h with this one:"
              << std::endl
              << "const char GENESIS_COINBASE_TX_HEX[] = \"" << transactionHex << "\";" << std::endl;
}

JsonValue buildLoggerConfiguration(const Level level, const std::string &logfile)
{
    JsonValue loggerConfiguration(JsonValue::OBJECT);
    loggerConfiguration.insert("globalLevel", static_cast<int64_t>(level));

    JsonValue &cfgLoggers = loggerConfiguration.insert("loggers", JsonValue::ARRAY);

    JsonValue &fileLogger = cfgLoggers.pushBack(JsonValue::OBJECT);
    fileLogger.insert("type", "file");
    fileLogger.insert("filename", logfile);
    fileLogger.insert("level", static_cast<int64_t>(TRACE));

    JsonValue &consoleLogger = cfgLoggers.pushBack(JsonValue::OBJECT);
    consoleLogger.insert("type", "console");
    consoleLogger.insert("level", static_cast<int64_t>(TRACE));
    consoleLogger.insert("pattern", "%D %T %L ");

    return loggerConfiguration;
}

int main(int argc, char *argv[])
{
    fs::path temp = fs::path(argv[0]).filename();
    DaemonConfiguration config = initConfiguration(temp.string().c_str());

#ifdef WIN32
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // Initial loading of CLI parameters
    handleSettings(argc, argv, config);

    // If the user passed in the --config-file option, we need to handle that first
    if (!config.configFile.empty())
    {
        try
        {
            handleSettings(config.configFile, config);
        }
        catch (std::exception &e)
        {
            std::cout
                << std::endl
                << "There was an error parsing the specified configuration file. Please check the file and try again"
                << std::endl
                << e.what() << std::endl;
            exit(1);
        }
    }

    // Load in the CLI specified parameters again to overwrite anything from the config file
    handleSettings(argc, argv, config);

    const auto logManager = std::make_shared<LoggerManager>();
    LoggerRef logger(logManager, "daemon");

    if (config.printGenesisTx) // Do we want to generate the Genesis Tx?
    {
        print_genesis_tx_hex(false, logManager);
        return 0;
    }

    if (config.dumpConfig)
    {
        std::cout << getProjectCLIHeader() << asString(config) << std::endl;
        return 0;
    }

    if (!config.outputFile.empty())
    {
        try
        {
            asFile(config, config.outputFile);
            std::cout << getProjectCLIHeader() << "Configuration saved to: " << config.outputFile << std::endl;
            return 0;
        }
        catch (std::exception &e)
        {
            std::cout << getProjectCLIHeader() << "Could not save configuration to: " << config.outputFile << std::endl
                      << e.what() << std::endl;
            exit(1);
        }
    }

    /* If we were given the resync arg, we're deleting everything */
    if (config.resync)
    {
        std::error_code ec;

        std::vector removablePaths = {
            fs::path(config.dataDirectory) / CryptoNote::parameters::P2P_NET_DATA_FILENAME,
            fs::path(config.dataDirectory) / RocksDBWrapper::DB_NAME,
        };

        for (const auto &path : removablePaths)
        {
            fs::remove_all(path, ec);

            if (ec)
            {
                std::cout << "Could not delete data path: " << path << std::endl;
                exit(1);
            }
        }
    }

    if (config.p2pPort <= 1024 || config.p2pPort > 65535)
    {
        std::cout << "P2P Port must be between 1024 and 65,535" << std::endl;
        return 1;
    }

    if (config.p2pExternalPort < 0 || config.p2pExternalPort > 65535)
    {
        std::cout << "P2P External Port must be between 0 and 65,535" << std::endl;
        return 1;
    }

    if (config.rpcPort <= 1024 || config.rpcPort > 65535)
    {
        std::cout << "RPC Port must be between 1024 and 65,535" << std::endl;
        return 1;
    }

    try
    {
        fs::path cwdPath = fs::current_path();
        auto modulePath = cwdPath / temp;
        auto cfgLogFile = fs::path(config.logFile);

        if (cfgLogFile.empty())
        {
            cfgLogFile = modulePath.replace_extension(".log");
        }
        else
        {
            if (!cfgLogFile.has_parent_path())
            {
                cfgLogFile = modulePath.parent_path() / cfgLogFile;
            }
        }

        auto cfgLogLevel = static_cast<Level>(static_cast<int>(Logging::ERROR) + config.logLevel);

        // configure logging
        logManager->configure(buildLoggerConfiguration(cfgLogLevel, cfgLogFile.string()));

        Logger::logger.setLogLevel(Logger::DEBUG);

        /* New logger, for now just passing through messages to old logger */
        Logger::logger.setLogCallback(
            [&logger](const std::string prettyMessage,
                      const std::string message,
                      const Logger::LogLevel level,
                      const std::vector<Logger::LogCategory> categories)
            {
                Logging::Level oldLogLevel;
                std::string logColour;

                if (level == Logger::DEBUG)
                {
                    oldLogLevel = Logging::DEBUGGING;
                    logColour = Logging::DEFAULT;
                }
                else if (level == Logger::INFO)
                {
                    oldLogLevel = Logging::INFO;
                    logColour = Logging::DEFAULT;
                }
                else if (level == Logger::WARNING)
                {
                    oldLogLevel = Logging::WARNING;
                    logColour = Logging::RED;
                }
                else if (level == Logger::FATAL)
                {
                    oldLogLevel = Logging::FATAL;
                    logColour = Logging::RED;
                }
                /* setLogCallback shouldn't get called if log level is DISABLED */
                else
                {
                    throw std::runtime_error("Programmer error @ setLogCallback in Daemon.cpp");
                }

                logger(oldLogLevel, logColour) << message;
            });

        logger(INFO, BRIGHT_GREEN) << getProjectCLIHeader() << std::endl;

        logger(INFO) << "Program Working Directory: " << cwdPath;

        // create objects and link them
        CryptoNote::CurrencyBuilder currencyBuilder(logManager);
        const bool explorerMode = config.daemonMode == DaemonConfiguration::DAEMON_MODE_EXPLORER;
        currencyBuilder.isBlockexplorer(explorerMode);

        try
        {
            currencyBuilder.currency();
        }
        catch (std::exception &)
        {
            std::cout << "GENESIS_COINBASE_TX_HEX constant has an incorrect value. Please launch: "
                      << CryptoNote::CRYPTONOTE_NAME << "d --print-genesis-tx" << std::endl;
            return 1;
        }
        CryptoNote::Currency currency = currencyBuilder.currency();

        bool use_checkpoints = !config.checkPoints.empty();
        CryptoNote::Checkpoints checkpoints(logManager);

        if (use_checkpoints)
        {
            logger(INFO) << "Loading Checkpoints for faster initial sync...";
            if (config.checkPoints == "default")
            {
                for (const auto &cp : CryptoNote::CHECKPOINTS)
                {
                    checkpoints.addCheckpoint(cp.index, cp.blockId);
                }

                logger(INFO) << "Loaded " << std::size(CryptoNote::CHECKPOINTS) << " default checkpoints";
            }
            else
            {
                bool results = checkpoints.loadCheckpointsFromFile(config.checkPoints);
                if (!results)
                {
                    throw std::runtime_error("Failed to load checkpoints");
                }
            }
        }

        NetNodeConfig netNodeConfig;
        netNodeConfig.init(config.p2pInterface,
                           config.p2pPort,
                           config.p2pExternalPort,
                           config.localIp,
                           config.hideMyPort,
                           config.dataDirectory,
                           config.peers,
                           config.exclusiveNodes,
                           config.priorityNodes,
                           config.seedNodes,
                           config.p2pResetPeerstate);

        DataBaseConfig dbConfig(config.dataDirectory,
                                config.dbThreads,
                                config.dbMaxOpenFiles,
                                config.dbWriteBufferSizeMB,
                                config.dbReadCacheSizeMB,
                                CryptoNote::LEVELDB_MAX_FILE_SIZE_MB,
                                config.enableDbCompression,
                                false);

        if (!Tools::create_directories_if_necessary(dbConfig.dataDir))
        {
            throw std::runtime_error("Can't create directory: " + dbConfig.dataDir);
        }

        std::shared_ptr<IDataBase> database;

        database = std::make_shared<RocksDBWrapper>(logManager, dbConfig);

        if (config.dbOptimize)
        {
            database->optimize();
            return 0;
        }

        database->init();
        Tools::ScopeExit dbShutdownOnExit([&database] { database->shutdown(); });

        if (!DatabaseBlockchainCache::checkDBSchemeVersion(*database, logManager))
        {
            dbShutdownOnExit.cancel();

            database->shutdown();
            database->destroy();
            database->init();

            dbShutdownOnExit.resume();
        }

        System::Dispatcher dispatcher;
        logger(INFO) << "Initializing core...";

        const auto ccore = std::make_shared<CryptoNote::Core>(
            currency,
            logManager,
            std::move(checkpoints),
            dispatcher,
            std::unique_ptr<IBlockchainCacheFactory>(
                std::make_unique<DatabaseBlockchainCacheFactory>(*database, logger.getLogger())),
            config.transactionValidationThreads);

        ccore->load();

        logger(INFO) << "Core initialized OK";

        /* --sync-from-height bootstrap ------------------------------------------ */
        if (config.syncFromHeight > 0)
        {
            const uint32_t existingSyncFloor = ccore->getSyncFloorHeight();
            const uint32_t topIndex          = ccore->getTopBlockIndex();

            if (existingSyncFloor > 0)
            {
                logger(INFO) << "Resuming bootstrapped node: sync floor is already at height "
                             << existingSyncFloor << ", chain top at " << topIndex << ".";
            }
            else if (topIndex > 0)
            {
                logger(WARNING)
                    << "--sync-from-height=" << config.syncFromHeight
                    << " was requested but the database already contains " << (topIndex + 1)
                    << " blocks.  Ignoring bootstrap injection (use --resync first for a "
                       "clean start from height " << config.syncFromHeight << ").";
            }
            else
            {
                /* Fresh database (only genesis).  Find the matching bootstrap entry. */
                const CryptoNote::BootstrapCheckpoint *entry = nullptr;
                for (size_t i = 0; i < CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS_COUNT; ++i)
                {
                    if (CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS[i].height == config.syncFromHeight)
                    {
                        entry = &CryptoNote::SYNC_BOOTSTRAP_CHECKPOINTS[i];
                        break;
                    }
                }

                if (entry == nullptr)
                {
                    logger(ERROR) << "No bootstrap checkpoint found for height "
                                  << config.syncFromHeight
                                  << " – cannot bootstrap. Exiting.";
                    return 1;
                }

                /* Parse the hex block hash from the bootstrap entry. */
                Crypto::Hash anchorHash;
                if (!Common::podFromHex(std::string(entry->blockHash), anchorHash))
                {
                    logger(ERROR)
                        << "Bootstrap checkpoint for height " << entry->height
                        << " has an invalid block hash: " << entry->blockHash;
                    return 1;
                }

                logger(INFO)
                    << "Bootstrapping node from height " << entry->height
                    << " (hash " << entry->blockHash << ") ...";

                /* Use the block timestamp from the checkpoint height's hash to
                   pick a reasonable timestamp.  We approximate it by using the
                   current time minus the expected block time since that height.
                   The anchor's exact timestamp is not critical for correctness
                   (it only affects difficulty calculation which transitions into
                   real block data once the anchor height + 1 is synced). */
                const uint64_t anchorTimestamp =
                    static_cast<uint64_t>(std::time(nullptr));

                ccore->bootstrapFromHeight(
                    entry->height,
                    anchorHash,
                    anchorTimestamp,
                    entry->alreadyGeneratedCoins,
                    entry->cumulativeDifficulty,
                    entry->alreadyGeneratedTransactions);

                logger(INFO)
                    << "Bootstrap complete. Node will sync from height "
                    << entry->height << " onwards.";
            }
        }
        /* ----------------------------------------------------------------------- */

        std::string error;
        std::string filepath = "blockchain.dump";

        auto startTimer = std::chrono::high_resolution_clock::now();
        auto elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;

        if (config.importChain)
        {
            constexpr bool performExpensiveValidation = false;
            logger(INFO) << "Importing blockchain...";
            error = ccore->importBlockchain(filepath, performExpensiveValidation);
            elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;
            if (!error.empty())
            {
                logger(ERROR) << "Failed to import blockchain: " << error;
                exit(1);
            }
            else
            {
                std::cout << "Time to import " << std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count()
                          << " seconds." << std::endl
                          << std::endl;
                exit(0);
            }
        }

        if (config.exportChain)
        {
            logger(INFO) << "Exporting blockchain...";
            error = ccore->exportBlockchain(filepath, config.exportNumBlocks);
            elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;
            if (error != "")
            {
                logger(ERROR) << "Failed to export "
                              << "blockchain: " << error;
                exit(1);
            }
            else
            {
                std::cout << "Time to export " << std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count()
                          << " seconds." << std::endl
                          << std::endl;
                exit(0);
            }
        }

        /* If we were told to rewind the blockchain to a certain height
           we will remove blocks until we're back at the height specified */
        if (config.rewindToHeight > 0)
        {
            logger(INFO) << "Rewinding blockchain to: " << config.rewindToHeight << std::endl;

            ccore->rewind(config.rewindToHeight);
        }

        if (config.prune)
        {
            logger(INFO) << "Prune DB mode enabled with depth " << config.pruneDepth << ".";

            /* One-shot startup prune: delete raw blocks below the prune floor right
             * now so that the DB reflects the configured depth immediately on launch,
             * even when --background-prune is off. */
            const uint64_t startupHeight = ccore->getTopBlockIndex() + 1;
            const uint32_t startupFloor = startupHeight > config.pruneDepth
                                              ? static_cast<uint32_t>(startupHeight - config.pruneDepth)
                                              : 0;
            if (startupFloor > ccore->getPruneFloor())
            {
                logger(INFO) << "Startup prune: removing raw blocks below height " << startupFloor << ".";
                try
                {
                    ccore->pruneRawBlocksBefore(startupFloor);
                }
                catch (const std::exception &e)
                {
                    logger(WARNING) << "Startup prune failed: " << e.what();
                }
            }
        }
        else
        {
            logger(INFO) << "Prune DB mode disabled.";
        }

        if (config.backgroundPrune)
        {
            logger(INFO) << "Background prune task enabled (depth " << config.pruneDepth << ").";
        }

        const auto cprotocol =
            std::make_shared<CryptoNote::CryptoNoteProtocolHandler>(currency, dispatcher, *ccore, nullptr, logManager);

        const auto p2psrv = std::make_shared<CryptoNote::NodeServer>(dispatcher, *cprotocol, logManager);

        RpcMode rpcMode = explorerMode ? RpcMode::BlockExplorerEnabled : RpcMode::Default;

        RpcServer rpcServer(config.rpcPort,
                            config.rpcInterface,
                            config.enableCors,
                            config.feeAddress,
                            config.feeAmount,
                            rpcMode,
                            ccore,
                            p2psrv,
                            cprotocol,
                            config.enableTrtlRpc);

        cprotocol->set_p2p_endpoint(&*p2psrv);
        logger(INFO) << "Initializing p2p server...";
        if (!p2psrv->init(netNodeConfig))
        {
            logger(ERROR, BRIGHT_RED) << "Failed to initialize p2p server.";
            return 1;
        }

        logger(INFO) << "P2p server initialized OK";

        // Fire up the RPC Server
        logger(INFO) << "Starting core rpc server on address " << config.rpcInterface << ":" << config.rpcPort;

        rpcServer.start();

        /* Get the RPC IP address and port we are bound to */
        auto [ip, port] = rpcServer.getConnectionInfo();

        /* If we bound the RPC to 0.0.0.0, we can't reach that with a
           standard HTTP client from anywhere. Instead, let's use the
           localhost IP address to reach ourselves */
        if (ip == "0.0.0.0")
        {
            ip = "127.0.0.1";
        }

        DaemonCommandsHandler dch(*ccore, *p2psrv, cprotocol, logManager, ip, port, database, config);

        if (!config.noConsole)
        {
            dch.start_handling();
        }

        std::atomic<bool> stopPruneWorker(false);
        std::thread pruneWorker;
        Tools::ScopeExit stopPruneWorkerOnExit([&]
                                               {
                                                   stopPruneWorker = true;
                                                   if (pruneWorker.joinable())
                                                   {
                                                       pruneWorker.join();
                                                   }
                                               });

        if (config.backgroundPrune)
        {
            constexpr auto prunePassInterval = std::chrono::seconds(60);
            constexpr auto prunePollInterval = std::chrono::seconds(1);

            pruneWorker = std::thread([&, prunePassInterval, prunePollInterval]
                                      {
                                          auto nextRun = std::chrono::steady_clock::now() + prunePassInterval;
                                          std::future<void> prunePassTask;

                                          while (!stopPruneWorker)
                                          {
                                              if (prunePassTask.valid()
                                                  && prunePassTask.wait_for(std::chrono::seconds(0))
                                                         != std::future_status::ready)
                                              {
                                                  std::this_thread::sleep_for(prunePollInterval);
                                                  continue;
                                              }

                                              if (std::chrono::steady_clock::now() < nextRun)
                                              {
                                                  std::this_thread::sleep_for(prunePollInterval);
                                                  continue;
                                              }

                                              prunePassTask = std::async(std::launch::async,
                                                                         [&, depth = config.pruneDepth]
                                                                         {
                                                                             logger(INFO)
                                                                                 << "Starting periodic prune pass in "
                                                                                    "background (depth "
                                                                                 << depth << ").";

                                                                             uint32_t pruneFloor = 0;
                                                                             try
                                                                             {
                                                                                 const uint64_t height =
                                                                                     ccore->getTopBlockIndex() + 1;
                                                                                 pruneFloor =
                                                                                     height > depth
                                                                                         ? static_cast<uint32_t>(
                                                                                               height - depth)
                                                                                         : 0;
                                                                             }
                                                                             catch (const std::exception &e)
                                                                             {
                                                                                 logger(WARNING)
                                                                                     << "Prune pass: failed to get "
                                                                                        "chain height: "
                                                                                     << e.what();
                                                                                 return;
                                                                             }

                                                                             if (pruneFloor > 0)
                                                                             {
                                                                                 try
                                                                                 {
                                                                                     ccore->pruneRawBlocksBefore(
                                                                                         pruneFloor);
                                                                                 }
                                                                                 catch (const std::exception &e)
                                                                                 {
                                                                                     logger(WARNING)
                                                                                         << "Prune pass failed: "
                                                                                         << e.what();
                                                                                     return;
                                                                                 }
                                                                             }

                                                                             logger(INFO)
                                                                                 << "Periodic prune pass completed. "
                                                                                    "Prune floor now at: "
                                                                                 << pruneFloor;
                                                                         });

                                              nextRun = std::chrono::steady_clock::now() + prunePassInterval;
                                          }

                                          if (prunePassTask.valid())
                                          {
                                              prunePassTask.wait();
                                          }
                                      });
        }

        Tools::SignalHandler::install(
            [&dch]
            {
                dch.exit({});
                dch.stop_handling();
            });

        logger(INFO) << "Starting p2p net loop...";
        p2psrv->run();
        logger(INFO) << "p2p net loop stopped";

        dch.stop_handling();

        // stop components
        logger(INFO) << "Stopping core rpc server...";
        rpcServer.stop();

        // deinitialize components
        logger(INFO) << "Deinitializing p2p...";
        p2psrv->deinit();

        cprotocol->set_p2p_endpoint(nullptr);
        ccore->save();
    }
    catch (const std::exception &e)
    {
        logger(ERROR, BRIGHT_RED) << "Exception: " << e.what();
        return 1;
    }

    logger(INFO) << "Node stopped.";
    return 0;
}
