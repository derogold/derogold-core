// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

////////////////////////
#include <nigel/Nigel.h>
////////////////////////

#include <common/CryptoNoteTools.h>
#include <common/IpcSocket.h>
#include <config/CryptoNoteConfig.h>
#include <cryptonotecore/CachedBlock.h>
#include <cryptonotecore/Core.h>
#include <CryptoNote.h>
#include <errors/ValidateParameters.h>
#include <utilities/Utilities.h>
#include <version.h>

using json = nlohmann::json;

////////////////////////////////
/*   Inline helper methods    */
////////////////////////////////

inline std::shared_ptr<httplib::Client> getClient(
    const std::string daemonHost,
    const uint16_t daemonPort,
    const bool daemonSSL,
    const std::chrono::seconds timeout)
{
    std::shared_ptr<httplib::Client> client;

    /* A daemon address that is an absolute path, or an "@name" abstract
       socket, names a local socket rather than a host. Nothing resolvable can
       look like either, so the two cannot be confused. The port is meaningless
       for a socket; httplib wants one anyway and ignores it. */
    if (Common::Ipc::looksLikePath(daemonHost))
    {
        client = std::make_shared<httplib::Client>(daemonHost.c_str(), 80);
        Common::Ipc::configureClient(*client);

        client->set_connection_timeout(timeout);
        client->set_read_timeout(timeout);
        client->set_write_timeout(timeout);

        return client;
    }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (daemonSSL)
    {
        client = std::make_shared<httplib::SSLClient>(daemonHost.c_str(), daemonPort);
    }
    else
    {
#endif
        client = std::make_shared<httplib::Client>(daemonHost.c_str(), daemonPort);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    }
#endif

    client->set_connection_timeout(timeout);

    /* Set the read and write timeouts explicitly. Only the connection timeout
       was set, leaving these at whatever the library defaults to, which has
       changed between cpp-httplib versions and is five seconds in the pinned
       one. A daemon that needs longer than that to start a response had its
       request abandoned mid-flight while it carried on building the answer, so
       the wallet retried and the daemon piled up duplicate work. */
    client->set_read_timeout(timeout);
    client->set_write_timeout(timeout);

    return client;
}

////////////////////////////////
/* Constructors / Destructors */
////////////////////////////////

Nigel::Nigel(const std::string daemonHost, const uint16_t daemonPort, const bool daemonSSL):
    Nigel(daemonHost, daemonPort, daemonSSL, std::chrono::seconds(10))
{
}

Nigel::Nigel(
    const std::string daemonHost,
    const uint16_t daemonPort,
    const bool daemonSSL,
    const std::chrono::seconds timeout):
    m_timeout(timeout),
    m_daemonHost(daemonHost),
    m_daemonPort(daemonPort),
    m_daemonSSL(daemonSSL)
{
    std::stringstream userAgent;
    userAgent << "Nigel/" << PROJECT_VERSION_LONG;

    m_requestHeaders = {{"User-Agent", userAgent.str()}};
    m_nodeClient = getClient(m_daemonHost, m_daemonPort, m_daemonSSL, m_timeout);
}

Nigel::~Nigel()
{
    stop();
}

//////////////////////
/* Member functions */
//////////////////////

void Nigel::swapNode(const std::string daemonHost, const uint16_t daemonPort, const bool daemonSSL)
{
    stop();

    m_blockCount = CryptoNote::BLOCKS_SYNCHRONIZING_DEFAULT_COUNT;
    m_localDaemonBlockCount = 0;
    m_networkBlockCount = 0;
    m_peerCount = 0;
    m_lastKnownHashrate = 0;
    m_isBlockchainCache = false;
    m_nodeFeeAddress = "";
    m_nodeFeeAmount = 0;
    m_useRawBlocks = true;

    m_daemonHost = daemonHost;
    m_daemonPort = daemonPort;
    m_daemonSSL = daemonSSL;

    m_nodeClient = getClient(m_daemonHost, m_daemonPort, m_daemonSSL, m_timeout);

    init();
}

void Nigel::decreaseRequestedBlockCount()
{
    if (m_blockCount > 1)
    {
        m_blockCount = m_blockCount / 2;
    }
}

