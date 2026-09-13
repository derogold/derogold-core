// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018, The Karai Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The CyprusCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#include "AttachConsole.h"
#include "ChainNotifier.h"
#include "StratumServer.h"
#include "DaemonCommandsHandler.h"
#include "DaemonConfiguration.h"
#include "common/CryptoNoteTools.h"
#include "common/IpcSocket.h"
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
    #include <sys/resource.h>
    #include <unistd.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <future>
#include <thread>

using Common::JsonValue;
using namespace CryptoNote;
using namespace Logging;
using namespace DaemonConfig;


namespace
{
    /* Records how this database was built, so a later run cannot silently treat
       an index-only chain as a complete one. Value is "lite:<height>".
       See LITENODE.md. */
    const std::string LITE_PROFILE_KEY = "lite_node_profile";

    /* Written by DatabaseBlockchainCache the first time a database is opened.
       Its absence is what tells us a database is brand new, which is the only
       point at which lite mode may be chosen. Must match DB_VERSION_KEY in
       DatabaseBlockchainCache.cpp. */
    const std::string DB_SCHEME_VERSION_KEY = "db_scheme_version";

    class StringSettingReadBatch : public IReadBatch
    {
      public:
        explicit StringSettingReadBatch(std::string key): key(std::move(key)) {}

        std::vector<std::string> getRawKeys() const override
        {
            return {key};
        }

        void submitRawResult(const std::vector<std::string> &values, const std::vector<bool> &states) override
        {
            if (values.size() != 1 || states.size() != 1 || !states[0])
            {
                return;
            }

            value = values[0];
        }

        std::optional<std::string> getValue() const
        {
            return value;
        }

      private:
        std::string key;

        std::optional<std::string> value;
    };

    class StringSettingWriteBatch : public IWriteBatch
    {
      public:
        StringSettingWriteBatch(std::string key, std::string value):
            key(std::move(key)),
            value(std::move(value))
        {
        }

        std::vector<std::pair<std::string, std::string>> extractRawDataToInsert() override
        {
            return {std::make_pair(key, value)};
        }

        std::vector<std::string> extractRawKeysToRemove() override
        {
            return {};
        }

      private:
        std::string key;

        std::string value;
    };

    std::optional<std::string> readStringSetting(IDataBase &database, const std::string &key)
    {
        StringSettingReadBatch readBatch(key);

        if (const auto error = database.read(readBatch))
        {
            throw std::system_error(error);
        }

        return readBatch.getValue();
    }

    void writeStringSetting(IDataBase &database, const std::string &key, const std::string &value)
    {
        StringSettingWriteBatch writeBatch(key, value);

        if (const auto error = database.write(writeBatch))
        {
            throw std::system_error(error);
        }
    }

