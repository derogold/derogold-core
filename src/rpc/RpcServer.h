// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "httplib.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <cryptonotecore/Core.h>
#include <cryptonoteprotocol/CryptoNoteProtocolHandlerCommon.h>
#include <errors/Errors.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <p2p/NetNode.h>
#include <string>
#include <thread>

enum class RpcMode
{
    Default = 0,
    BlockExplorerEnabled = 1,
    AllMethodsEnabled = 2,
};

class RpcServer
{
  public:
    ////////////////////////////////
    /* Constructors / Destructors */
    ////////////////////////////////
    RpcServer(
        uint16_t bindPort,
        std::string rpcBindIp,
        std::string corsHeader,
        std::string feeAddress,
        uint64_t feeAmount,
        RpcMode rpcMode,
        std::shared_ptr<CryptoNote::Core> core,
        std::shared_ptr<CryptoNote::NodeServer> p2p,
        std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> syncManager,
        std::string ipcPath = "",
        uint32_t ipcMode = 0600,
        std::string ipcGroup = "");

    ~RpcServer();

    /////////////////////////////
    /* Public member functions */
    /////////////////////////////

    /* Starts the server. */
    void start();

    /* Stops the server. */
    void stop();

    /* Gets the IP/port combo the server is running on */
    std::tuple<std::string, uint16_t> getConnectionInfo();

    /* The local socket being served, or empty when none was asked for or the
       bind failed. Empty is what tells the daemon not to advertise an endpoint
       that is not there. */
    std::string getIpcPath() const;

    /* Runs one console command inside the daemon and returns what it printed.
       Installed by the daemon once its command handler exists, and cleared
       before that handler goes away. */
    using ConsoleExecutor = std::function<std::string(const std::string &commandLine)>;

    void setConsoleExecutor(ConsoleExecutor executor);

  private:
    //////////////////////////////
    /* Private member functions */
    //////////////////////////////

    /* Registers every route on one listener. Called once for the TCP server
       and again for the local socket, because httplib keeps its handlers per
       instance. isIpc decides whether the console route is among them. */
    void setupRoutes(httplib::Server &srv, bool isIpc);

    /* Starts listening for requests on the server */
    void listen();

    /* Serves the local socket. Separate from listen() because the two block
       on different instances. */
    void listenIpc();

    std::optional<rapidjson::Document>
        getJsonBody(const httplib::Request &req, httplib::Response &res, bool bodyRequired);

    /* Handles stuff like parsing json and then forwards onto the handler */
    void middleware(
        const httplib::Request &req,
        httplib::Response &res,
        RpcMode routePermissions,
        bool bodyRequired,
        bool syncRequired,
        const std::function<std::tuple<Error, uint16_t>(
            const httplib::Request &req,
            httplib::Response &res,
            const rapidjson::Document &body)>& handler);

    void failRequest(int errorCode, const std::string& body, httplib::Response &res);

    /* What both wallet sync routes say when a wallet shares no block with this
       chain. The wallet matches on it to tell that apart from a daemon that is
       merely unwell, so it is one string in one place. */
    static const std::string NO_COMMON_ANCESTOR_MESSAGE;

    void failJsonRpcRequest(int64_t errorCode, const std::string &errorMessage, httplib::Response &res);

    uint64_t calculateTotalFeeAmount(const std::vector<Crypto::Hash> &transactionHashes);

    void generateBlockHeader(
        const Crypto::Hash &blockHash,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        bool headerOnly = false);

    void generateTransactionPrefix(
        const CryptoNote::Transaction &transaction,
        rapidjson::Writer<rapidjson::StringBuffer> &writer);

    /////////////////////
    /* OPTION REQUESTS */
    /////////////////////

    void handleOptions(const httplib::Request &req, httplib::Response &res) const;

    //////////////////
    /* GET REQUESTS */
    //////////////////

    std::tuple<Error, uint16_t>
        info(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        fee(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        height(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        peers(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    ///////////////////
    /* POST REQUESTS */
    ///////////////////

    std::tuple<Error, uint16_t>
        sendTransaction(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getRandomOuts(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getWalletSyncData(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getGlobalIndexes(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        queryBlocksLite(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getTransactionsStatus(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getPoolChanges(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        queryBlocksDetailed(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    /* Deprecated. Use getGlobalIndexes instead. */
    std::tuple<Error, uint16_t> getGlobalIndexesDeprecated(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getRawBlocks(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    ///////////////////////
    /* JSON RPC REQUESTS */
    ///////////////////////

    std::tuple<Error, uint16_t>
        getBlockTemplateJsonRpc(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        submitBlockJsonRpc(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getBlockCountJsonRpc(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getBlockHashForHeightJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getLastBlockHeaderJsonRpc(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getBlockHeaderByHashJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getBlockHeaderByHeightJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t>
        getBlocksByHeightJsonRpc(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getBlockDetailsByHashJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getTransactionDetailsByHashJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getTransactionsInPoolJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    std::tuple<Error, uint16_t> getTransactionHashesByPaymentIdJsonRpc(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body);

    /* Runs a daemon console command. Registered on the local socket only -
       these commands stop the node, ban peers, change log levels and start
       compactions, and the mode on the socket file is what decides who may
       ask. Never reachable over TCP, token or no token. */
    std::tuple<Error, uint16_t>
        console(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body);

    //////////////////////////////
    /* Private member variables */
    //////////////////////////////

    /* Our server instance */
    httplib::Server m_server;

    /* The local socket listener, built only when a path was configured. A
       separate httplib::Server because one instance binds one address. */
    std::unique_ptr<httplib::Server> m_ipcServer;

    /* Empty when no local socket is being served. */
    std::string m_ipcPath;

    const uint32_t m_ipcMode;

    const std::string m_ipcGroup;

    std::thread m_ipcThread;

    mutable std::mutex m_consoleExecutorMutex;

    ConsoleExecutor m_consoleExecutor;

    /* The server host */
    const std::string m_host;

    /* The server port */
    const uint16_t m_port;

    /* The header to use with 'Access-Control-Allow-Origin'. If empty string,
     * header is not added. */
    const std::string m_corsHeader;

    /* The thread running the server */
    std::thread m_serverThread;

    /* The address to return from the /fee endpoint */
    const std::string m_feeAddress;

    /* The amount to return from the /fee endpoint */
    const uint64_t m_feeAmount;

    /* RPC methods that are enabled */
    const RpcMode m_rpcMode;

    /* A pointer to our CryptoNoteCore instance */
    const std::shared_ptr<CryptoNote::Core> m_core;

    /* A pointer to our P2P stack */
    const std::shared_ptr<CryptoNote::NodeServer> m_p2p;

    const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> m_syncManager;

    /* Use turtle api instead of xmr variant */
};