void Nigel::resetRequestedBlockCount()
{
    m_blockCount = CryptoNote::BLOCKS_SYNCHRONIZING_DEFAULT_COUNT;
}

std::tuple<bool, std::vector<WalletTypes::WalletBlockInfo>, std::optional<WalletTypes::TopBlock>, uint64_t>
    Nigel::getWalletSyncData(
        const std::vector<Crypto::Hash> blockHashCheckpoints,
        const uint64_t startHeight,
        const uint64_t startTimestamp,
        const bool skipCoinbaseTransactions)
{
    Logger::logger.log("Fetching blocks from the daemon", Logger::DEBUG, {Logger::SYNC, Logger::DAEMON});

    json j = {{"blockHashCheckpoints", blockHashCheckpoints},
              {"startHeight", startHeight},
              {"startTimestamp", startTimestamp},
              {"blockCount", m_blockCount.load()},
              {"skipCoinbaseTransactions", skipCoinbaseTransactions}};

    const std::string endpoint = m_useRawBlocks ? "/getrawblocks" : "/getwalletsyncdata";

    Logger::logger.log(
        "Sending " + endpoint + " request to daemon: " + j.dump(),
        Logger::TRACE,
        { Logger::SYNC, Logger::DAEMON }
    );

    const auto res = m_nodeClient->Post(endpoint, m_requestHeaders, j.dump(), "application/json");

    /* A 400 here is the daemon answering this request rather than failing at
       it - it has looked at our block hashes and will not serve us. Retrying
       cannot change that answer, so carry the reason up to where somebody can
       read it instead of asking again forever behind a height that never
       moves. */
    if (res && res->status == 400)
    {
        setSyncError(extractDaemonError(res->body));

        return {false, {}, std::nullopt, 0};
    }

    /* Daemon doesn't support /getrawblocks, fall back to /getwalletsyncdata */
    if (res && res->status == 404 && m_useRawBlocks)
    {
        m_useRawBlocks = false;

        return getWalletSyncData(
            blockHashCheckpoints,
            startHeight,
            startTimestamp,
            skipCoinbaseTransactions
        );
    }

    const auto parsedResponse = tryParseJSONResponse(
        res,
        "Failed to fetch blocks from daemon",
        [this, skipCoinbaseTransactions](const nlohmann::json j) {

        std::vector<WalletTypes::WalletBlockInfo> items;

        if (m_useRawBlocks)
        {
            const auto rawBlocks = j.at("items").get<std::vector<CryptoNote::RawBlock>>();

            for (const auto &rawBlock : rawBlocks)
            {
                CryptoNote::BlockTemplate block;

                fromBinaryArray(block, rawBlock.block);

                WalletTypes::WalletBlockInfo walletBlock;

                CryptoNote::CachedBlock cachedBlock(block);

                walletBlock.blockHeight = cachedBlock.getBlockIndex();
                walletBlock.blockHash = cachedBlock.getBlockHash();
                walletBlock.blockPrevHash = block.previousBlockHash;
                walletBlock.blockTimestamp = block.timestamp;

                if (!skipCoinbaseTransactions)
                {
                    walletBlock.coinbaseTransaction = CryptoNote::Core::getRawCoinbaseTransaction(block.baseTransaction);
                }

                for (const auto &transaction : rawBlock.transactions)
                {
                    walletBlock.transactions.push_back(CryptoNote::Core::getRawTransaction(transaction));
                }

                items.push_back(walletBlock);
            }
        }
        else
        {
            items = j.at("items").get<std::vector<WalletTypes::WalletBlockInfo>>();
        }

        /* If the daemon sent pre-parsed wallet data for a pruned height range,
           prepend those items so the wallet processes all heights in order.
           Applies to both /getrawblocks and /getwalletsyncdata endpoints. */
        if (j.find("prunedItems") != j.end())
        {
            auto prunedItems = j.at("prunedItems").get<std::vector<WalletTypes::WalletBlockInfo>>();
            items.insert(items.begin(), prunedItems.begin(), prunedItems.end());
        }

        std::optional<WalletTypes::TopBlock> topBlock;

        if (j.find("synced") != j.end() && j.find("topBlock") != j.end() && j.at("synced").get<bool>())
        {
            topBlock = j.at("topBlock").get<WalletTypes::TopBlock>();
        }

        const uint64_t pruneFloor =
            (j.find("pruneFloor") != j.end()) ? j.at("pruneFloor").get<uint64_t>() : 0;

        return std::make_tuple(items, topBlock, pruneFloor);
    });

    if (parsedResponse)
    {
        const auto [ items, topBlock, pruneFloor ] = *parsedResponse;

        /* Served, so whatever the daemon last refused us for no longer holds. */
        setSyncError("");

        return { true, items, topBlock, pruneFloor };
    }

    return { false, {}, std::nullopt, 0 };
}

