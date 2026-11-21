// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "WalletTypes.h"
#include "httplib.h"

#include <atomic>
#include <config/CryptoNoteConfig.h>
#include <logger/Logger.h>
#include <rpc/CoreRpcServerCommandsDefinitions.h>
#include <string>
#include <thread>
#include <unordered_set>

class Nigel
{
  public:
    /////////////////////////
    /* Public Constructors */
    /////////////////////////

    Nigel(const std::string daemonHost, const uint16_t daemonPort, const bool daemonSSL);

    Nigel(
        const std::string daemonHost,
        const uint16_t daemonPort,
        const bool daemonSSL,
        const std::chrono::seconds timeout);

    ~Nigel();

    /////////////////////////////
    /* Public member functions */
    /////////////////////////////

    void init();

    void swapNode(const std::string daemonHost, const uint16_t daemonPort, const bool daemonSSL);

    void decreaseRequestedBlockCount();

    void resetRequestedBlockCount();

    /* Returns whether we've received info from the daemon at some point */
    bool isOnline() const;

    uint64_t localDaemonBlockCount() const;

    uint64_t networkBlockCount() const;

    uint64_t peerCount() const;

    uint64_t hashrate() const;

    /* Empty when nothing is wrong. Otherwise what the daemon last said about
       why it will not serve this wallet blocks - a thing retrying does not
       fix, so somebody has to be told. */
    std::string syncError() const;

    static std::string extractDaemonError(const std::string &body);

  private:
    void setSyncError(const std::string &error);

  public:

    std::tuple<uint64_t, std::string> nodeFee() const;

    std::tuple<std::string, uint16_t, bool> nodeAddress() const;

    /* Returns {success, blocks, topBlock, pruneFloor}.
       pruneFloor > 0 means the daemon skipped a pruned range; the wallet should
       advance its startHeight to pruneFloor on its next request. */
    std::tuple<bool, std::vector<WalletTypes::WalletBlockInfo>, std::optional<WalletTypes::TopBlock>, uint64_t>
        getWalletSyncData(
            const std::vector<Crypto::Hash> blockHashCheckpoints,
            const uint64_t startHeight,
            const uint64_t startTimestamp,
            const bool skipCoinbaseTransactions);

    /* Returns a bool on success or not */
    bool getTransactionsStatus(
        const std::unordered_set<Crypto::Hash> transactionHashes,
        std::unordered_set<Crypto::Hash> &transactionsInPool,
        std::unordered_set<Crypto::Hash> &transactionsInBlock,
        std::unordered_set<Crypto::Hash> &transactionsUnknown) const;

    std::tuple<bool, std::vector<CryptoNote::RandomOuts>>
        getRandomOutsByAmounts(const std::vector<uint64_t> amounts, const uint64_t requestedOuts) const;

    /* {success, connectionError, errorMessage} */
    std::tuple<bool, bool, std::string> sendTransaction(const CryptoNote::Transaction tx) const;

    std::tuple<bool, std::unordered_map<Crypto::Hash, std::vector<uint64_t>>>
        getGlobalIndexesForRange(const uint64_t startHeight, const uint64_t endHeight) const;

  private:
    //////////////////////////////
    /* Private member functions */
    //////////////////////////////

    void stop();

    void backgroundRefresh();

    bool getDaemonInfo();

    bool getFeeInfo();