    /* Settles what lite height this database runs at, and refuses to run at all
       when the flags and the database disagree. Whether a chain is stored in
       full or index-only is baked in the moment the first block is written, so
       it can never be changed later - only rebuilt from scratch.

       Every disagreement here exits rather than recreating the database.
       Dropping a chain because an operator forgot a flag would be the worst
       possible reading of their intent, so the removal is always left to them.

       Returns the lite height to build the cache with; 0 means full storage. */
    uint32_t resolveLiteProfile(IDataBase &database, const DaemonConfiguration &config, LoggerRef &logger)
    {
        const auto storedProfile = readStringSetting(database, LITE_PROFILE_KEY);

        /* No scheme version yet means DatabaseBlockchainCache has never opened
           this database, so there is nothing in it to contradict. */
        const bool databaseIsNew = !readStringSetting(database, DB_SCHEME_VERSION_KEY).has_value();

        std::optional<uint32_t> storedLiteHeight;

        if (storedProfile && storedProfile->rfind("lite:", 0) == 0)
        {
            try
            {
                storedLiteHeight = static_cast<uint32_t>(std::stoul(storedProfile->substr(5)));
            }
            catch (const std::exception &)
            {
                logger(ERROR, BRIGHT_RED)
                    << "The lite-node marker in this database is unreadable. Refusing to start rather than guess "
                       "how it was built. Remove the data directory to rebuild.";
                exit(1);
            }
        }

        if (!config.lite)
        {
            if (storedLiteHeight)
            {
                logger(ERROR, BRIGHT_RED)
                    << "This database was built as a lite node from height " << *storedLiteHeight
                    << ", so it does not hold the block data a full node serves. Restart with --lite --lite-height "
                    << *storedLiteHeight << ", or delete the data directory to sync a full node from scratch.";
                exit(1);
            }

            return 0;
        }

        /* --lite from here down. */
        if (config.liteHeight == 0)
        {
            logger(ERROR, BRIGHT_RED)
                << "--lite requires --lite-height, the height from which full block data is kept. There is no "
                   "sensible default: it decides what this node can never serve or rescan again.";
            exit(1);
        }

        if (config.prune)
        {
            logger(ERROR, BRIGHT_RED)
                << "--lite and --prune cannot be combined. Pruning below the lite height would remove nothing, and "
                   "above it would break the promise a lite node makes to serve every block from its lite height up.";
            exit(1);
        }

        /* Every explorer endpoint reads the transaction records a lite node
           drops, so below the lite height they answer with nothing rather than
           fail. That is a node that looks like it works and quietly reports an
           incomplete chain, which is worse than one that refuses to start. */
        if (config.daemonMode == DaemonConfiguration::DAEMON_MODE_EXPLORER)
        {
            logger(ERROR, BRIGHT_RED)
                << "--lite and --daemon-mode explorer cannot be combined. Block and transaction lookups below the "
                   "lite height need the transaction records a lite node never stores, so the explorer endpoints "
                   "would return nothing for those heights rather than report an error.";
            exit(1);
        }

        if (storedLiteHeight)
        {
            if (*storedLiteHeight != config.liteHeight)
            {
                logger(ERROR, BRIGHT_RED)
                    << "This database was built as a lite node from height " << *storedLiteHeight << ", not "
                    << config.liteHeight
                    << ". The stored height cannot be changed - blocks below it were never written. Restart with "
                       "--lite-height "
                    << *storedLiteHeight << ", or delete the data directory to rebuild at a different height.";
                exit(1);
            }

            return *storedLiteHeight;
        }

        if (!databaseIsNew)
        {
            logger(ERROR, BRIGHT_RED)
                << "--lite can only be chosen for a new database. This one already holds a chain that was synced in "
                   "full, and nothing here will delete it for you. Point --data-dir at an empty directory, or "
                   "remove this one yourself, to build a lite node.";
            exit(1);
        }

        writeStringSetting(database, LITE_PROFILE_KEY, "lite:" + std::to_string(config.liteHeight));

        logger(INFO, BRIGHT_GREEN) << "Lite node mode enabled from height " << config.liteHeight
                                   << ". This is permanent for this database.";

        return config.liteHeight;
    }
} // namespace

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

#if !defined(WIN32)
    /* Raise the open-file-descriptor limit as high as the hard limit allows.
     * RocksDB compaction opens SST files outside the table-cache, so the OS
     * per-process limit must accommodate concurrent compaction FDs in addition
     * to the configured max_open_files table cache.  Changing ulimit in a
     * parent shell has no effect on an already-running process; doing it here
     * ensures the limit is set correctly regardless of how the daemon was
     * launched (shell, systemd, etc.). */
    {
        struct rlimit rl {};
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
        {
            const rlim_t desired = 65536;
            rl.rlim_cur = (rl.rlim_max == RLIM_INFINITY || rl.rlim_max >= desired) ? desired : rl.rlim_max;
            setrlimit(RLIMIT_NOFILE, &rl); // best-effort; failure is non-fatal
        }
    }