/* Pulls the reason out of a daemon's failure body, which is
   {"status": "Failed", "error": "..."}. A body that is not that shape still
   has to say something, so it is reported as it arrived. */
std::string Nigel::extractDaemonError(const std::string &body)
{
    try
    {
        const auto j = nlohmann::json::parse(body);

        if (j.find("error") != j.end() && j.at("error").is_string())
        {
            return j.at("error").get<std::string>();
        }
    }
    catch (const std::exception &)
    {
    }

    if (body.empty())
    {
        return "The daemon refused to serve this wallet blocks, and gave no reason";
    }

    return body;
}

void Nigel::stop()
{
    m_shouldStop = true;

    if (m_backgroundThread.joinable())
    {
        m_backgroundThread.join();
    }
}

void Nigel::init()
{
    m_shouldStop = false;

    /* Get the initial daemon info, and the initial fee info before returning.
       This way the info is always valid, and there's no race on accessing
       the fee info or something */
    getDaemonInfo();

    getFeeInfo();

    /* Now launch the background thread to constantly update the heights etc */
    m_backgroundThread = std::thread(&Nigel::backgroundRefresh, this);
}

bool Nigel::getDaemonInfo()
{
    Logger::logger.log("Updating daemon info", Logger::DEBUG, {Logger::SYNC, Logger::DAEMON});

    Logger::logger.log(
        "Sending /info request to daemon",
        Logger::TRACE,
        { Logger::SYNC, Logger::DAEMON }
    );

    auto res = m_nodeClient->Get("/info", m_requestHeaders);

    const auto parsedResponse = tryParseJSONResponse(res, "Failed to update daemon info", [this](const nlohmann::json j) {
        m_localDaemonBlockCount = j.at("height").get<uint64_t>();

        /* Height returned is one more than the current height - but we
           don't want to overflow is the height returned is zero */
        if (m_localDaemonBlockCount != 0)
        {
            m_localDaemonBlockCount--;
        }

        m_networkBlockCount = j.at("network_height").get<uint64_t>();

        /* Height returned is one more than the current height - but we
           don't want to overflow is the height returned is zero */
        if (m_networkBlockCount != 0)
        {
            m_networkBlockCount--;
        }

        /* A daemon that does not report its connections is still worth having
           heights from, so a missing count is zero rather than a thrown-away
           update. */
        m_peerCount = j.value("incoming_connections_count", static_cast<uint64_t>(0))
                      + j.value("outgoing_connections_count", static_cast<uint64_t>(0));

        /* The daemon divides the difficulty by the block time in force at its
           own height and ships the result in this same response, so take it
           from there.

           This used to divide by DIFFICULTY_TARGET here - the launch-era ten
           seconds - on a chain that has targeted three hundred since
           DIFFICULTY_TARGET_V3_HEIGHT, so every wallet reported thirty times
           the real network hashrate. Daemons predating the field fall back to
           the same division, against the right target this time. */
        if (j.find("hashrate") != j.end())
        {
            m_lastKnownHashrate = j.at("hashrate").get<uint64_t>();
        }
        else if (j.find("difficulty") != j.end())
        {
            const uint64_t blockTime = CryptoNote::parameters::getCurrentDifficultyTarget(m_networkBlockCount);

            m_lastKnownHashrate = j.at("difficulty").get<uint64_t>() / blockTime;
        }

        /* Look to see if the isCacheApi property exists in the response
           and if so, set the internal value to whatever it found */
        if (j.find("isCacheApi") != j.end())
        {
            m_isBlockchainCache = j.at("isCacheApi").get<bool>();
        }

        return true;
    });

    return parsedResponse.has_value();
}

