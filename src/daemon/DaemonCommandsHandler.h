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
    DaemonCommandsHandler(CryptoNote::Core &core,
                          CryptoNote::NodeServer &srv,
                          const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> &syncManager,
                          const std::shared_ptr<Logging::LoggerManager> &log,
                          const std::string &ip,
                          uint32_t port,
                          const std::shared_ptr<CryptoNote::IDataBase> &database,
                          DaemonConfig::DaemonConfiguration config);

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

    std::unordered_map<std::string, std::chrono::system_clock::time_point> m_bannedHosts;
    std::future<void> m_compactDbTask;
    std::atomic<bool> m_compactDbRunning {false};
    std::atomic<bool> m_compactDbLastSuccess {true};
    std::atomic<bool> m_compactDbHasRun {false};
    std::chrono::steady_clock::time_point m_compactDbStart;
    std::string m_compactDbLastError;
    std::mutex m_compactDbMutex;

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

    bool save(const std::vector<std::string> &args);
};
