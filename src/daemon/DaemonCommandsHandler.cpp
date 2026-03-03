// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#include "JsonHelper.h"
#include "version.h"
#include "config/SyncBootstrapCheckpoints.h"

#include <boost/format.hpp>
#include <common/StringTools.h>
#include <common/Util.h>
#include <IDataBase.h>
#include <cryptonotecore/Core.h>
#include <cryptonotecore/Currency.h>
#include <cryptonoteprotocol/CryptoNoteProtocolHandler.h>
#include <ctime>
#include <daemon/DaemonCommandsHandler.h>
#include <filesystem>
#include <p2p/NetNode.h>
#include <rpc/JsonRpc.h>
#include <serialization/SerializationTools.h>
#include <utilities/ColouredMsg.h>
#include <utilities/FormatTools.h>
#include <utilities/Utilities.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    namespace fs = std::filesystem;
    constexpr uint64_t AUTO_COMPACTION_CHECK_INTERVAL_FAST_SECONDS = 60;
    constexpr uint64_t AUTO_COMPACTION_CHECK_INTERVAL_SLOW_SECONDS = 30 * 60;
    constexpr uint64_t AUTO_COMPACTION_NEAR_SYNC_LAG_BLOCKS = 2;
    constexpr uint64_t AUTO_COMPACTION_RESYNC_LAG_BLOCKS = 20;
    constexpr uint32_t AUTO_COMPACTION_NEAR_SYNC_STREAK_REQUIRED = 3;
    constexpr uint64_t AUTO_COMPACTION_MIN_GAP_BLOCKS = 10080;

    std::string format_epoch(uint64_t epochSeconds)
    {
        if (epochSeconds == 0)
        {
            return "n/a";
        }

        const std::time_t t = static_cast<std::time_t>(epochSeconds);
        std::tm tm {};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    }

    template<typename T> bool print_as_json(const T &obj)
    {
        std::cout << CryptoNote::storeToJson(obj) << ENDL;
        return true;
    }

    std::string printTransactionShortInfo(const CryptoNote::CachedTransaction &transaction)
    {
        std::stringstream ss;

        ss << "id: " << transaction.getTransactionHash() << std::endl;
        ss << "fee: " << transaction.getTransactionFee() << std::endl;
        ss << "blobSize: " << transaction.getTransactionBinaryArray().size() << std::endl;

        return ss.str();
    }

    std::string printTransactionFullInfo(const CryptoNote::CachedTransaction &transaction)
    {
        std::stringstream ss;
        ss << printTransactionShortInfo(transaction);
        ss << "JSON: \n" << CryptoNote::storeToJson(transaction.getTransaction()) << std::endl;

        return ss.str();
    }

} // namespace