bool Nigel::getFeeInfo()
{
    Logger::logger.log("Fetching fee info", Logger::DEBUG, {Logger::DAEMON});

    Logger::logger.log(
        "Sending /fee request to daemon",
        Logger::TRACE,
        { Logger::SYNC, Logger::DAEMON }
    );

    auto res = m_nodeClient->Get("/fee", m_requestHeaders);

    const auto parsedResponse = tryParseJSONResponse(res, "Failed to update fee info", [this](const nlohmann::json j) {
        std::string tmpAddress = j.at("address").get<std::string>();

        /* Read the full width the daemon writes. Reading it as uint32 silently
           wrapped any node fee at or above 2^32 atomic units, so the wallet
           could attach a fee far smaller than the node asked for and have the
           transaction rejected. */
        const uint64_t tmpFee = j.at("amount").get<uint64_t>();

        const bool integratedAddressesAllowed = false;

        Error error = validateAddresses({tmpAddress}, integratedAddressesAllowed);

        if (!error)
        {
            m_nodeFeeAddress = tmpAddress;
            m_nodeFeeAmount = tmpFee;
        }

        return true;
    });

    return parsedResponse.has_value();
}

void Nigel::backgroundRefresh()
{
    while (!m_shouldStop)
    {
        getDaemonInfo();

        Utilities::sleepUnlessStopping(std::chrono::seconds(10), m_shouldStop);
    }
}

bool Nigel::isOnline() const
{
    return m_localDaemonBlockCount != 0 || m_networkBlockCount != 0 || m_peerCount != 0 || m_lastKnownHashrate != 0;
}

uint64_t Nigel::localDaemonBlockCount() const
{
    return m_localDaemonBlockCount;
}

uint64_t Nigel::networkBlockCount() const
{
    return m_networkBlockCount;
}

uint64_t Nigel::peerCount() const
{
    return m_peerCount;
}

uint64_t Nigel::hashrate() const
{
    return m_lastKnownHashrate;
}

std::string Nigel::syncError() const
{
    std::scoped_lock lock(m_syncErrorMutex);

    return m_syncError;
}

void Nigel::setSyncError(const std::string &error)
{
    std::scoped_lock lock(m_syncErrorMutex);

    if (m_syncError != error)
    {
        if (error.empty())
        {
            Logger::logger.log("Daemon is serving this wallet again", Logger::INFO, {Logger::SYNC, Logger::DAEMON});
        }
        else
        {
            Logger::logger.log(error, Logger::WARNING, {Logger::SYNC, Logger::DAEMON});
        }
    }

    m_syncError = error;
}

std::tuple<uint64_t, std::string> Nigel::nodeFee() const
{
    return {m_nodeFeeAmount, m_nodeFeeAddress};
}

std::tuple<std::string, uint16_t, bool> Nigel::nodeAddress() const
{
    return {m_daemonHost, m_daemonPort, m_daemonSSL};
}

bool Nigel::getTransactionsStatus(
    const std::unordered_set<Crypto::Hash> transactionHashes,
    std::unordered_set<Crypto::Hash> &transactionsInPool,
    std::unordered_set<Crypto::Hash> &transactionsInBlock,
    std::unordered_set<Crypto::Hash> &transactionsUnknown) const
{
    json j = {{"transactionHashes", transactionHashes}};

    Logger::logger.log(
        "Sending /get_transactions_status request to daemon: " + j.dump(),
        Logger::TRACE,
        { Logger::SYNC, Logger::DAEMON }
    );

    auto res = m_nodeClient->Post("/get_transactions_status", m_requestHeaders, j.dump(), "application/json");

    const auto parsedResponse = tryParseJSONResponse(res, "Failed to get transactions status", [&](const nlohmann::json j) {
        transactionsInPool = j.at("transactionsInPool").get<std::unordered_set<Crypto::Hash>>();
        transactionsInBlock = j.at("transactionsInBlock").get<std::unordered_set<Crypto::Hash>>();
        transactionsUnknown = j.at("transactionsUnknown").get<std::unordered_set<Crypto::Hash>>();

        return true;
    });

    return parsedResponse.has_value();
}