#endif

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

    /* --attach talks to a daemon that is already running. Handled before any
       of the startup below, because none of it applies: no database is opened,
       no ports are bound, and the data directory is not touched. */
    if (!config.attachSocket.empty())
    {
        return Daemon::runAttachConsole(config.attachSocket);
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
                           config.p2pResetPeerstate,
                           config.outPeers,
                           config.inPeers);

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

        /* Settle the lite profile before the cache is built: it decides how
           every block from here on is written, and it can never change for a
           database once the first block has landed. */
        const uint32_t liteHeight = resolveLiteProfile(*database, config, logger);

        System::Dispatcher dispatcher;
        logger(INFO) << "Initializing core...";

        const auto ccore = std::make_shared<CryptoNote::Core>(
            currency,
            logManager,
            std::move(checkpoints),
            dispatcher,
            std::unique_ptr<IBlockchainCacheFactory>(
                std::make_unique<DatabaseBlockchainCacheFactory>(*database, logger.getLogger(), liteHeight)),
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

                /* Use the recorded on-chain timestamp from the checkpoint when
                   available.  This ensures synthetic pre-anchor blocks get
                   historically correct timestamps so that the first real block
                   after the anchor passes the median-timestamp check.  Fall back
                   to wall-clock if the checkpoint has no timestamp (== 0); a
                   sync-floor bypass in Core::validateBlock keeps things safe even
                   in that case. */
                const uint64_t anchorTimestamp = (entry->timestamp != 0)
                    ? entry->timestamp
                    : static_cast<uint64_t>(std::time(nullptr));

                const uint64_t *lwmaTs = (entry->lwmaTimestamps[0] != 0)
                    ? entry->lwmaTimestamps
                    : nullptr;

                ccore->bootstrapFromHeight(
                    entry->height,
                    anchorHash,
                    anchorTimestamp,
                    entry->alreadyGeneratedCoins,
                    entry->cumulativeDifficulty,
                    entry->alreadyGeneratedTransactions,
                    entry->windowCumulDiff,
                    entry->anchorPrevBlockDiff,
                    lwmaTs);

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
        if (config.rewindToHeight > 0 && liteHeight != 0 && config.rewindToHeight < liteHeight)
        {
            logger(ERROR, BRIGHT_RED) << "Cannot rewind to " << config.rewindToHeight
                                      << ": this lite node only stores full block data from " << liteHeight
                                      << ". The blocks below that height were never stored, so there is nothing "
                                         "to roll back to.";
            return 1;
        }

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

        /* Explorer mode answers block and transaction lookups out of the raw
           blocks, and out of the transaction and payment ID indexes that point
           into them. None of that is written because the mode is on - a node
           synced without --daemon-mode explorer already holds all of it - so
           turning the mode on is a restart, not a resync, and there is no
           separate index to build.

           What the mode cannot conjure is a block this node never stored.
           --prune deletes raw blocks below its floor and --sync-from-height
           never downloads them, and every explorer answer ends up reading one:
           even f_transaction_json resolves a transaction to its block index and
           then reads that block. Report the position here, once, at startup,
           rather than leaving an operator to discover it one failed query at a
           time. */
        if (explorerMode)
        {
            const uint32_t explorerFloor = std::max(ccore->getPruneFloor(), ccore->getSyncFloorHeight());

            if (explorerFloor == 0)
            {
                logger(INFO, BRIGHT_GREEN)
                    << "Explorer mode: this node holds full block data from the genesis block, so every height can "
                       "be served.";
            }
            else
            {
                const bool pruned = ccore->getPruneFloor() > 0;

                logger(WARNING, BRIGHT_YELLOW)
                    << "Explorer mode: this node only holds full block data from height " << explorerFloor
                    << " upward. Block and transaction lookups below that height will fail, because "
                    << (pruned ? "--prune deleted those blocks" : "--sync-from-height never downloaded them")
                    << ". What is missing is the block data itself, not an index, so no reindex can recover it - "
                       "only downloading those blocks again. To serve the whole chain, resync this data directory "
                       "from scratch with --resync --daemon-mode explorer, and leave --prune and --sync-from-height "
                       "off. Serving only from height "
                    << explorerFloor << " upward is a legitimate setup, so the node is starting either way.";
            }
        }

        uint32_t ipcMode = Common::Ipc::DEFAULT_MODE;

        if (!config.rpcIpcPath.empty())
        {
            if (!Common::Ipc::parseMode(config.rpcIpcMode, ipcMode))
            {
                logger(ERROR, BRIGHT_RED)
                    << "--rpc-ipc-mode " << config.rpcIpcMode
                    << " is not an octal permission triple. Use something like 0600 (owner only) or 0660 "
                       "(owner and group).";
                return 1;
            }

            /* The mode on the socket file is the whole of the access control -
               there is no token in front of it - so a world-writable one is
               worth saying out loud rather than quietly honouring. */
            if ((ipcMode & 0007) != 0)
            {
                logger(WARNING, BRIGHT_YELLOW)
                    << "--rpc-ipc-mode " << Common::Ipc::formatMode(ipcMode)
                    << " lets any user on this machine drive the daemon, including its console. The file mode is "
                       "the only thing guarding that socket.";
            }
        }

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
                            config.rpcIpcPath,
                            ipcMode,
                            config.rpcIpcGroup);

        cprotocol->setSyncTuning(config.syncBatchMin, config.syncBatchMax, config.blockSyncBytes);
        cprotocol->setLiteNodeConfig(liteHeight);

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

        /* The stratum server lets a stock miner hash for this node directly.
           Optional, and a failure to bind is not fatal: the node keeps running
           without it rather than refusing to start over a busy port. */
        std::unique_ptr<Daemon::StratumServer> stratumServer;

        if (config.stratumBindPort != 0)
        {
            stratumServer = std::make_unique<Daemon::StratumServer>(
                dispatcher,
                *ccore,
                *cprotocol,
                logManager,
                config.stratumBindIp,
                config.stratumBindPort,
                config.stratumShareDifficulty,
                config.stratumMaxConnections);

            if (!stratumServer->start())
            {
                logger(WARNING) << "Failed to start the stratum server. Continuing without it.";
                stratumServer.reset();
            }
        }

        /* Monero-style --block-notify / --reorg-notify / --tx-notify hooks.
           Delivery runs on the notifier's own worker threads; the dispatcher
           fiber that consumes Core's message stream only formats and enqueues,
           so a slow webhook or a wedged child process cannot stall the node. */
        std::unique_ptr<Daemon::ChainNotifier> chainNotifier;

        if (!config.blockNotify.empty() || !config.reorgNotify.empty() || !config.txNotify.empty())
        {
            chainNotifier = std::make_unique<Daemon::ChainNotifier>(
                dispatcher,
                *ccore,
                *cprotocol,
                logManager,
                config.blockNotify,
                config.reorgNotify,
                config.txNotify,
                config.notifyDuringSync);

            if (!chainNotifier->start())
            {
                logger(WARNING) << "No usable notification hook configured. Continuing without notifications.";
                chainNotifier.reset();
            }
        }

        /* Get the RPC IP address and port we are bound to */
        auto [ip, port] = rpcServer.getConnectionInfo();

        /* If we bound the RPC to 0.0.0.0, we can't reach that with a
           standard HTTP client from anywhere. Instead, let's use the
           localhost IP address to reach ourselves */
        if (ip == "0.0.0.0")
        {
            ip = "127.0.0.1";
        }

        auto pruneTrigger = std::make_shared<std::atomic<bool>>(false);
        DaemonCommandsHandler dch(*ccore, *p2psrv, cprotocol, logManager, ip, port, database, config, pruneTrigger);

        /* A console attached over the socket runs its commands through the same
           handler as the local one. Set after the handler exists and cleared
           before it goes away, so the RPC thread can never reach a dangling
           reference. */
        rpcServer.setConsoleExecutor([&dch](const std::string &commandLine)
                                     { return dch.run_remote_command(commandLine); });

        if (!rpcServer.getIpcPath().empty())
        {
            logger(INFO, BRIGHT_GREEN)
                << "RPC is also on " << Common::Ipc::describe(rpcServer.getIpcPath()) << " (mode "
                << Common::Ipc::formatMode(ipcMode) << "). Console: DeroGoldd --attach " << rpcServer.getIpcPath();
        }

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

            pruneWorker = std::thread([&, prunePassInterval, prunePollInterval, pruneTrigger]
                                      {
                                          // Start immediately on first run to catch up any blocks skipped
                                          // by previous daemon runs where prune was enabled but non-functional.
                                          auto nextRun = std::chrono::steady_clock::now();
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

                                              // Manual trigger from 'prune_status start' console command.
                                              if (pruneTrigger->exchange(false))
                                              {
                                                  nextRun = std::chrono::steady_clock::now();
                                              }

                                              if (std::chrono::steady_clock::now() < nextRun)
                                              {
                                                  std::this_thread::sleep_for(prunePollInterval);
                                                  continue;
                                              }

                                              prunePassTask = std::async(
                                                  std::launch::async,
                                                  [&, depth = config.pruneDepth]
                                                  {
                                                      if (!config.prune)
                                                      {
                                                          return;
                                                      }

                                                      // Prune through the blockchain cache rather than writing
                                                      // raw deletes here. That path preserves the genesis block,
                                                      // batches the deletes, and — critically — persists the new
                                                      // prune floor in the same atomic write. Deleting keys
                                                      // directly left the stored floor stale, so the wallet sync
                                                      // RPC, the pruned-block guard and prune_status all reported
                                                      // a floor that no longer matched the database, and every
                                                      // restart re-issued a tombstone for the whole pruned range.
                                                      uint64_t pruneFloor = 0;
                                                      uint32_t currentFloor = 0;
                                                      try
                                                      {
                                                          const uint64_t height = ccore->getTopBlockIndex() + 1;
                                                          pruneFloor = height > depth ? height - depth : 0;
                                                          currentFloor = ccore->getPruneFloor();
                                                      }
                                                      catch (const std::exception &)
                                                      {
                                                          return;
                                                      }

                                                      if (pruneFloor <= currentFloor)
                                                      {
                                                          return;
                                                      }

                                                      logger(INFO)
                                                          << "Starting periodic prune pass (depth " << depth
                                                          << ", pruning raw blocks [" << currentFloor
                                                          << ", " << pruneFloor << ")).";

                                                      try
                                                      {
                                                          ccore->pruneRawBlocksBefore(
                                                              static_cast<uint32_t>(pruneFloor));
                                                      }
                                                      catch (const std::exception &e)
                                                      {
                                                          logger(WARNING) << "Prune pass failed: " << e.what();
                                                          return;
                                                      }

                                                      logger(INFO)
                                                          << "Periodic prune pass completed. Prune floor now at: "
                                                          << pruneFloor << ".";
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
                static std::atomic<bool> s_alreadyShuttingDown(false);
                if (s_alreadyShuttingDown.exchange(true))
                {
                    // Second signal while shutdown is already in progress: force-exit immediately.
                    std::_Exit(1);
                }
                dch.exit({});
                dch.stop_handling();
            });

        logger(INFO) << "Starting p2p net loop...";
        p2psrv->run();
        logger(INFO) << "p2p net loop stopped";

        dch.stop_handling();

        /* Before dch goes out of scope: the RPC threads are still running and
           would otherwise hold a reference to it. */
        rpcServer.setConsoleExecutor(nullptr);

        // stop components
        if (stratumServer)
        {
            logger(INFO) << "Stopping stratum server...";
            stratumServer->stop();
        }

        if (chainNotifier)
        {
            logger(INFO) << "Stopping chain notifier...";
            chainNotifier->stop();
        }

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
