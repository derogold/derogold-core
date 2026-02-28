// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "IDataBase.h"
#include "rocksdb/db.h"

#include <atomic>
#include <logging/LoggerRef.h>
#include <mutex>
#include <memory>
#include <string>

namespace CryptoNote
{
    class RocksDBWrapper final : public IDataBase
    {
        enum State
        {
            NOT_INITIALIZED,
            INITIALIZED
        };

        const Logging::LoggerRef logger;
        const DataBaseConfig config;

        std::atomic<State> state;
        std::atomic<bool> optimizeRunning {false};
        std::atomic<bool> optimizeCancelRequested {false};

        std::unique_ptr<rocksdb::DB> db;
        std::mutex optimizeMutex;
        rocksdb::DB *optimizeDbHandle = nullptr;

    public:
        inline static const std::string DB_NAME = "DB";

        RocksDBWrapper(const std::shared_ptr<Logging::ILogger> &logger, DataBaseConfig config) :
            logger(logger, "RocksDBWrapper"),
            config(std::move(config)),
            state(NOT_INITIALIZED)
        {
        }

        RocksDBWrapper(const RocksDBWrapper &) = delete;
        RocksDBWrapper(RocksDBWrapper &&) = delete;

        RocksDBWrapper &operator=(const RocksDBWrapper &) = delete;
        RocksDBWrapper &operator=(RocksDBWrapper &&) = delete;

        void init() override;

        void shutdown() override;

        void destroy() override;

        std::error_code write(IWriteBatch &batch) override;

        std::error_code read(IReadBatch &batch) override;

        std::error_code readThreadSafe(IReadBatch &batch) override;

        void recreate() override;

        void optimize() override;

        bool cancelOptimize() override;

        const DataBaseConfig &getConfig() const override { return config; }

    private:
        static rocksdb::Options getDBOptions(const DataBaseConfig &config);

        static void getDBOptions(const DataBaseConfig &config, rocksdb::DBOptions &dbOptions, std::vector<rocksdb::ColumnFamilyDescriptor> &columnFamilies);

        static std::string getDataDir(const DataBaseConfig &config)
        {
            return config.dataDir + '/' + DB_NAME;
        }
    };
} // namespace CryptoNote
