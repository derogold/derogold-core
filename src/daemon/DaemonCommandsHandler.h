// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "common/ConsoleHandler.h"
#include "daemon/DaemonConfiguration.h"
#include "logging/LoggerManager.h"
#include "logging/LoggerRef.h"
#include "rpc/CoreRpcServerCommandsDefinitions.h"
#include "rpc/JsonRpc.h"
#include "rpc/RpcServer.h"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <ostream>
#include <thread>
#include <unordered_map>

namespace CryptoNote
{
    class Core;
    class IDataBase;
    class NodeServer;
} // namespace CryptoNote

class DaemonCommandsHandler
{
public:
    ~DaemonCommandsHandler();

    DaemonCommandsHandler(CryptoNote::Core &core,
                          CryptoNote::NodeServer &srv,
                          const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> &syncManager,
                          const std::shared_ptr<Logging::LoggerManager> &log,
                          const std::string &ip,
                          uint32_t port,
                          const std::shared_ptr<CryptoNote::IDataBase> &database,
                          DaemonConfig::DaemonConfiguration config,
                          std::shared_ptr<std::atomic<bool>> pruneTrigger = nullptr);

    bool start_handling()
    {
        m_consoleHandler.start();
        return true;
    }

    void stop_handling()
    {
        m_consoleHandler.stop();
    }

    bool exit(const std::vector<std::string> &args);

    /* Runs one console command and returns what it printed, instead of
       printing it. This is what a console attached over the RPC socket calls;
       it goes through the same handler map as the local console, so the two
       can never drift apart. */
    std::string run_remote_command(const std::string &commandLine);

private:
    Common::ConsoleHandler m_consoleHandler;

    CryptoNote::Core &m_core;

    CryptoNote::NodeServer &m_srv;

    const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> m_syncManager;

    httplib::Client m_rpcServer;

    Logging::LoggerRef logger;

    DaemonConfig::DaemonConfiguration m_config;

    std::shared_ptr<Logging::LoggerManager> m_logManager;
    std::shared_ptr<CryptoNote::IDataBase> m_database;
    std::shared_ptr<std::atomic<bool>> m_pruneTrigger;

    std::unordered_map<std::string, std::chrono::system_clock::time_point> m_bannedHosts;
    std::future<void> m_compactDbTask;
    std::atomic<bool> m_compactDbRunning {false};
    std::atomic<bool> m_compactDbLastSuccess {true};
    std::atomic<bool> m_compactDbHasRun {false};
    std::atomic<bool> m_stopCompactDbScheduler {false};
    std::chrono::steady_clock::time_point m_compactDbStart;
    uint64_t m_compactDbStartedAtEpoch = 0;
    uint64_t m_compactDbFinishedAtEpoch = 0;
    uint64_t m_compactDbStartedAtHeight = 0;
    uint64_t m_compactDbFinishedAtHeight = 0;
    std::atomic<uint64_t> m_compactDbSchedulerCheckIntervalSeconds {60};
    uint32_t m_compactDbNearSyncStreak = 0;
    std::string m_compactDbLastError;
    std::mutex m_compactDbMutex;
    std::thread m_compactDbSchedulerThread;

    void refresh_compaction_state_locked();
    bool start_compaction_locked(const std::string &reason);
    void compact_db_scheduler_loop();

    std::string get_commands_str() const;

    bool print_block_by_height(uint32_t height);

    bool print_block_by_hash(const std::string &arg);

    bool help(const std::vector<std::string> &args);

    bool print_pl(const std::vector<std::string> &args);

    bool print_cn(const std::vector<std::string> &args);

    bool set_log(const std::vector<std::string> &args);

    bool print_block(const std::vector<std::string> &args);

    bool print_tx(const std::vector<std::string> &args);

    bool print_pool(const std::vector<std::string> &args);

    bool print_pool_sh(const std::vector<std::string> &args);

    bool status(const std::vector<std::string> &args);

    bool ban(const std::vector<std::string> &args);

    bool compact_db(const std::vector<std::string> &args);

    bool db_status(const std::vector<std::string> &args);

    bool prune_status(const std::vector<std::string> &args);

    bool export_bootstrap_state(const std::vector<std::string> &args);

    bool sync_height_status(const std::vector<std::string> &args);

    bool sync_info(const std::vector<std::string> &args);

    bool save(const std::vector<std::string> &args);

    /* Where command output goes. Commands write to out() rather than
       std::cout, so the same command can print to the terminal or be captured
       for a socket without knowing which. */
    std::ostream *m_out = &std::cout;

    std::ostream &out()
    {
        return *m_out;
    }

    /* One command at a time. The local console runs on its own thread and a
       console attached over the socket runs on an httplib worker; without this
       the two would interleave their output into the same stream, and the
       redirect below would be visible to whichever was not expecting it. */
    std::mutex m_commandMutex;
};
