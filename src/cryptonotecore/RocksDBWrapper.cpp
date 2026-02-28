// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2018-2020, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#include "RocksDBWrapper.h"

#include "DBUtils.h"
#include "DataBaseErrors.h"
#include "common/ScopeExit.h"

#include <rocksdb/filter_policy.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/options_util.h>
#include <algorithm>
#include <utility>
#include <thread>

namespace CryptoNote
{
    void RocksDBWrapper::init()
    {
        if (state.load() != NOT_INITIALIZED)
        {
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::ALREADY_INITIALIZED));
        }

        const std::string dataDir = getDataDir(config);

        logger(Logging::INFO) << "Opening DB in " << dataDir;

        rocksdb::DB *dbPtr;
        const rocksdb::Options dbOptions = getDBOptions(config);

        if (const rocksdb::Status status = rocksdb::DB::Open(dbOptions, dataDir, &dbPtr); status.ok())
        {
            logger(Logging::INFO) << "DB opened in " << dataDir;
        }
        else if (status.IsIOError())
        {
            logger(Logging::ERROR) << "DB Error. DB can't be opened in " << dataDir << ". Error: " << status.ToString();
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::IO_ERROR));
        }
        else
        {
            logger(Logging::ERROR) << "DB Error. DB can't be opened in " << dataDir << ". Error: " << status.ToString();
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::INTERNAL_ERROR));
        }

        db.reset(dbPtr);
        state.store(INITIALIZED);
    }

    void RocksDBWrapper::shutdown()
    {
        if (state.load() != INITIALIZED)
        {
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::NOT_INITIALIZED));
        }

        logger(Logging::INFO) << "Closing DB.";
        db->Flush(rocksdb::FlushOptions());
        db->SyncWAL();
        db.reset();
        state.store(NOT_INITIALIZED);
    }

    void RocksDBWrapper::destroy()
    {
        if (state.load() != NOT_INITIALIZED)
        {
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::ALREADY_INITIALIZED));
        }

        const std::string dataDir = getDataDir(config);

        logger(Logging::WARNING) << "Destroying DB in " << dataDir;

        const rocksdb::Options dbOptions = getDBOptions(config);

        if (const rocksdb::Status status = DestroyDB(dataDir, dbOptions); status.ok())
        {
            logger(Logging::WARNING) << "DB destroyed in " << dataDir;
        }
        else
        {
            logger(Logging::ERROR) << "DB Error. DB can't be destroyed in " << dataDir
                                   << ". Error: " << status.ToString();
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::INTERNAL_ERROR));
        }
    }

    std::error_code RocksDBWrapper::write(IWriteBatch &batch)
    {
        if (state.load() != INITIALIZED)
        {
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::NOT_INITIALIZED));
        }

        rocksdb::WriteBatch rocksdbBatch;

        for (const auto &[key, value] : batch.extractRawDataToInsert())
        {
            rocksdbBatch.Put(rocksdb::Slice(key), rocksdb::Slice(value));
        }

        for (const std::string &key : batch.extractRawKeysToRemove())
        {
            rocksdbBatch.Delete(rocksdb::Slice(key));
        }

        if (const rocksdb::Status status = db->Write(rocksdb::WriteOptions(), &rocksdbBatch); !status.ok())
        {
            logger(Logging::ERROR) << "Can't write to DB. " << status.ToString();
            return make_error_code(error::DataBaseErrorCodes::INTERNAL_ERROR);
        }

        return std::error_code {};
    }

    std::error_code RocksDBWrapper::read(IReadBatch &batch)
    {
        if (state.load() != INITIALIZED)
        {
            throw std::system_error(make_error_code(error::DataBaseErrorCodes::NOT_INITIALIZED));
        }

        const std::vector rawKeys(batch.getRawKeys());

        if (rawKeys.empty())
        {
            logger(Logging::ERROR) << "RocksDBWrapper::read: detected rawKeys.size() == 0!!!";
            return make_error_code(error::DataBaseErrorCodes::INTERNAL_ERROR);
        }

        std::vector<rocksdb::Slice> keySlices;
        keySlices.reserve(rawKeys.size());

        for (const std::string &key : rawKeys)
        {
            keySlices.emplace_back(key);
        }

        std::vector<std::string> values;
        values.reserve(rawKeys.size());

        const std::vector<rocksdb::Status> statuses = db->MultiGet(rocksdb::ReadOptions(), keySlices, &values);

        std::vector<bool> resultStates;
        resultStates.reserve(rawKeys.size());

        for (const rocksdb::Status &status : statuses)
        {
            if (!status.ok() && !status.IsNotFound())
            {
                return make_error_code(error::DataBaseErrorCodes::INTERNAL_ERROR);
            }

            resultStates.push_back(status.ok());
        }

        batch.submitRawResult(values, resultStates);
        return std::error_code {};
    }

    std::error_code RocksDBWrapper::readThreadSafe(IReadBatch &batch)
    {
        if (state.load() != INITIALIZED)
        {
            throw std::runtime_error("Not initialized.");
        }

        const std::vector rawKeys(batch.getRawKeys());
        std::vector<std::string> values(rawKeys.size());
        std::vector<bool> resultStates;

        size_t i = 0;

        for (const std::string &key : rawKeys)
        {
            const auto status = db->Get(rocksdb::ReadOptions(), rocksdb::Slice(key), &values[i++]);

            if (!status.ok() && !status.IsNotFound())
            {
                return make_error_code(error::DataBaseErrorCodes::INTERNAL_ERROR);
            }

            resultStates.push_back(status.ok());
        }

        batch.submitRawResult(values, resultStates);
        return std::error_code {};
    }

    void RocksDBWrapper::recreate()
    {
        if (state.load() == INITIALIZED)
        {
            shutdown();
        }

        destroy();
        init();
    }

    void RocksDBWrapper::optimize()
    {
        if (optimizeRunning.exchange(true))
        {
            throw std::runtime_error("Database compaction is already running");
        }

        optimizeCancelRequested.store(false);

        const std::string dbData = getDataDir(config);
        const rocksdb::Options dbOptions = getDBOptions(config);
        rocksdb::DB *rocksDb = nullptr;

        auto clearOptimizeHandle = [&]()
        {
            std::lock_guard<std::mutex> lock(optimizeMutex);
            optimizeDbHandle = nullptr;
        };

        auto setOptimizeHandle = [&](rocksdb::DB *handle)
        {
            std::lock_guard<std::mutex> lock(optimizeMutex);
            optimizeDbHandle = handle;
        };

        const auto stopOptimize = Common::ScopeExit([&]
                                                    {
                                                        clearOptimizeHandle();
                                                        optimizeRunning.store(false);
                                                    });
        (void) stopOptimize;

        const rocksdb::Status openStatus = rocksdb::DB::Open(dbOptions, dbData, &rocksDb);
        if (!openStatus.ok())
        {
            throw std::runtime_error("Failed to open DB for optimization: " + openStatus.ToString());
        }

        setOptimizeHandle(rocksDb);

        rocksdb::CompactRangeOptions compactRangeOptions;
        compactRangeOptions.exclusive_manual_compaction = true;
        compactRangeOptions.change_level = true;

        logger(Logging::INFO) << "Preparing to optimize DB for reading... This may take a long time.";
        logger(Logging::INFO) << "Please do not close the program abruptly to prevent DB corruption.";

        std::atomic<bool> monitorStop(false);
        std::thread monitorThread([&]
                                  {
                                      while (!monitorStop.load())
                                      {
                                          std::string value;
                                          rocksDb->GetProperty("rocksdb.stats", &value);
                                          auto ss = std::stringstream {value};
                                          std::stringstream output;
                                          bool stop = false;

                                          for (std::string line; std::getline(ss, line, '\n');)
                                          {
                                              if (line.find("**") != std::string::npos)
                                              {
                                                  if (stop) break;
                                                  stop = true;
                                              }

                                              output << line << std::endl;
                                          }

                                          logger(Logging::INFO) << output.str();

                                          for (size_t i = 0; i < 60 && !monitorStop.load(); ++i)
                                          {
                                              std::this_thread::sleep_for(std::chrono::seconds(1));
                                          }
                                      }
                                  });

        rocksdb::Status compactStatus = rocksDb->CompactRange(compactRangeOptions, nullptr, nullptr);

        auto waitForCompactOptions = rocksdb::WaitForCompactOptions();
        waitForCompactOptions.flush = true;
        waitForCompactOptions.close_db = true;
        const rocksdb::Status waitStatus = rocksDb->WaitForCompact(waitForCompactOptions);

        monitorStop.store(true);
        if (monitorThread.joinable())
        {
            monitorThread.join();
        }

        delete rocksDb;
        rocksDb = nullptr;
        clearOptimizeHandle();

        if (optimizeCancelRequested.load())
        {
            throw std::runtime_error("DB compaction was cancelled");
        }

        if (!compactStatus.ok())
        {
            throw std::runtime_error("DB compaction failed: " + compactStatus.ToString());
        }

        if (!waitStatus.ok())
        {
            throw std::runtime_error("DB wait-for-compaction failed: " + waitStatus.ToString());
        }

        logger(Logging::INFO) << "Optimized DeroGold DB.";
    }

    bool RocksDBWrapper::cancelOptimize()
    {
        if (!optimizeRunning.load())
        {
            return false;
        }

        optimizeCancelRequested.store(true);

        std::lock_guard<std::mutex> lock(optimizeMutex);
        if (optimizeDbHandle == nullptr)
        {
            return true;
        }

        optimizeDbHandle->CancelAllBackgroundWork(false);
        return true;
    }

    rocksdb::Options RocksDBWrapper::getDBOptions(const DataBaseConfig &config)
    {
        rocksdb::DBOptions dbOptions;
        dbOptions.create_if_missing = true;
        dbOptions.create_missing_column_families = true;
        dbOptions.info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
        dbOptions.keep_log_file_num = 1;
        dbOptions.IncreaseParallelism(static_cast<int>(config.backgroundThreadsCount));
        dbOptions.max_open_files = static_cast<int>(config.maxOpenFiles);
        dbOptions.skip_stats_update_on_db_open = true;
        dbOptions.compaction_readahead_size = 2 * 1024 * 1024;

        rocksdb::ColumnFamilyOptions cfOptions;

        // Set size of a single memtable.
        cfOptions.write_buffer_size = static_cast<size_t>(config.writeBufferSize);
        // merge two memtables when flushing to L0
        cfOptions.min_write_buffer_number_to_merge = 2;
        // Reduce write stalls by allowing more immutable memtables.
        cfOptions.max_write_buffer_number = 6;
        // Delay compaction trigger slightly to improve write throughput.
        cfOptions.level0_file_num_compaction_trigger = 20;
        cfOptions.level0_slowdown_writes_trigger = 30;
        cfOptions.level0_stop_writes_trigger = 40;

        cfOptions.target_file_size_base = std::max<uint64_t>(config.writeBufferSize / 2, 8ULL * 1024 * 1024);
        cfOptions.max_bytes_for_level_base = std::max<uint64_t>(config.writeBufferSize * 4, 64ULL * 1024 * 1024);

        cfOptions.num_levels = 7;
        cfOptions.target_file_size_multiplier = 2;

        // level style compaction
        cfOptions.compaction_style = rocksdb::kCompactionStyleLevel;
        cfOptions.compression_per_level.resize(cfOptions.num_levels);

        const auto compressionLevel = config.compressionEnabled ? rocksdb::kZSTD : rocksdb::kNoCompression;

        for (int i = 0; i < cfOptions.num_levels; ++i)
        {
            // don't compress l0 & l1
            cfOptions.compression_per_level[i] = i < 2 ? rocksdb::kNoCompression : compressionLevel;
        }

        cfOptions.bottommost_compression = compressionLevel;

        rocksdb::BlockBasedTableOptions bbtOptions;
        bbtOptions.data_block_index_type = rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndHash;
        bbtOptions.data_block_hash_table_util_ratio = 0.75;
        bbtOptions.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10));
        bbtOptions.block_cache = rocksdb::NewLRUCache(config.readCacheSize);
        bbtOptions.block_size = 32 * 1024;

        cfOptions.table_factory.reset(NewBlockBasedTableFactory(bbtOptions));
        cfOptions.memtable_prefix_bloom_size_ratio = 0.02;
        cfOptions.memtable_whole_key_filtering = true;

        return rocksdb::Options {dbOptions, cfOptions};
    }

    void RocksDBWrapper::getDBOptions(const DataBaseConfig &config,
                                      rocksdb::DBOptions &dbOptions,
                                      std::vector<rocksdb::ColumnFamilyDescriptor> &columnFamilies)
    {
        dbOptions.create_if_missing = true;
        dbOptions.create_missing_column_families = true;
        dbOptions.info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
        dbOptions.keep_log_file_num = 1;
        dbOptions.IncreaseParallelism(static_cast<int>(config.backgroundThreadsCount));
        dbOptions.max_open_files = static_cast<int>(config.maxOpenFiles);
        dbOptions.skip_stats_update_on_db_open = true;
        dbOptions.compaction_readahead_size = 2 * 1024 * 1024;

        rocksdb::ColumnFamilyOptions cfOptions;
        cfOptions.OptimizeLevelStyleCompaction();
        cfOptions.compression_per_level.resize(cfOptions.num_levels);
        const auto compressionLevel = config.compressionEnabled ? rocksdb::kZSTD : rocksdb::kNoCompression;

        for (int i = 0; i < cfOptions.num_levels; ++i)
        {
            cfOptions.compression_per_level[i] = i < 2 ? rocksdb::kNoCompression : compressionLevel;
        }
        cfOptions.bottommost_compression = compressionLevel;

        rocksdb::BlockBasedTableOptions bbtOptions;
        bbtOptions.data_block_index_type = rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndHash;
        bbtOptions.data_block_hash_table_util_ratio = 0.75;
        bbtOptions.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10));
        bbtOptions.block_size = 32 * 1024;

        cfOptions.table_factory.reset(NewBlockBasedTableFactory(bbtOptions));
        cfOptions.memtable_prefix_bloom_size_ratio = 0.02;
        cfOptions.memtable_whole_key_filtering = true;

        columnFamilies.emplace_back(DB::V2::RAW_BLOCKS_CF, cfOptions);
        columnFamilies.emplace_back("SpentKeyImages", cfOptions);
        columnFamilies.emplace_back("CachedTransactions", cfOptions);
        columnFamilies.emplace_back("PaymentIds", cfOptions);
        columnFamilies.emplace_back("CachedBlocks", cfOptions);
        columnFamilies.emplace_back("KeyOutputGlobalIndexes", cfOptions);
        columnFamilies.emplace_back("Others", cfOptions);
    }
} // namespace CryptoNote