DaemonCommandsHandler::DaemonCommandsHandler(
    CryptoNote::Core &core,
    CryptoNote::NodeServer &srv,
    const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> &syncManager,
    const std::shared_ptr<Logging::LoggerManager> &log,
    const std::string &ip,
    const uint32_t port,
    const std::shared_ptr<CryptoNote::IDataBase> &database,
    DaemonConfig::DaemonConfiguration config
) :
    m_core(core),
    m_srv(srv),
    m_syncManager(syncManager),
    m_rpcServer(ip, static_cast<int>(port)),
    logger(log, "daemon"),
    m_config(std::move(config)),
    m_logManager(log),
    m_database(database)
{
    m_consoleHandler
        .setHandler("?", [this](const std::vector<std::string> &args) { return help(args); }, "Show this help");
    m_consoleHandler
        .setHandler("exit", [this](const std::vector<std::string> &args) { return exit(args); }, "Shutdown the daemon");
    m_consoleHandler
        .setHandler("help", [this](const std::vector<std::string> &args) { return help(args); }, "Show this help");
    m_consoleHandler.setHandler(
        "ban",
        [this](const std::vector<std::string> &args) { return ban(args); },
        "Manage in-memory host bans: ban list | ban add <ip> [seconds] | ban delete <ip>"
    );
    m_consoleHandler.setHandler(
        "compact_db",
        [this](const std::vector<std::string> &args) { return compact_db(args); },
        "Manage DB compaction: compact_db [start|status|wait|stop]"
    );
    m_consoleHandler.setHandler(
        "db_status",
        [this](const std::vector<std::string> &args) { return db_status(args); },
        "Show on-disk DB status for the active DB engine"
    );
    m_consoleHandler.setHandler(
        "print_pl",
        [this](const std::vector<std::string> &args) { return print_pl(args); },
        "Print peer list"
    );
    m_consoleHandler.setHandler(
        "print_cn",
        [this](const std::vector<std::string> &args) { return print_cn(args); },
        "Print connections"
    );
    m_consoleHandler.setHandler(
        "print_block",
        [this](const std::vector<std::string> &args) { return print_block(args); },
        "Print block, print_block <block_hash> | <block_height>"
    );
    m_consoleHandler.setHandler(
        "print_tx",
        [this](const std::vector<std::string> &args) { return print_tx(args); },
        "Print transaction, print_tx <transaction_hash>"
    );
    m_consoleHandler.setHandler(
        "print_pool",
        [this](const std::vector<std::string> &args) { return print_pool(args); },
        "Print transaction pool (long format)"
    );
    m_consoleHandler.setHandler(
        "print_pool_sh",
        [this](const std::vector<std::string> &args) { return print_pool_sh(args); },
        "Print transaction pool (short format)"
    );
    m_consoleHandler.setHandler(
        "prune_status",
        [this](const std::vector<std::string> &args) { return prune_status(args); },
        "Show prune mode and capability status"
    );
    m_consoleHandler.setHandler(
        "export_bootstrap_state",
        [this](const std::vector<std::string> &args) { return export_bootstrap_state(args); },
        "export_bootstrap_state <height> – Print the BootstrapCheckpoint record for <height>. "
        "Run this on a fully-synced node and copy the output into SyncBootstrapCheckpoints.h."
    );
    m_consoleHandler.setHandler(
        "sync_height_status",
        [this](const std::vector<std::string> &args) { return sync_height_status(args); },
        "Show whether this node was started with --sync-from-height and what the sync floor is"
    );
    m_consoleHandler.setHandler(
        "save",
        [this](const std::vector<std::string> &args) { return save(args); },
        "Force-save blockchain state to disk"
    );
    m_consoleHandler.setHandler(
        "set_log",
        [this](const std::vector<std::string> &args) { return set_log(args); },
        "set_log <level> - Change current log level, <level> is a number 0-4"
    );
    m_consoleHandler.setHandler(
        "status",
        [this](const std::vector<std::string> &args) { return status(args); },
        "Show daemon status"
    );

    m_compactDbSchedulerCheckIntervalSeconds.store(AUTO_COMPACTION_CHECK_INTERVAL_FAST_SECONDS);
    m_stopCompactDbScheduler = false;
    m_compactDbSchedulerThread = std::thread([this] { compact_db_scheduler_loop(); });
}

DaemonCommandsHandler::~DaemonCommandsHandler()
{
    m_stopCompactDbScheduler = true;
    if (m_compactDbSchedulerThread.joinable())
    {
        m_compactDbSchedulerThread.join();
    }
}