std::tuple<bool, std::vector<CryptoNote::RandomOuts>>
    Nigel::getRandomOutsByAmounts(const std::vector<uint64_t> amounts, const uint64_t requestedOuts) const
{
    json j = {{"amounts", amounts}, {"outs_count", requestedOuts}};

    /* The blockchain cache doesn't call it outs_count
       it calls it mixin */
    if (m_isBlockchainCache)
    {
        j.erase("outs_count");
        j["mixin"] = requestedOuts;

        Logger::logger.log(
            "Sending /randomOutputs request to daemon: " + j.dump(),
            Logger::TRACE,
            { Logger::SYNC, Logger::DAEMON }
        );

        /* We also need to handle the request and response a bit
           differently so we'll do this here */
        auto res = m_nodeClient->Post("/randomOutputs", m_requestHeaders, j.dump(), "application/json");

        const auto parsedResponse = tryParseJSONResponse(res, "Failed to get random outs", [](const nlohmann::json j) {
            return j.get<std::vector<CryptoNote::RandomOuts>>();
        }, false);

        if (parsedResponse)
        {
            return {true, *parsedResponse};
        }
    }
    else
    {
        Logger::logger.log(
            "Sending /getrandom_outs request to daemon: " + j.dump(),
            Logger::TRACE,
            { Logger::SYNC, Logger::DAEMON }
        );

        auto res = m_nodeClient->Post("/getrandom_outs", m_requestHeaders, j.dump(), "application/json");

        const auto parsedResponse = tryParseJSONResponse(res, "Failed to get random outs", [](const nlohmann::json j) {
            return j.at("outs").get<std::vector<CryptoNote::RandomOuts>>();
        });

        if (parsedResponse)
        {
            return {true, *parsedResponse};
        }
    }

    return {false, {}};
}

std::tuple<bool, bool, std::string> Nigel::sendTransaction(const CryptoNote::Transaction tx) const
{
    json j = {{"tx_as_hex", Common::toHex(CryptoNote::toBinaryArray(tx))}};

    Logger::logger.log(
        "Sending /sendrawtransaction request to daemon: " + j.dump(),
        Logger::TRACE,
        { Logger::SYNC, Logger::DAEMON }
    );

    auto res = m_nodeClient->Post("/sendrawtransaction", m_requestHeaders, j.dump(), "application/json");

    bool success = false;
    bool connectionError = true;
    std::string error;

    tryParseJSONResponse(res, "Failed to send transaction", [&](const nlohmann::json j) {
        connectionError = false;

        success = j.at("status").get<std::string>() == "OK";

        if (j.find("error") != j.end())
        {
            error = j.at("error").get<std::string>();
        }

        return true;
    }, false);

    return {success, connectionError, error};
}

std::tuple<bool, std::unordered_map<Crypto::Hash, std::vector<uint64_t>>>
    Nigel::getGlobalIndexesForRange(const uint64_t startHeight, const uint64_t endHeight) const
{
    /* Blockchain cache API does not support this method and we
       don't need it to because it returns the global indexes
       with the key outputs when we get the wallet sync data */
    if (m_isBlockchainCache)
    {
        return {false, {}};
    }

    json j = {{"startHeight", startHeight}, {"endHeight", endHeight}};

    Logger::logger.log(
        "Sending /get_global_indexes_for_range request to daemon: " + j.dump(),
        Logger::TRACE,
        { Logger::SYNC, Logger::DAEMON }
    );

    auto res = m_nodeClient->Post("/get_global_indexes_for_range", m_requestHeaders, j.dump(), "application/json");

    std::unordered_map<Crypto::Hash, std::vector<uint64_t>> result;

    const auto parsedResponse = tryParseJSONResponse(res, "Failed to get global indexes for range", [&result](const nlohmann::json j) {
        /* The daemon doesn't serialize the way nlohmann::json does, so
           we can't just .get<std::unordered_map ...> */
        nlohmann::json indexes = j.at("indexes");

        for (const auto &index : indexes)
        {
            result[index.at("key").get<Crypto::Hash>()] = index.at("value").get<std::vector<uint64_t>>();
        }

        return true;
    });

    return {parsedResponse.has_value(), result};
}