    template<typename F>
    auto tryParseJSONResponse(
        const httplib::Result &res,
        const std::string &failMessage,
        const F parseFunc,
        const bool verifyStatus = true) const -> std::optional<decltype(parseFunc(nlohmann::json()))>
    {
        if (res)
        {
            if (res->status == 200)
            {
                try
                {
                    nlohmann::json j = nlohmann::json::parse(res->body);

                    /* Only serialise the response back to text when the trace
                       level will actually record it. Building this string
                       unconditionally re-serialised every block batch the
                       daemon sent, just to throw it away. */
                    if (Logger::logger.getLogLevel() >= Logger::TRACE)
                    {
                        Logger::logger.log(
                            "Got response from daemon: " + j.dump(),
                            Logger::TRACE,
                            { Logger::SYNC, Logger::DAEMON }
                        );
                    }

                    if (verifyStatus)
                    {
                        const auto status = j.at("status").get<std::string>();

                        if (status != "OK")
                        {
                            Logger::logger.log(
                                failMessage + " - Expected status \"OK\", got " + status,
                                Logger::INFO,
                                { Logger::SYNC, Logger::DAEMON }
                            );

                            return std::nullopt;
                        }
                    }

                    return parseFunc(j);
                }
                /* Catch everything the parse callback can throw, not just JSON
                   errors. Hex decoding and the transaction input variant cast
                   both throw plain std::runtime_error / boost::bad_get on
                   malformed daemon data, and those escaped this handler into
                   the download thread, which has no handler of its own and so
                   terminated the whole wallet. nlohmann's exceptions derive
                   from std::exception, so this still covers them. */
                catch (const std::exception &e)
                {
                    Logger::logger.log(
                        failMessage + ": " + std::string(e.what()),
                        Logger::INFO,
                        { Logger::SYNC, Logger::DAEMON }
                    );

                    return std::nullopt;
                }
                catch (...)
                {
                    Logger::logger.log(
                        failMessage + ": unknown error parsing daemon response",
                        Logger::INFO,
                        { Logger::SYNC, Logger::DAEMON }
                    );

                    return std::nullopt;
                }
            }
            else
            {
                std::stringstream stream;

                stream << failMessage << " - got status code " << res->status;

                if (!res->body.empty())
                {
                    stream << ", body: " << res->body;
                }

                Logger::logger.log(
                    stream.str(),
                    Logger::INFO,
                    { Logger::SYNC, Logger::DAEMON }
                );

                return std::nullopt;
            }
        }
        else
        {
            Logger::logger.log(
                failMessage + " - failed to open socket or timed out.",
                Logger::INFO,
                { Logger::SYNC, Logger::DAEMON }
            );

            return std::nullopt;
        }
    }

    //////////////////////////////
    /* Private member variables */
    //////////////////////////////

    /* Stores our http client (Don't really care about it launching threads
       and making our functions non const) */
    std::shared_ptr<httplib::Client> m_nodeClient = nullptr;

    /* Stores the HTTP headers included in all Nigel requests */
    httplib::Headers m_requestHeaders;

    /* Runs a background refresh on height, hashrate, etc */
    std::thread m_backgroundThread;

    /* If we should stop the background thread */
    std::atomic<bool> m_shouldStop = false;

    /* Stores how many blocks we'll try to sync */
    std::atomic<uint64_t> m_blockCount = CryptoNote::BLOCKS_SYNCHRONIZING_DEFAULT_COUNT;

    /* The amount of blocks the daemon we're connected to has */
    std::atomic<uint64_t> m_localDaemonBlockCount = 0;

    /* The amount of blocks the network has */
    std::atomic<uint64_t> m_networkBlockCount = 0;

    /* The amount of peers we're connected to */
    std::atomic<uint64_t> m_peerCount = 0;

    /* The hashrate (based on the last local block the daemon has synced) */
    std::atomic<uint64_t> m_lastKnownHashrate = 0;

    /* Set when the daemon refuses this wallet's sync request for a reason of
       its own, cleared the moment one succeeds. Read by whoever is asking for
       wallet status, written by the download thread. */
    mutable std::mutex m_syncErrorMutex;

    std::string m_syncError;

    /* Whether the daemon is a blockchain cache API
       see: https://github.com/TurtlePay/blockchain-cache-api */
    std::atomic<bool> m_isBlockchainCache = false;

    /* The address to send the node fee to (May be "") */
    std::string m_nodeFeeAddress;

    /* The fee the node charges */
    uint64_t m_nodeFeeAmount = 0;

    /* The timeout on requests */
    std::chrono::seconds m_timeout;

    /* The daemon hostname */
    std::string m_daemonHost;

    /* The daemon port */
    uint16_t m_daemonPort;

    /* If the daemon is SSL */
    bool m_daemonSSL = false;

    /* Whether we should use /getrawblocks instead of /getwalletsyncdata */
    bool m_useRawBlocks = true;
};