//--------------------------------------------------------------------------------
std::string DaemonCommandsHandler::get_commands_str() const
{
    std::stringstream ss;
    ss << CryptoNote::CRYPTONOTE_NAME << " v" << PROJECT_VERSION_LONG << ENDL;
    ss << "Commands: " << ENDL;
    std::string usage = m_consoleHandler.getUsage();
    boost::replace_all(usage, "\n", "\n  ");
    usage.insert(0, "  ");
    ss << usage << ENDL;
    return ss.str();
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::exit(const std::vector<std::string> &args)
{
    std::cout << InformationMsg("================= EXITING ==================\n"
                                "== PLEASE WAIT, THIS MAY TAKE A LONG TIME ==\n"
                                "============================================\n");

    /* Set log to max when exiting. Sometimes this takes a while, and it helps
       to let users know the daemon is still doing stuff */
    m_logManager->setMaxLevel(Logging::TRACE);
    m_consoleHandler.requestStop();
    m_srv.sendStopSignal();
    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::help(const std::vector<std::string> &args)
{
    std::cout << get_commands_str() << ENDL;
    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_pl(const std::vector<std::string> &args)
{
    m_srv.log_peerlist();
    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_cn(const std::vector<std::string> &args)
{
    m_srv.get_payload_object().log_connections();
    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::set_log(const std::vector<std::string> &args)
{
    if (args.size() != 1)
    {
        std::cout << "use: set_log <log_level_number_0-4>" << ENDL;
        return true;
    }

    uint16_t l = 0;
    if (!Common::fromString(args[0], l))
    {
        std::cout << "wrong number format, use: set_log <log_level_number_0-4>" << ENDL;
        return true;
    }

    ++l;

    if (l > Logging::TRACE)
    {
        std::cout << "wrong number range, use: set_log <log_level_number_0-4>" << ENDL;
        return true;
    }

    m_logManager->setMaxLevel(static_cast<Logging::Level>(l));
    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_block_by_height(uint32_t height)
{
    if (height - 1 > m_core.getTopBlockIndex())
    {
        std::cout << "block wasn't found. Current block chain height: " << m_core.getTopBlockIndex() + 1
                  << ", requested: " << height << std::endl;
        return false;
    }

    auto hash = m_core.getBlockHashByIndex(height - 1);
    std::cout << "block_id: " << hash << ENDL;
    print_as_json(m_core.getBlockByIndex(height - 1));

    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_block_by_hash(const std::string &arg)
{
    Crypto::Hash block_hash;
    if (!parse_hash256(arg, block_hash))
    {
        return false;
    }

    if (m_core.hasBlock(block_hash))
    {
        print_as_json(m_core.getBlockByHash(block_hash));
    }
    else
    {
        std::cout << "block wasn't found: " << arg << std::endl;
        return false;
    }

    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_block(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cout << "expected: print_block (<block_hash> | <block_height>)" << std::endl;
        return true;
    }

    const std::string &arg = args.front();
    try
    {
        uint32_t height = boost::lexical_cast<uint32_t>(arg);
        print_block_by_height(height);
    }
    catch (boost::bad_lexical_cast &)
    {
        print_block_by_hash(arg);
    }

    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_tx(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cout << "expected: print_tx <transaction hash>" << std::endl;
        return true;
    }

    const std::string &str_hash = args.front();
    Crypto::Hash tx_hash;
    if (!parse_hash256(str_hash, tx_hash))
    {
        return true;
    }

    std::vector<Crypto::Hash> tx_ids;
    tx_ids.push_back(tx_hash);
    std::vector<CryptoNote::BinaryArray> txs;
    std::vector<Crypto::Hash> missed_ids;
    m_core.getTransactions(tx_ids, txs, missed_ids);

    if (1 == txs.size())
    {
        CryptoNote::CachedTransaction tx(txs.front());
        print_as_json(tx.getTransaction());
    }
    else
    {
        std::cout << "transaction wasn't found: <" << str_hash << '>' << std::endl;
    }

    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_pool(const std::vector<std::string> &args)
{
    std::cout << "Pool state: \n";
    auto pool = m_core.getPoolTransactions();

    for (const auto &tx : pool)
    {
        CryptoNote::CachedTransaction ctx(tx);
        std::cout << printTransactionFullInfo(ctx) << "\n";
    }

    std::cout << std::endl;

    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::print_pool_sh(const std::vector<std::string> &args)
{
    const auto pool = m_core.getPoolTransactions();

    if (pool.size() == 0)
    {
        std::cout << InformationMsg("\nPool state: ") << SuccessMsg("Empty.") << std::endl;
        return true;
    }

    std::cout << InformationMsg("\nPool state:\n");

    uint64_t totalSize = 0;

    const float maxTxSize = Utilities::getMaxTxSize(m_core.getTopBlockIndex());

    for (const auto &tx : pool)
    {
        CryptoNote::CachedTransaction ctx(tx);

        std::cout << InformationMsg("Hash: ") << SuccessMsg(ctx.getTransactionHash()) << InformationMsg(", Fusion: ");

        if (ctx.getTransactionFee() == 0)
        {
            std::cout << SuccessMsg("Yes") << std::endl;
        }
        else
        {
            std::cout << WarningMsg("No") << std::endl;
        }

        totalSize += ctx.getTransactionBinaryArray().size();
    }

    const float blocksRequiredToClear = std::ceil(totalSize / maxTxSize);

    std::cout << InformationMsg("\nTotal transactions: ") << SuccessMsg(pool.size())
              << InformationMsg("\nTotal size of transactions: ") << SuccessMsg(Utilities::prettyPrintBytes(totalSize))
              << InformationMsg("\nEstimated full blocks to clear: ") << SuccessMsg(blocksRequiredToClear) << std::endl
              << std::endl;

    return true;
}

//--------------------------------------------------------------------------------
bool DaemonCommandsHandler::status(const std::vector<std::string> &args)
{
    const std::time_t uptime = std::time(nullptr) - m_core.getStartTime();

    const uint64_t seconds = uptime;
    const uint64_t minutes = seconds / 60;
    const uint64_t hours = minutes / 60;
    const uint64_t days = hours / 24;

    const std::string uptimeStr = std::to_string(days) + "d " + std::to_string(hours % 24) + "h "
                                + std::to_string(minutes % 60) + "m " + std::to_string(seconds % 60) + "s";

    const uint64_t height = m_core.getTopBlockIndex() + 1;
    const uint64_t networkHeight = std::max(1u, m_syncManager->getBlockchainHeight());
    const uint64_t supportedHeight =
        CryptoNote::parameters::FORK_HEIGHTS_SIZE == 0
            ? 0
            : CryptoNote::parameters::FORK_HEIGHTS[CryptoNote::parameters::CURRENT_FORK_INDEX];
    std::vector<uint64_t> upgradeHeights;

    for (const auto &height : CryptoNote::parameters::FORK_HEIGHTS)
    {
        upgradeHeights.push_back(height);
    }

    const auto forkStatus = Utilities::get_fork_status(networkHeight, upgradeHeights, supportedHeight);

    const uint64_t total_conn = m_srv.get_connections_count();
    const uint64_t outgoing_connections_count = m_srv.get_outgoing_connections_count();

    std::vector<std::tuple<std::string, std::string>> statusTable;

    statusTable.emplace_back("Local Height", std::to_string(height));
    statusTable.emplace_back("Network Height", std::to_string(networkHeight));
    statusTable.emplace_back("Percentage Synced", Utilities::get_sync_percentage(height, networkHeight) + "%");
    statusTable.emplace_back(
        "Network Hashrate",
        Utilities::get_mining_speed(
            m_core.getDifficultyForNextBlock() / CryptoNote::parameters::getCurrentDifficultyTarget(networkHeight)
        )
    );
    statusTable.emplace_back("Block Version", "v" + std::to_string(m_core.getBlockDetails(height - 1).majorVersion));
    statusTable.emplace_back("Incoming Connections", std::to_string(total_conn - outgoing_connections_count));
    statusTable.emplace_back("Outgoing Connections", std::to_string(outgoing_connections_count));
    statusTable.emplace_back("Uptime", uptimeStr);
    statusTable.emplace_back("Fork Status", Utilities::get_update_status(forkStatus));
    statusTable.emplace_back("Next Fork", Utilities::get_fork_time(networkHeight, upgradeHeights));
    statusTable.emplace_back("Transaction Pool Size", std::to_string(m_core.getPoolTransactionHashes().size()));
    statusTable.emplace_back("Alternative Block Count", std::to_string(m_core.getAlternativeBlockCount()));
    statusTable.emplace_back(
        "Prune Mode",
        m_config.prune ? ("Enabled (depth " + std::to_string(m_config.pruneDepth) + ")") : "Disabled");
    statusTable.emplace_back("Background Prune", m_config.backgroundPrune ? "Enabled (async)" : "Disabled");
    statusTable.emplace_back("DB Engine", "RocksDB");
    statusTable.emplace_back("Version", PROJECT_VERSION_WITH_BUILD);

    size_t longestValue = 0;
    size_t longestDescription = 0;

    /* Figure out the dimensions of the table */
    for (const auto &[value, description] : statusTable)
    {
        if (value.length() > longestValue)
        {
            longestValue = value.length();
        }

        if (description.length() > longestDescription)
        {
            longestDescription = description.length();
        }
    }

    /* Need 7 extra chars for all the padding and borders in addition to the
     * values inside the table */
    const size_t totalTableWidth = longestValue + longestDescription + 7;

    /* Table border */
    std::cout << std::string(totalTableWidth, '-') << std::endl;

    /* Output the table itself */
    for (const auto &[value, description] : statusTable)
    {
        std::cout << "| " << InformationMsg(value, longestValue) << " ";
        std::cout << "| " << SuccessMsg(description, longestDescription) << " |" << std::endl;
    }

    /* Table border */
    std::cout << std::string(totalTableWidth, '-') << std::endl;

    if (forkStatus == Utilities::OutOfDate)
    {
        std::cout << WarningMsg(Utilities::get_upgrade_info(supportedHeight, upgradeHeights)) << std::endl;
    }

    return true;
}

bool DaemonCommandsHandler::ban(const std::vector<std::string> &args)
{
    const auto now = std::chrono::system_clock::now();

    for (auto it = m_bannedHosts.begin(); it != m_bannedHosts.end();)
    {
        if (it->second <= now)
        {
            it = m_bannedHosts.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (args.empty() || args[0] == "list")
    {
        if (m_bannedHosts.empty())
        {
            std::cout << InformationMsg("Ban list is empty.") << std::endl;
            return true;
        }

        std::cout << InformationMsg("Banned hosts:") << std::endl;
        for (const auto &[ip, expiry] : m_bannedHosts)
        {
            const auto secs =
                std::chrono::duration_cast<std::chrono::seconds>(expiry - now).count();
            std::cout << "  " << ip << " (" << std::max<int64_t>(0, secs) << "s remaining)" << std::endl;
        }
        return true;
    }

    if (args[0] == "add")
    {
        if (args.size() < 2 || args.size() > 3)
        {
            std::cout << "usage: ban add <ip> [seconds]" << std::endl;
            return true;
        }

        uint64_t banSeconds = 3600;
        if (args.size() == 3 && !Common::fromString(args[2], banSeconds))
        {
            std::cout << "Invalid seconds value." << std::endl;
            return true;
        }

        m_bannedHosts[args[1]] = now + std::chrono::seconds(banSeconds);
        std::cout << InformationMsg("Banned host: ") << SuccessMsg(args[1]) << InformationMsg(" for ")
                  << SuccessMsg(std::to_string(banSeconds) + "s") << std::endl;
        std::cout << InformationMsg("Note: active connection drop for bans is not exposed in this build.") << std::endl;

        return true;
    }

    if (args[0] == "delete")
    {
        if (args.size() != 2)
        {
            std::cout << "usage: ban delete <ip>" << std::endl;
            return true;
        }

        if (m_bannedHosts.erase(args[1]) > 0)
        {
            std::cout << InformationMsg("Unbanned host: ") << SuccessMsg(args[1]) << std::endl;
        }
        else
        {
            std::cout << WarningMsg("Host is not in ban list: ") << args[1] << std::endl;
        }
        return true;
    }

    std::cout << "usage: ban list | ban add <ip> [seconds] | ban delete <ip>" << std::endl;
    return true;
}

bool DaemonCommandsHandler::compact_db(const std::vector<std::string> &args)
{
    {
        std::lock_guard<std::mutex> lock(m_compactDbMutex);
        refresh_compaction_state_locked();
    }

    std::string action = args.empty() ? "status" : args[0];
    std::transform(action.begin(), action.end(), action.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });

    if (action == "status")
    {
        std::lock_guard<std::mutex> lock(m_compactDbMutex);
        refresh_compaction_state_locked();

        if (m_compactDbRunning)
        {
            const auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::steady_clock::now() - m_compactDbStart)
                                            .count();
            std::cout << InformationMsg("DB compaction status: ") << WarningMsg("running")
                      << InformationMsg(" (elapsed ") << SuccessMsg(std::to_string(elapsedSeconds) + "s")
                      << InformationMsg(")") << std::endl;
            std::cout << InformationMsg("Started at: ") << SuccessMsg(format_epoch(m_compactDbStartedAtEpoch))
                      << InformationMsg(", height ") << SuccessMsg(std::to_string(m_compactDbStartedAtHeight))
                      << std::endl;
            return true;
        }

        if (!m_compactDbHasRun)
        {
            std::cout << InformationMsg("DB compaction status: ") << SuccessMsg("idle") << std::endl;
            return true;
        }

        if (m_compactDbLastSuccess)
        {
            std::cout << InformationMsg("DB compaction status: ") << SuccessMsg("last run completed") << std::endl;
        }
        else
        {
            std::string error;
            {
                std::lock_guard<std::mutex> lock(m_compactDbMutex);
                error = m_compactDbLastError;
            }

            std::cout << InformationMsg("DB compaction status: ") << WarningMsg("last run failed");
            if (!error.empty())
            {
                std::cout << InformationMsg(" (") << WarningMsg(error) << InformationMsg(")");
            }
            std::cout << std::endl;
        }

        std::cout << InformationMsg("Last start: ") << SuccessMsg(format_epoch(m_compactDbStartedAtEpoch))
                  << InformationMsg(", height ") << SuccessMsg(std::to_string(m_compactDbStartedAtHeight))
                  << std::endl;
        std::cout << InformationMsg("Last finish: ") << SuccessMsg(format_epoch(m_compactDbFinishedAtEpoch))
                  << InformationMsg(", height ") << SuccessMsg(std::to_string(m_compactDbFinishedAtHeight))
                  << std::endl;
        std::cout << InformationMsg("Auto scheduler: ")
                  << SuccessMsg(
                         "enabled, interval "
                         + std::to_string(m_compactDbSchedulerCheckIntervalSeconds.load()) + "s")
                  << std::endl;
        return true;
    }

    if (action == "start")
    {
        std::lock_guard<std::mutex> lock(m_compactDbMutex);
        refresh_compaction_state_locked();

        if (!start_compaction_locked("manual console request"))
        {
            std::cout << WarningMsg("DB compaction is already running.") << std::endl;
            return true;
        }

        std::cout << SuccessMsg("Started DB compaction in background.") << std::endl;
        return true;
    }

    if (action == "wait")
    {
        if (!m_compactDbTask.valid())
        {
            std::cout << InformationMsg("No DB compaction job has been started.") << std::endl;
            return true;
        }

        if (m_compactDbRunning)
        {
            std::cout << InformationMsg("Waiting for DB compaction to complete...") << std::endl;
            m_compactDbTask.wait();
        }

        {
            std::lock_guard<std::mutex> lock(m_compactDbMutex);
            refresh_compaction_state_locked();
        }

        if (!m_compactDbRunning && m_compactDbLastSuccess)
        {
            std::cout << SuccessMsg("DB compaction completed.") << std::endl;
            return true;
        }

        std::string error;
        {
            std::lock_guard<std::mutex> lock(m_compactDbMutex);
            error = m_compactDbLastError;
        }
        std::cout << WarningMsg("DB compaction failed.");
        if (!error.empty())
        {
            std::cout << " " << error;
        }
        std::cout << std::endl;
        return true;
    }

    if (action == "stop")
    {
        std::lock_guard<std::mutex> lock(m_compactDbMutex);
        refresh_compaction_state_locked();

        if (!m_compactDbRunning)
        {
            std::cout << InformationMsg("No running DB compaction job.") << std::endl;
            return true;
        }

        if (!m_database)
        {
            std::cout << WarningMsg("Database handle is not available.") << std::endl;
            return true;
        }

        if (m_database->cancelOptimize())
        {
            std::cout << InformationMsg("Stop requested for DB compaction.") << std::endl;
            m_compactDbNearSyncStreak = 0;
            m_compactDbFinishedAtHeight = static_cast<uint64_t>(m_core.getTopBlockIndex()) + 1;
        }
        else
        {
            std::cout << WarningMsg("Unable to stop DB compaction (not running).") << std::endl;
        }

        return true;
    }

    std::cout << "usage: compact_db [start|status|wait|stop]" << std::endl;
    return true;
}

void DaemonCommandsHandler::refresh_compaction_state_locked()
{
    if (!m_compactDbTask.valid())
    {
        return;
    }

    if (m_compactDbTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }

    try
    {
        m_compactDbTask.get();
        m_compactDbLastSuccess = true;
        m_compactDbLastError.clear();
    }
    catch (const std::exception &e)
    {
        m_compactDbLastSuccess = false;
        m_compactDbLastError = e.what();
    }
    catch (...)
    {
        m_compactDbLastSuccess = false;
        m_compactDbLastError = "Unknown compaction error";
    }

    m_compactDbRunning = false;
    m_compactDbFinishedAtEpoch = static_cast<uint64_t>(std::time(nullptr));
    m_compactDbFinishedAtHeight = static_cast<uint64_t>(m_core.getTopBlockIndex()) + 1;
}

bool DaemonCommandsHandler::start_compaction_locked(const std::string &reason)
{
    if (m_compactDbRunning)
    {
        return false;
    }

    m_compactDbHasRun = true;
    m_compactDbLastSuccess = true;
    m_compactDbLastError.clear();
    m_compactDbStart = std::chrono::steady_clock::now();
    m_compactDbStartedAtEpoch = static_cast<uint64_t>(std::time(nullptr));
    m_compactDbStartedAtHeight = static_cast<uint64_t>(m_core.getTopBlockIndex()) + 1;
    m_compactDbRunning = true;

    logger(Logging::INFO) << "Starting DB compaction (" << reason << ").";
    m_compactDbTask = std::async(std::launch::async,
                                 [this]
                                 {
                                     if (!m_database)
                                     {
                                         throw std::runtime_error("Database handle is not available");
                                     }
                                     m_database->optimize();
                                 });
    return true;
}

void DaemonCommandsHandler::compact_db_scheduler_loop()
{
    while (!m_stopCompactDbScheduler)
    {
        const uint64_t sleepSeconds = m_compactDbSchedulerCheckIntervalSeconds.load();
        for (uint64_t i = 0; i < sleepSeconds && !m_stopCompactDbScheduler; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (m_stopCompactDbScheduler)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_compactDbMutex);
        refresh_compaction_state_locked();

        const uint64_t localHeight = static_cast<uint64_t>(m_core.getTopBlockIndex()) + 1;
        const uint64_t networkHeight = std::max<uint64_t>(1, m_syncManager->getBlockchainHeight());
        const uint64_t lag = networkHeight > localHeight ? networkHeight - localHeight : 0;

        if (lag <= AUTO_COMPACTION_NEAR_SYNC_LAG_BLOCKS)
        {
            m_compactDbNearSyncStreak += 1;
        }
        else if (lag >= AUTO_COMPACTION_RESYNC_LAG_BLOCKS)
        {
            m_compactDbNearSyncStreak = 0;
        }

        const uint64_t desiredInterval =
            m_compactDbNearSyncStreak >= AUTO_COMPACTION_NEAR_SYNC_STREAK_REQUIRED
                ? AUTO_COMPACTION_CHECK_INTERVAL_SLOW_SECONDS
                : AUTO_COMPACTION_CHECK_INTERVAL_FAST_SECONDS;

        if (desiredInterval != m_compactDbSchedulerCheckIntervalSeconds.load())
        {
            m_compactDbSchedulerCheckIntervalSeconds.store(desiredInterval);
            logger(Logging::INFO)
                << "Adaptive compact_db scheduler interval switched to "
                << m_compactDbSchedulerCheckIntervalSeconds.load() << "s (lag " << lag << ").";
        }

        if (m_compactDbRunning)
        {
            continue;
        }

        if (m_compactDbNearSyncStreak < AUTO_COMPACTION_NEAR_SYNC_STREAK_REQUIRED)
        {
            continue;
        }

        const uint64_t lastActivityHeight = std::max(m_compactDbStartedAtHeight, m_compactDbFinishedAtHeight);
        if (lastActivityHeight != 0 && localHeight > lastActivityHeight
            && (localHeight - lastActivityHeight) < AUTO_COMPACTION_MIN_GAP_BLOCKS)
        {
            continue;
        }

        if (start_compaction_locked("automatic scheduler"))
        {
            logger(Logging::INFO) << "Automatic periodic DB compaction started in background.";
        }
    }
}

bool DaemonCommandsHandler::db_status(const std::vector<std::string> &args)
{
    const fs::path dbPath = fs::path(m_config.dataDirectory) / "DB";

    if (!fs::exists(dbPath))
    {
        std::cout << WarningMsg("DB path does not exist: ") << dbPath.string() << std::endl;
        return true;
    }

    uintmax_t totalBytes = 0;
    uint64_t fileCount = 0;
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(dbPath, ec))
    {
        if (ec)
        {
            break;
        }
        if (entry.is_regular_file(ec))
        {
            fileCount++;
            totalBytes += entry.file_size(ec);
        }
    }

    std::cout << InformationMsg("DB Engine: ") << SuccessMsg("RocksDB") << std::endl;
    std::cout << InformationMsg("DB Path: ") << SuccessMsg(dbPath.string()) << std::endl;
    std::cout << InformationMsg("DB Files: ") << SuccessMsg(fileCount) << std::endl;
    std::cout << InformationMsg("DB Size: ") << SuccessMsg(Utilities::prettyPrintBytes(totalBytes)) << std::endl;
    std::cout << InformationMsg("Compression: ") << SuccessMsg(m_config.enableDbCompression ? "Enabled" : "Disabled")
              << std::endl;
    return true;
}

bool DaemonCommandsHandler::prune_status(const std::vector<std::string> &args)
{
    const uint64_t height = m_core.getTopBlockIndex() + 1;
    const uint64_t pruneDepth = m_config.pruneDepth;
    const uint64_t pruneFloor = height > pruneDepth ? height - pruneDepth : 0;

    std::cout << InformationMsg("Pruned Node: ") << SuccessMsg(m_config.prune ? "Yes" : "No") << std::endl;
    std::cout << InformationMsg("Background Prune Task: ")
              << SuccessMsg(m_config.backgroundPrune ? "Enabled (async)" : "Disabled") << std::endl;
    std::cout << InformationMsg("Prune Depth: ") << SuccessMsg(pruneDepth) << std::endl;
    std::cout << InformationMsg("Approx Prune Floor Height: ") << SuccessMsg(pruneFloor) << std::endl;
    return true;
}

bool DaemonCommandsHandler::export_bootstrap_state(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cout << InformationMsg("Usage: export_bootstrap_state <height>") << std::endl
                  << "Prints a BootstrapCheckpoint record for the given height." << std::endl
                  << "Run this on a fully-synced node and add the output to" << std::endl
                  << "  src/config/SyncBootstrapCheckpoints.h" << std::endl;
        return true;
    }

    uint32_t targetHeight = 0;
    try
    {
        targetHeight = static_cast<uint32_t>(std::stoul(args[0]));
    }
    catch (const std::exception &)
    {
        std::cout << WarningMsg("Invalid height: ") << args[0] << std::endl;
        return false;
    }

    const uint64_t topIndex = m_core.getTopBlockIndex();
    if (targetHeight == 0 || targetHeight > topIndex)
    {
        std::cout << WarningMsg("Height must be between 1 and ")
                  << topIndex << " (current chain top)." << std::endl;
        return false;
    }

    /* Fetch the block hash at targetHeight. */
    const Crypto::Hash blockHash = m_core.getBlockHashByIndex(targetHeight);

    /* Walk from genesis to targetHeight to accumulate coin & tx counts.
     * For a fully-synced node this is read from DB metadata per-block. */
    uint64_t alreadyGeneratedCoins        = 0;
    uint64_t cumulativeDifficulty         = 0;
    uint64_t alreadyGeneratedTransactions = 0;

    try
    {
        CryptoNote::BlockDetails details = m_core.getBlockDetails(blockHash);
        alreadyGeneratedCoins        = details.alreadyGeneratedCoins;
        alreadyGeneratedTransactions = details.alreadyGeneratedTransactions;
        cumulativeDifficulty         = m_core.getCumulativeDifficulty(targetHeight);
    }
    catch (const std::exception &e)
    {
        std::cout << WarningMsg("Could not fetch block details: ") << e.what() << std::endl;
        return false;
    }

    std::cout << std::endl
              << InformationMsg("=== Bootstrap Checkpoint for height ") << targetHeight
              << InformationMsg(" ===") << std::endl
              << "Copy this struct into src/config/SyncBootstrapCheckpoints.h :"
              << std::endl << std::endl;

    std::cout << "        {" << std::endl
              << "            " << targetHeight << "," << std::endl
              << "            \"" << Common::podToHex(blockHash) << "\"," << std::endl
              << "            UINT64_C(" << alreadyGeneratedCoins << "), // alreadyGeneratedCoins" << std::endl
              << "            UINT64_C(" << cumulativeDifficulty << "), // cumulativeDifficulty" << std::endl
              << "            UINT64_C(" << alreadyGeneratedTransactions << "), // alreadyGeneratedTransactions" << std::endl
              << "        }," << std::endl << std::endl;

    std::cout << InformationMsg("Also verify height ") << targetHeight
              << InformationMsg(" is in CryptoNoteCheckpoints.h with hash ")
              << Common::podToHex(blockHash) << "." << std::endl;

    return true;
}

bool DaemonCommandsHandler::sync_height_status(const std::vector<std::string> &args)
{
    const uint32_t syncFloor = m_core.getSyncFloorHeight();
    const uint64_t topIndex  = m_core.getTopBlockIndex();

    if (syncFloor == 0)
    {
        std::cout << InformationMsg("Sync-from-height: ")
                  << SuccessMsg("disabled (synced from genesis)") << std::endl;
    }
    else
    {
        std::cout << InformationMsg("Sync floor height: ") << SuccessMsg(syncFloor) << std::endl
                  << InformationMsg("Current chain top:  ") << SuccessMsg(topIndex) << std::endl
                  << InformationMsg("Mode: ")
                  << SuccessMsg("bootstrapped – blocks below ") << syncFloor
                  << SuccessMsg(" are trusted via checkpoint, not stored locally") << std::endl;
    }
    return true;
}

bool DaemonCommandsHandler::save(const std::vector<std::string> &args)
{
    try
    {
        m_core.save();
        std::cout << SuccessMsg("Blockchain state saved.") << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cout << WarningMsg("Save failed: ") << e.what() << std::endl;
    }
    return true;
}

