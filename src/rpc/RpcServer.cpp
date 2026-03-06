// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

//////////////////////////
#include <rpc/RpcServer.h>
//////////////////////////

#include <iostream>

#include "version.h"

#include <config/Constants.h>
#include <common/CryptoNoteTools.h>
#include <errors/ValidateParameters.h>
#include <logger/Logger.h>
#include <serialization/SerializationTools.h>
#include <utilities/Addresses.h>
#include <utilities/ColouredMsg.h>
#include <utilities/FormatTools.h>
#include <utilities/ParseExtra.h>

RpcServer::RpcServer(
    const uint16_t bindPort,
    const std::string rpcBindIp,
    const std::string corsHeader,
    const std::string feeAddress,
    const uint64_t feeAmount,
    const RpcMode rpcMode,
    const std::shared_ptr<CryptoNote::Core> core,
    const std::shared_ptr<CryptoNote::NodeServer> p2p,
    const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> syncManager,
    const bool useTrtlApi):
    m_port(bindPort),
    m_host(rpcBindIp),
    m_corsHeader(corsHeader),
    m_feeAddress(feeAddress),
    m_feeAmount(feeAmount),
    m_rpcMode(rpcMode),
    m_core(core),
    m_p2p(p2p),
    m_syncManager(syncManager),
    m_useTrtlApi(useTrtlApi)
{
    if (!m_feeAddress.empty())
    {
        Error error = validateAddresses({m_feeAddress}, false);

        if (error != SUCCESS)
        {
            std::cout << WarningMsg("Fee address given is not valid: " + error.getErrorMessage()) << std::endl;
            exit(1);
        }
    }

    const bool bodyRequired = true;
    const bool bodyNotRequired = false;

    const bool syncRequired = true;
    const bool syncNotRequired = false;

    /* Route the request through our middleware function, before forwarding
       to the specified function */
    const auto router = [this](const auto function, const RpcMode routePermissions, const bool isBodyRequired, const bool syncRequired) {
        return [=](const httplib::Request &req, httplib::Response &res) {
            /* Pass the inputted function with the arguments passed through
               to middleware */
            middleware(
                req,
                res,
                routePermissions,
                isBodyRequired,
                syncRequired,
                std::bind(function, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
            );
        };
    };

    const auto jsonRpc = [this, router, bodyRequired, bodyNotRequired, syncRequired, syncNotRequired](const auto &req, auto &res) {
        const auto body = getJsonBody(req, res, true);

        if (!body)
        {
            return;
        }

        if (!hasMember(*body, "method"))
        {
            failRequest(400, "Missing JSON parameter: 'method'", res);
            return;
        }

        const auto method = getStringFromJSON(*body, "method");

        if (method == "getblocktemplate")
        {
            router(&RpcServer::getBlockTemplateJsonRpc, RpcMode::Default, bodyRequired, syncRequired)(req, res);
        }
        else if (method == "submitblock")
        {
            router(&RpcServer::submitBlockJsonRpc, RpcMode::Default, bodyRequired, syncRequired)(req, res);
        }
        else if (method == "getblockcount")
        {
            router(&RpcServer::getBlockCountJsonRpc, RpcMode::Default, bodyNotRequired, syncNotRequired)(req, res);
        }
        else if (method == "getlastblockheader")
        {
            router(&RpcServer::getLastBlockHeaderJsonRpc, RpcMode::Default, bodyNotRequired, syncNotRequired)(req, res);
        }
        else if (method == "getblockheaderbyhash")
        {
            router(&RpcServer::getBlockHeaderByHashJsonRpc, RpcMode::Default, bodyRequired, syncNotRequired)(req, res);
        }
        else if (method == "getblockheaderbyheight")
        {
            router(&RpcServer::getBlockHeaderByHeightJsonRpc, RpcMode::Default, bodyRequired, syncNotRequired)(req, res);
        }
        else if (method == "f_blocks_list_json")
        {
            router(&RpcServer::getBlocksByHeightJsonRpc, RpcMode::BlockExplorerEnabled, bodyRequired, syncNotRequired)(req, res);
        }
        else if (method == "f_block_json")
        {
            router(
                &RpcServer::getBlockDetailsByHashJsonRpc, RpcMode::BlockExplorerEnabled, bodyRequired, syncNotRequired)(req, res);
        }
        else if (method == "f_transaction_json")
        {
            router(
                &RpcServer::getTransactionDetailsByHashJsonRpc, RpcMode::BlockExplorerEnabled, bodyRequired, syncNotRequired)(req, res);
        }
        else if (method == "f_on_transactions_pool_json")
        {
            router(
                &RpcServer::getTransactionsInPoolJsonRpc, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired)(req, res);
        }
        else
        {
            res.status = 404;
        }
    };

    if (m_useTrtlApi)
    {
        m_server
            .Post("/block", router(&RpcServer::submitBlockTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))

            /* /block/{hash} */
            .Get("/block/([a-fA-F0-9]{64})", router(&RpcServer::getBlockHeaderByHashTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            /* /block/{height} */
            .Get("/block/(\\d+)", router(&RpcServer::getBlockHeaderByHeightTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            /* /block/{hash}/raw */
            .Get("/block/([a-fA-F0-9]{64})/raw", router(&RpcServer::getRawBlockByHashTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            /* /block/{height}/raw */
            .Get("/block/(\\d+)/raw", router(&RpcServer::getRawBlockByHeightTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            .Get("/block/count", router(&RpcServer::getBlockCountTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            /* /block/headers/{height} */
            .Get("/block/headers/(\\d+)", router(&RpcServer::getBlocksByHeightTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            .Get("/block/last", router(&RpcServer::getLastBlockHeaderTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Post("/block/template", router(&RpcServer::getBlockTemplateTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))

            .Get("/fee", router(&RpcServer::feeTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Get("/height", router(&RpcServer::heightTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))

            .Get("/indexes/(\\d+)/(\\d+)", router(&RpcServer::getGlobalIndexesTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Post("/indexes/random", router(&RpcServer::getRandomOutsTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))

            .Get("/info", router(&RpcServer::infoTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Get("/peers", router(&RpcServer::peersTrtlApi, RpcMode::Default, bodyNotRequired, syncNotRequired))

            .Post("/sync", router(&RpcServer::getWalletSyncDataTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/sync/raw", router(&RpcServer::getRawBlocksTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))

            .Post("/transaction", router(&RpcServer::sendTransactionTrtlApi, RpcMode::Default, bodyRequired, syncRequired))
            /* /transaction/{hash} */
            .Get("/transaction/([a-fA-F0-9]{64})", router(&RpcServer::getTransactionDetailsByHashTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            /* /transaction/{hash}/raw */
            .Get("/transaction/([a-fA-F0-9]{64})/raw", router(&RpcServer::getRawTransactionByHashTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            .Get("/transaction/pool", router(&RpcServer::getTransactionsInPoolTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            .Post("/transaction/pool/delta", router(&RpcServer::getPoolChangesTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))
            .Get("/transaction/pool/raw", router(&RpcServer::getRawTransactionsInPoolTrtlApi, RpcMode::BlockExplorerEnabled, bodyNotRequired, syncNotRequired))
            .Post("/transaction/status", router(&RpcServer::getTransactionsStatusTrtlApi, RpcMode::Default, bodyRequired, syncNotRequired))

            .Options(".*", [this](auto &req, auto &res) { handleOptions(req, res); });
    }
    else
    {
        m_server
            .Get("/json_rpc", jsonRpc)
            .Get("/info", router(&RpcServer::info, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Get("/fee", router(&RpcServer::fee, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Get("/height", router(&RpcServer::height, RpcMode::Default, bodyNotRequired, syncNotRequired))
            .Get("/peers", router(&RpcServer::peers, RpcMode::Default, bodyNotRequired, syncNotRequired))

            .Post("/json_rpc", jsonRpc)
            .Post("/sendrawtransaction", router(&RpcServer::sendTransaction, RpcMode::Default, bodyRequired, syncRequired))
            .Post("/getrandom_outs", router(&RpcServer::getRandomOuts, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/getwalletsyncdata", router(&RpcServer::getWalletSyncData, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/get_global_indexes_for_range", router(&RpcServer::getGlobalIndexes, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/queryblockslite", router(&RpcServer::queryBlocksLite, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/get_transactions_status", router(&RpcServer::getTransactionsStatus, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/get_pool_changes_lite", router(&RpcServer::getPoolChanges, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/queryblocksdetailed", router(&RpcServer::queryBlocksDetailed, RpcMode::AllMethodsEnabled, bodyRequired, syncNotRequired))
            .Post("/get_o_indexes", router(&RpcServer::getGlobalIndexesDeprecated, RpcMode::Default, bodyRequired, syncNotRequired))
            .Post("/getrawblocks", router(&RpcServer::getRawBlocks, RpcMode::Default, bodyRequired, syncNotRequired))

            /* Matches everything */
            /* NOTE: Not passing through middleware */
            .Options(".*", [this](auto &req, auto &res) { handleOptions(req, res); });
    }
}

RpcServer::~RpcServer()
{
    stop();
}

void RpcServer::start()
{
    m_serverThread = std::thread(&RpcServer::listen, this);
}

void RpcServer::listen()
{
    const auto listenError = m_server.listen(m_host, m_port);

    if (!listenError)
    {
        std::cout << WarningMsg("Failed to start RPC server.") << std::endl;
        exit(1);
    }
}

void RpcServer::stop()
{
    m_server.stop();

    if (m_serverThread.joinable())
    {
        m_serverThread.join();
    }
}

std::tuple<std::string, uint16_t> RpcServer::getConnectionInfo()
{
    return {m_host, m_port};
}

std::optional<rapidjson::Document> RpcServer::getJsonBody(
    const httplib::Request &req,
    httplib::Response &res,
    const bool bodyRequired)
{
    rapidjson::Document jsonBody;

    if (!bodyRequired)
    {
        /* Some compilers are stupid and can't figure out just `return jsonBody`
         * and we can't construct a std::optional(jsonBody) since the copy
         * constructor is deleted, so we need to std::move */
        return std::optional<rapidjson::Document>(std::move(jsonBody));
    }

    if (jsonBody.Parse(req.body.c_str()).HasParseError())
    {
        std::stringstream stream;

        if (!req.body.empty())
        {
            stream << "Warning: received body is not JSON encoded!\n"
                   << "Key/value parameters are NOT supported.\n"
                   << "Body:\n" << req.body;

            Logger::logger.log(
                stream.str(),
                Logger::INFO,
                { Logger::DAEMON_RPC }
            );
        }

        stream << "Failed to parse request body as JSON";

        if (m_useTrtlApi)
        {
            failRequest(Error(API_INVALID_ARGUMENT, stream.str()), res);
            res.status = 400;
        }
        else
        {
            failRequest(400, stream.str(), res);
        }

        return std::nullopt;
    }

    return std::optional<rapidjson::Document>(std::move(jsonBody));
}

void RpcServer::middleware(
    const httplib::Request &req,
    httplib::Response &res,
    const RpcMode routePermissions,
    const bool bodyRequired,
    const bool syncRequired,
    const std::function<std::tuple<Error, uint16_t>(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body)>& handler)
{
    Logger::logger.log(
        "[" + req.get_header_value("REMOTE_ADDR") + "] Incoming " + req.method + " request: " + req.path + ", User-Agent: " + req.get_header_value("User-Agent"),
        Logger::DEBUG,
        { Logger::DAEMON_RPC }
    );

    if (!m_corsHeader.empty())
    {
        res.set_header("Access-Control-Allow-Origin", m_corsHeader);
    }

    res.set_header("Content-Type", "application/json");

    const auto jsonBody = getJsonBody(req, res, bodyRequired);

    if (!jsonBody)
    {
        return;
    }

    /* If this route requires higher permissions than we have enabled, then
     * reject the request */
    if (routePermissions > m_rpcMode)
    {
        if (m_useTrtlApi)
        {
            failRequest(Error(API_BLOCKEXPLORER_DISABLED), res);
            res.status = 403;
        }
        else
        {
            std::stringstream stream;

            stream << "You do not have permission to access this method. Please "
                      "relaunch your daemon with the --enable-blockexplorer";

            if (routePermissions == RpcMode::AllMethodsEnabled)
            {
                stream << "-detailed";
            }

            stream << " command line option to access this method.";

            failRequest(403, stream.str(), res);
        }

        return;
    }

    const uint64_t height = m_core->getTopBlockIndex() + 1;
    const uint64_t networkHeight = std::max(1u, m_syncManager->getBlockchainHeight());

    const bool areSynced = m_p2p->get_payload_object().isSynchronized() && height >= networkHeight;

    if (syncRequired && !areSynced)
    {
        if (m_useTrtlApi)
        {
            failRequest(Error(API_NODE_NOT_SYNCED), res);
            res.status = 503;
        }
        else
        {
            failRequest(200, "Daemon must be synced to process this RPC method call, please retry when synced", res);
        }

        return;
    }

    try
    {
        const auto [error, statusCode] = handler(req, res, *jsonBody);

        if (error)
        {
            if (m_useTrtlApi)
            {
                failRequest(error, res);
                res.status = statusCode;
            }
            else
            {
                rapidjson::StringBuffer sb;
                rapidjson::Writer writer(sb);

                writer.StartObject();

                writer.Key("errorCode");
                writer.Uint(error.getErrorCode());

                writer.Key("errorMessage");
                writer.String(error.getErrorMessage());

                writer.EndObject();

                res.body = sb.GetString();
                res.status = 400;
            }
        }
        else
        {
            res.status = statusCode;
        }

        return;
    }
    catch (const std::invalid_argument &e)
    {
        Logger::logger.log(
            "Caught JSON exception, likely missing required json parameter: " + std::string(e.what()),
            Logger::FATAL,
            { Logger::DAEMON_RPC }
        );

        if (m_useTrtlApi)
        {
            failRequest(Error(API_INVALID_ARGUMENT, e.what()), res);
            res.status = 400;
        }
        else
        {
            failRequest(400, e.what(), res);
        }
    }
    catch (const std::exception &e)
    {
        std::stringstream error;

        error << "Caught unexpected exception: " << e.what() << " while processing "
              << req.path << " request for User-Agent: " << req.get_header_value("User-Agent");

        Logger::logger.log(error.str(), Logger::FATAL, { Logger::DAEMON_RPC });

        if (!req.body.empty())
        {
            Logger::logger.log("Body: " + req.body, Logger::FATAL, { Logger::DAEMON_RPC });
        }

        if (m_useTrtlApi)
        {
            failRequest(Error(API_INTERNAL_ERROR, e.what()), res);
            res.status = 500;
        }
        else
        {
            failRequest(500, "Internal server error: " + std::string(e.what()), res);
        }
    }
}

void RpcServer::failRequest(const int errorCode, const std::string& body, httplib::Response &res)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();

    writer.Key("status");
    writer.String("Failed");

    writer.Key("error");
    writer.String(body);

    writer.EndObject();

    res.body = sb.GetString();
    res.status = errorCode;
}

void RpcServer::failRequest(const Error& error, httplib::Response &res)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();
    {
        writer.Key("error");
        writer.StartObject();
        {
            writer.Key("code");
            writer.Uint(error.getErrorCode());

            writer.Key("message");
            writer.String(error.getErrorMessage());
        }
        writer.EndObject();
    }
    writer.EndObject();
}

void RpcServer::failJsonRpcRequest(const int64_t errorCode, const std::string& errorMessage, httplib::Response &res)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();
    {
        writer.Key("jsonrpc");
        writer.String("2.0");

        writer.Key("error");
        writer.StartObject();
        {
            writer.Key("message");
            writer.String(errorMessage);

            writer.Key("code");
            writer.Int64(errorCode);
        }
        writer.EndObject();
    }
    writer.EndObject();

    res.body = sb.GetString();
    res.status = 200;
}

uint64_t RpcServer::calculateTotalFeeAmount(const std::vector<Crypto::Hash> &transactionHashes)
{
    uint64_t totalFeeAmount = 0;

    std::vector<std::vector<uint8_t>> transactions;
    std::vector<Crypto::Hash> ignore;

    m_core->getTransactions(transactionHashes, transactions, ignore);

    for (const std::vector<uint8_t>& rawTX : transactions)
    {
        CryptoNote::Transaction tx;

        fromBinaryArray(tx, rawTX);

        const uint64_t outputAmount = std::accumulate(
            tx.outputs.begin(),
            tx.outputs.end(),
            0ull,
            [](const auto acc, const auto out) { return acc + out.amount; });

        const uint64_t inputAmount = std::accumulate(
            tx.inputs.begin(),
            tx.inputs.end(),
            0ull,
            [](const auto acc, const auto in)
            {
                if (in.type() == typeid(CryptoNote::KeyInput))
                {
                    return acc + boost::get<CryptoNote::KeyInput>(in).amount;
                }

                return acc;
            });

        const uint64_t fee = inputAmount - outputAmount;

        totalFeeAmount += fee;
    }

    return totalFeeAmount;
}

void RpcServer::generateBlockHeader(
    const Crypto::Hash &blockHash,
    rapidjson::Writer<rapidjson::StringBuffer> &writer,
    const bool headerOnly)
{
    CryptoNote::BlockTemplate block = m_core->getBlockByHash(blockHash);
    CryptoNote::CachedBlock cachedBlock(block);

    const auto topHeight = m_core->getTopBlockIndex();
    const auto height = cachedBlock.getBlockIndex();

    const auto outputs = block.baseTransaction.outputs;

    const auto extraDetails = m_core->getBlockDetails(blockHash);

    const uint64_t reward = std::accumulate(
        outputs.begin(), outputs.end(), 0ull, [](const auto acc, const auto out) { return acc + out.amount; });
    const uint64_t totalFeeAmount = calculateTotalFeeAmount(block.transactionHashes);

    writer.StartObject();
    {
        writer.Key("alreadyGeneratedCoins");
        writer.String(std::to_string(extraDetails.alreadyGeneratedCoins));

        writer.Key("alreadyGeneratedTransactions");
        writer.Uint64(extraDetails.alreadyGeneratedTransactions);

        writer.Key("baseReward");
        writer.Uint64(extraDetails.baseReward);

        writer.Key("depth");
        writer.Uint64(topHeight - height);

        writer.Key("difficulty");
        writer.Uint64(m_core->getBlockDifficulty(height));

        writer.Key("hash");
        blockHash.toJSON(writer);

        writer.Key("height");
        writer.Uint64(height);

        writer.Key("majorVersion");
        writer.Uint64(block.majorVersion);

        writer.Key("minorVersion");
        writer.Uint64(block.minorVersion);

        writer.Key("nonce");
        writer.Uint64(block.nonce);

        writer.Key("orphan");
        writer.Bool(extraDetails.isAlternative);

        writer.Key("penalty");
        writer.Double(extraDetails.penalty);

        writer.Key("prevHash");
        block.previousBlockHash.toJSON(writer);

        writer.Key("reward");
        writer.Uint64(reward);

        writer.Key("size");
        writer.Uint64(extraDetails.blockSize);

        writer.Key("sizeMedian");
        writer.Uint64(extraDetails.sizeMedian);

        writer.Key("timestamp");
        writer.Uint64(block.timestamp);

        writer.Key("totalFeeAmount");
        writer.Uint64(totalFeeAmount);

        writer.Key("transactionCount");
        writer.Uint64(extraDetails.transactions.size());

        /* If we are not part of a sub-object (such as /transaction) then we can
         * include basic information about the transactions */
        if (!headerOnly)
        {
            writer.Key("transactions");
            writer.StartArray();
            {
                /* Coinbase transaction */
                writer.StartObject();
                {
                    const auto txOutputs = block.baseTransaction.outputs;

                    const uint64_t outputAmount = std::accumulate(
                        txOutputs.begin(),
                        txOutputs.end(),
                        0ull,
                        [](const auto acc, const auto out) { return acc + out.amount; });

                    writer.Key("amountOut");
                    writer.Uint64(outputAmount);

                    writer.Key("fee");
                    writer.Uint64(0);

                    writer.Key("hash");
                    const auto baseTransactionBranch = getObjectHash(block.baseTransaction);
                    baseTransactionBranch.toJSON(writer);

                    writer.Key("size");
                    writer.Uint64(getObjectBinarySize(block.baseTransaction));
                }
                writer.EndObject();

                std::vector<std::vector<uint8_t>> transactions;
                std::vector<Crypto::Hash> ignore;
                m_core->getTransactions(block.transactionHashes, transactions, ignore);

                for (const std::vector<uint8_t> &rawTX : transactions)
                {
                    writer.StartObject();
                    {
                        CryptoNote::Transaction tx;

                        fromBinaryArray(tx, rawTX);

                        const uint64_t outputAmount = std::accumulate(
                            tx.outputs.begin(),
                            tx.outputs.end(),
                            0ull,
                            [](const auto acc, const auto out) { return acc + out.amount; });

                        const uint64_t inputAmount = std::accumulate(
                            tx.inputs.begin(),
                            tx.inputs.end(),
                            0ull,
                            [](const auto acc, const auto in)
                            {
                                if (in.type() == typeid(CryptoNote::KeyInput))
                                {
                                    return acc + boost::get<CryptoNote::KeyInput>(in).amount;
                                }

                                return acc;
                            });

                        const uint64_t fee = inputAmount - outputAmount;

                        writer.Key("amountOut");
                        writer.Uint64(outputAmount);

                        writer.Key("fee");
                        writer.Uint64(fee);

                        writer.Key("hash");
                        const auto txHash = getObjectHash(tx);
                        txHash.toJSON(writer);

                        writer.Key("size");
                        writer.Uint64(getObjectBinarySize(tx));
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();
        }

        writer.Key("transactionsCumulativeSize");
        writer.Uint64(extraDetails.transactionsCumulativeSize);
    }
    writer.EndObject();
}

void RpcServer::generateTransactionPrefix(
    const CryptoNote::Transaction &transaction,
    rapidjson::Writer<rapidjson::StringBuffer> &writer)
{
    writer.StartObject();
    {
        writer.Key("extra");
        writer.String(Common::toHex(transaction.extra));

        writer.Key("inputs");
        writer.StartArray();
        {
            for (const auto &input : transaction.inputs)
            {
                const auto type = input.type() == typeid(CryptoNote::BaseInput) ? "ff" : "02";

                writer.StartObject();
                {
                    if (input.type() == typeid(CryptoNote::BaseInput))
                    {
                        writer.Key("height");
                        writer.Uint64(boost::get<CryptoNote::BaseInput>(input).blockIndex);
                    }
                    else
                    {
                        const auto keyInput = boost::get<CryptoNote::KeyInput>(input);

                        writer.Key("amount");
                        writer.Uint64(keyInput.amount);

                        writer.Key("keyImage");
                        keyInput.keyImage.toJSON(writer);

                        writer.Key("offsets");
                        writer.StartArray();
                        {
                            for (const auto index : keyInput.outputIndexes)
                            {
                                writer.Uint(index);
                            }
                        }
                        writer.EndArray();
                    }

                    writer.Key("type");
                    writer.String(type);
                }
                writer.EndObject();
            }
        }
        writer.EndArray();

        writer.Key("outputs");
        writer.StartArray();
        {
            for (const auto &output : transaction.outputs)
            {
                writer.StartObject();
                {
                    writer.Key("amount");
                    writer.Uint64(output.amount);

                    writer.Key("key");
                    const auto key = boost::get<CryptoNote::KeyOutput>(output.target).key;
                    key.toJSON(writer);

                    writer.Key("type");
                    writer.String("02");
                }
                writer.EndObject();
            }
        }
        writer.EndArray();

        writer.Key("unlockTime");
        writer.Uint64(transaction.unlockTime);

        writer.Key("version");
        writer.Uint64(transaction.version);
    }
    writer.EndObject();
}

void RpcServer::handleOptions(const httplib::Request &req, httplib::Response &res) const
{
    Logger::logger.log("Incoming " + req.method + " request: " + req.path, Logger::DEBUG, { Logger::DAEMON_RPC });

    std::string supported = "OPTIONS, GET, POST";

    if (m_corsHeader.empty())
    {
        supported = "";
    }

    if (req.has_header("Access-Control-Request-Method"))
    {
        res.set_header("Access-Control-Allow-Methods", supported);
    }
    else
    {
        res.set_header("Allow", supported);
    }

    if (!m_corsHeader.empty())
    {
        res.set_header("Access-Control-Allow-Origin", m_corsHeader);
        res.set_header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    }

    res.status = 200;
}

std::tuple<Error, uint16_t> RpcServer::getBlockHeaderByHashTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string hashStr = req.matches[1];
    Crypto::Hash hash{};

    if (!Common::podFromHex(hashStr, hash))
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    try
    {
        generateBlockHeader(hash, writer);

        res.body = sb.GetString();

        return {SUCCESS, 200};
    }
    catch (const std::exception &)
    {
        return {Error(API_HASH_NOT_FOUND), 404};
    }
}

std::tuple<Error, uint16_t> RpcServer::getBlockHeaderByHeightTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    uint64_t height = 0;
    const auto topHeight = m_core->getTopBlockIndex();

    try
    {
        const std::string heightStr = req.matches[1];
        height = std::stoull(heightStr);

        /* We cannot request a block height higher than the current top block */
        if (height > topHeight)
        {
            return {Error(API_INVALID_ARGUMENT, "Requested height cannot be greater than the top block height."), 400};
        }
    }
    catch (const std::out_of_range &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }
    catch (const std::invalid_argument &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    try
    {
        const auto hash = m_core->getBlockHashByIndex(height);

        generateBlockHeader(hash, writer);

        res.body = sb.GetString();

        return {SUCCESS, 200};
    }
    catch (const std::exception &)
    {
        return {Error(API_HASH_NOT_FOUND), 404};
    }
}

std::tuple<Error, uint16_t> RpcServer::getRawBlockByHashTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string hashStr = req.matches[1];
    Crypto::Hash hash{};

    if (!Common::podFromHex(hashStr, hash))
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    try
    {
        m_core->getRawBlock(hash).toJSON(writer);

        res.body = sb.GetString();

        return {SUCCESS, 200};
    }
    catch (const std::exception &)
    {
        return {Error(API_HASH_NOT_FOUND), 404};
    }
}

std::tuple<Error, uint16_t> RpcServer::getRawBlockByHeightTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string heightStr = req.matches[1];

    uint32_t height = 0;
    const auto topHeight = m_core->getTopBlockIndex();

    try
    {
        height = std::stoull(heightStr);

        if (height > topHeight)
        {
            return {Error(API_INVALID_ARGUMENT, "Requested height cannot be greater than the top block height."), 400};
        }
    }
    catch (const std::out_of_range &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }
    catch (const std::invalid_argument &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    try
    {
        m_core->getRawBlock(height).toJSON(writer);

        res.body = sb.GetString();

        return {SUCCESS, 200};
    }
    catch (const std::exception &)
    {
        return {Error(API_HASH_NOT_FOUND), 404};
    }
}

std::tuple<Error, uint16_t> RpcServer::getBlockCountTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.Uint64(m_core->getTopBlockIndex() + 1);

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlocksByHeightTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string heightStr = req.matches[1];
    uint64_t height;

    const auto topHeight = m_core->getTopBlockIndex();

    try
    {
        height = std::stoull(heightStr);

        /* We cannot request a block height higher than the current top block */
        if (height > topHeight)
        {
            return {Error(API_INVALID_ARGUMENT, "Requested height cannot be greater than the top block height."), 400};
        }
    }
    catch (const std::out_of_range &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }
    catch (const std::invalid_argument &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    const uint64_t MAX_BLOCKS_COUNT = 30;

    const uint64_t startHeight = height < MAX_BLOCKS_COUNT ? 0 : height - MAX_BLOCKS_COUNT;

    writer.StartArray();
    {
        /* Loop through the blocks in descending order and throw their resulting
         * headers into the array for the response */
        for (uint64_t i = height; i >= startHeight && i <= height; i--)
        {
            const auto hash = m_core->getBlockHashByIndex(i);

            generateBlockHeader(hash, writer);
        }
    }
    writer.EndArray();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getLastBlockHeaderTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    try
    {
        const auto height = m_core->getTopBlockIndex();
        const auto hash = m_core->getBlockHashByIndex(height);

        generateBlockHeader(hash, writer);

        res.body = sb.GetString();

        return {SUCCESS, 200};
    }
    catch (const std::exception &)
    {
        return {Error(API_INTERNAL_ERROR, "Could not retrieve last block header."), 500};
    }
}

std::tuple<Error, uint16_t>
    RpcServer::feeTrtlApi(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();
    {
        writer.Key("address");
        writer.String(m_feeAddress);

        writer.Key("amount");
        writer.Uint64(m_feeAmount);
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t>
    RpcServer::heightTrtlApi(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();
    {
        writer.Key("height");
        writer.Uint64(m_core->getTopBlockIndex() + 1);

        writer.Key("networkHeight");
        writer.Uint64(std::max(1u, m_syncManager->getBlockchainHeight()));
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getGlobalIndexesTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string startHeightStr = req.matches[1];
    const std::string endHeightStr = req.matches[2];

    uint64_t startHeight;
    uint64_t endHeight;

    try
    {
        startHeight = std::stoull(startHeightStr);
        endHeight = std::stoull(endHeightStr);

        // We allow startHeight and endHeight to be the same
        if (startHeight > endHeight)
        {
            return {Error(API_INVALID_ARGUMENT, "Start height cannot be greater than end height."), 400};
        }
    }
    catch (const std::out_of_range &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }
    catch (const std::invalid_argument &)
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    std::unordered_map<Crypto::Hash, std::vector<uint64_t>> indexes;

    // This is now inclusive
    const bool success = m_core->getGlobalIndexesForRange(startHeight, endHeight + 1, indexes);

    if (!success)
    {
        return {Error(API_INTERNAL_ERROR, "Cannot retrieve global indexes for range."), 500};
    }

    writer.StartArray();
    {
        for (const auto &[hash, globalIndexes] : indexes)
        {
            writer.StartObject();
            {
                writer.Key("hash");
                hash.toJSON(writer);

                writer.Key("indexes");
                writer.StartArray();
                {
                    for (const auto index : globalIndexes)
                    {
                        writer.Uint64(index);
                    }
                }
                writer.EndArray();
            }
            writer.EndObject();
        }
    }
    writer.EndArray();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::infoTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    const uint64_t height = m_core->getTopBlockIndex() + 1;
    const uint64_t networkHeight = std::max(1u, m_syncManager->getBlockchainHeight());
    const auto blockDetails = m_core->getBlockDetails(height - 1);
    const uint64_t difficulty = m_core->getDifficultyForNextBlock();

    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();
    {
        const uint64_t total_conn = m_p2p->get_connections_count();
        const uint64_t outgoing_connections_count = m_p2p->get_outgoing_connections_count();

        writer.Key("alternateBlockCount");
        writer.Uint64(m_core->getAlternativeBlockCount());

        writer.Key("difficulty");
        writer.Uint64(difficulty);

        writer.Key("explorer");
        writer.Bool(m_rpcMode >= RpcMode::BlockExplorerEnabled);

        writer.Key("greyPeerlistSize");
        writer.Uint64(m_p2p->getPeerlistManager().get_gray_peers_count());

        writer.Key("hashrate");
        writer.Uint64(difficulty / CryptoNote::parameters::getCurrentDifficultyTarget(networkHeight));

        writer.Key("height");
        writer.Uint64(height);

        writer.Key("incomingConnections");
        writer.Uint64(total_conn - outgoing_connections_count);

        writer.Key("lastBlockIndex");
        writer.Uint64(std::max(1u, m_syncManager->getObservedHeight()) - 1);

        writer.Key("majorVersion");
        writer.Uint64(blockDetails.majorVersion);

        writer.Key("minorVersion");
        writer.Uint64(blockDetails.minorVersion);

        writer.Key("networkHeight");
        writer.Uint64(networkHeight);

        writer.Key("outgoingConnections");
        writer.Uint64(outgoing_connections_count);

        writer.Key("startTime");
        writer.Uint64(m_core->getStartTime());

        writer.Key("supportedHeight");
        writer.Uint64(
            CryptoNote::parameters::FORK_HEIGHTS_SIZE == 0
                ? 0
                : CryptoNote::parameters::FORK_HEIGHTS[CryptoNote::parameters::CURRENT_FORK_INDEX]);

        writer.Key("synced");
        writer.Bool(height == networkHeight);

        writer.Key("transactionsPoolSize");
        writer.Uint64(m_core->getPoolTransactionCount());

        writer.Key("transactionsSize");
        /* Transaction count without coinbase transactions - one per block, so subtract height.
         * Use alreadyGeneratedTransactions from the top block rather than
         * getBlockchainTransactionCount() to avoid underflow on --sync-from-height nodes. */
        writer.Uint64(blockDetails.alreadyGeneratedTransactions - height);

        writer.Key("upgradeHeights");
        writer.StartArray();
        {
            for (const uint64_t height : CryptoNote::parameters::FORK_HEIGHTS)
            {
                writer.Uint64(height);
            }
        }
        writer.EndArray();

        writer.Key("version");
        writer.String(PROJECT_VERSION);

        writer.Key("whitePeerlistSize");
        writer.Uint64(m_p2p->getPeerlistManager().get_white_peers_count());
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::peersTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    std::list<PeerlistEntry> peers_white;
    std::list<PeerlistEntry> peers_gray;

    m_p2p->getPeerlistManager().get_peerlist_full(peers_gray, peers_white);

    writer.StartObject();
    {
        writer.Key("greyPeers");
        writer.StartArray();
        {
            for (const auto &peer : peers_gray)
            {
                std::stringstream stream;
                stream << peer.adr;
                writer.String(stream.str());
            }
        }
        writer.EndArray();

        writer.Key("peers");
        writer.StartArray();
        {
            for (const auto &peer : peers_white)
            {
                std::stringstream stream;
                stream << peer.adr;
                writer.String(stream.str());
            }
        }
        writer.EndArray();
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getTransactionDetailsByHashTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string hashStr = req.matches[1];

    Crypto::Hash hash {};

    if (!Common::podFromHex(hashStr, hash))
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    std::vector<Crypto::Hash> ignore;
    std::vector<std::vector<uint8_t>> rawTXs;
    std::vector hashes {hash};

    m_core->getTransactions(hashes, rawTXs, ignore);

    /* If we did not get exactly one transaction back then it's as if
     * we didn't get any transactions at all */
    if (rawTXs.size() != 1)
    {
        return {Error(API_HASH_NOT_FOUND), 404};
    }

    CryptoNote::Transaction transaction;
    CryptoNote::TransactionDetails txDetails = m_core->getTransactionDetails(hash);

    const uint64_t blockHeight = txDetails.blockIndex;
    const auto blockHash = m_core->getBlockHashByIndex(blockHeight);

    fromBinaryArray(transaction, rawTXs[0]);

    writer.StartObject();
    {
        /* This is a block header */
        writer.Key("block");
        generateBlockHeader(blockHash, writer, true);

        writer.Key("prefix");
        generateTransactionPrefix(transaction, writer);

        writer.Key("meta");
        writer.StartObject();
        {
            writer.Key("amountOut");
            writer.Uint64(txDetails.totalOutputsAmount);

            writer.Key("fee");
            writer.Uint64(txDetails.fee);

            writer.Key("paymentId");
            if (txDetails.paymentId == Constants::NULL_HASH)
            {
                writer.String("");
            }
            else
            {
                txDetails.paymentId.toJSON(writer);
            }

            writer.Key("publicKey");
            txDetails.extra.publicKey.toJSON(writer);

            writer.Key("ringSize");
            writer.Uint64(txDetails.mixin);

            writer.Key("size");
            writer.Uint64(txDetails.size);
        }
        writer.EndObject();
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getRawTransactionByHashTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const std::string hashStr = req.matches[1];
    Crypto::Hash hash {};

    if (!Common::podFromHex(hashStr, hash))
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    const auto transaction = m_core->getTransaction(hash);

    if (!transaction.has_value())
    {
        return {Error(API_HASH_NOT_FOUND), 404};
    }

    writer.String(Common::toHex(transaction.value()));

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getTransactionsInPoolTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartArray();
    {
        for (const auto &tx : m_core->getPoolTransactions())
        {
            writer.StartObject();

            const uint64_t outputAmount = std::accumulate(
                tx.outputs.begin(),
                tx.outputs.end(),
                0ull,
                [](const auto acc, const auto out) { return acc + out.amount; });

            const uint64_t inputAmount = std::accumulate(
                tx.inputs.begin(),
                tx.inputs.end(),
                0ull,
                [](const auto acc, const auto in)
                {
                    if (in.type() == typeid(CryptoNote::KeyInput))
                    {
                        return acc + boost::get<CryptoNote::KeyInput>(in).amount;
                    }

                    return acc;
                });

            const uint64_t fee = inputAmount - outputAmount;

            writer.Key("amountOut");
            writer.Uint64(outputAmount);

            writer.Key("fee");
            writer.Uint64(fee);

            writer.Key("hash");
            const auto txHash = getObjectHash(tx);
            txHash.toJSON(writer);

            writer.Key("size");
            writer.Uint64(getObjectBinarySize(tx));

            writer.EndObject();
        }
    }
    writer.EndArray();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getRawTransactionsInPoolTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartArray();
    {
        for (const auto &tx : m_core->getPoolTransactions())
        {
            const auto transaction = toBinaryArray(tx);

            writer.String(Common::toHex(transaction));
        }
    }
    writer.EndArray();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::info(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body)
{
    const uint64_t height = m_core->getTopBlockIndex() + 1;
    const uint64_t networkHeight = std::max(1u, m_syncManager->getBlockchainHeight());
    const auto blockDetails = m_core->getBlockDetails(height - 1);
    const uint64_t difficulty = m_core->getDifficultyForNextBlock();

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("height");
    writer.Uint64(height);

    writer.Key("difficulty");
    writer.Uint64(difficulty);

    writer.Key("tx_count");
    /* Transaction count without coinbase transactions - one per block, so subtract height.
     * Use alreadyGeneratedTransactions from the top block's CachedBlockInfo rather than
     * getBlockchainTransactionCount(), which only counts transactions indexed since the
     * node's anchor height and underflows for --sync-from-height nodes. */
    writer.Uint64(blockDetails.alreadyGeneratedTransactions - height);

    writer.Key("tx_pool_size");
    writer.Uint64(m_core->getPoolTransactionCount());

    writer.Key("alt_blocks_count");
    writer.Uint64(m_core->getAlternativeBlockCount());

    uint64_t total_conn = m_p2p->get_connections_count();
    uint64_t outgoing_connections_count = m_p2p->get_outgoing_connections_count();

    writer.Key("outgoing_connections_count");
    writer.Uint64(outgoing_connections_count);

    writer.Key("incoming_connections_count");
    writer.Uint64(total_conn - outgoing_connections_count);

    writer.Key("white_peerlist_size");
    writer.Uint64(m_p2p->getPeerlistManager().get_white_peers_count());

    writer.Key("grey_peerlist_size");
    writer.Uint64(m_p2p->getPeerlistManager().get_gray_peers_count());

    writer.Key("last_known_block_index");
    writer.Uint64(std::max(1u, m_syncManager->getObservedHeight()) - 1);

    writer.Key("network_height");
    writer.Uint64(networkHeight);

    writer.Key("upgrade_heights");
    writer.StartArray();
    {
        for (const uint64_t height : CryptoNote::parameters::FORK_HEIGHTS)
        {
            writer.Uint64(height);
        }
    }
    writer.EndArray();

    writer.Key("supported_height");
    writer.Uint64(CryptoNote::parameters::FORK_HEIGHTS_SIZE == 0
        ? 0
        : CryptoNote::parameters::FORK_HEIGHTS[CryptoNote::parameters::CURRENT_FORK_INDEX]);

    writer.Key("hashrate");
    writer.Uint64(round(difficulty / CryptoNote::parameters::getCurrentDifficultyTarget(networkHeight)));

    writer.Key("synced");
    writer.Bool(height == networkHeight);

    writer.Key("major_version");
    writer.Uint64(blockDetails.majorVersion);

    writer.Key("minor_version");
    writer.Uint64(blockDetails.minorVersion);

    writer.Key("version");
    writer.String(PROJECT_VERSION);

    writer.Key("status");
    writer.String("OK");

    writer.Key("start_time");
    writer.Uint64(m_core->getStartTime());

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::fee(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("address");
    writer.String(m_feeAddress);

    writer.Key("amount");
    writer.Uint64(m_feeAmount);

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::height(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("height");
    writer.Uint64(m_core->getTopBlockIndex() + 1);

    writer.Key("network_height");
    writer.Uint64(std::max(1u, m_syncManager->getBlockchainHeight()));

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::peers(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    std::list<PeerlistEntry> peers_white;
    std::list<PeerlistEntry> peers_gray;

    m_p2p->getPeerlistManager().get_peerlist_full(peers_gray, peers_white);

    writer.Key("peers");
    writer.StartArray();
    {
        for (const auto &peer : peers_white)
        {
            std::stringstream stream;
            stream << peer.adr;
            writer.String(stream.str());
        }
    }
    writer.EndArray();

    writer.Key("peers_gray");
    writer.StartArray();
    {
        for (const auto &peer : peers_gray)
        {
            std::stringstream stream;
            stream << peer.adr;
            writer.String(stream.str());
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t>
    RpcServer::submitBlockTrtlApi(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const auto blockBlob = getStringFromJSON(body);
    std::vector<uint8_t> rawBlob;

    if (!Common::fromHex(blockBlob, rawBlob))
    {
        return {Error(API_INVALID_ARGUMENT, "Submitted block blob is not hex!"), 400};
    }

    const auto submitResult = m_core->submitBlock(rawBlob);

    if (submitResult != CryptoNote::error::AddBlockErrorCondition::BLOCK_ADDED)
    {
        return {Error(API_BLOCK_NOT_ACCEPTED), 409};
    }

    if (submitResult == CryptoNote::error::AddBlockErrorCode::ADDED_TO_MAIN
        || submitResult == CryptoNote::error::AddBlockErrorCode::ADDED_TO_ALTERNATIVE_AND_SWITCHED)
    {
        CryptoNote::NOTIFY_NEW_BLOCK::request newBlockMessage;
        CryptoNote::BlockTemplate blockTemplate;
        CryptoNote::fromBinaryArray(blockTemplate, rawBlob);

        newBlockMessage.block = CryptoNote::RawBlockLegacy(rawBlob, blockTemplate, m_core);
        newBlockMessage.hop = 0;
        newBlockMessage.current_blockchain_height = m_core->getTopBlockIndex() + 1;

        m_syncManager->relayBlock(newBlockMessage);

        const CryptoNote::CachedBlock block(blockTemplate);
        const auto hash = block.getBlockHash();

        hash.toJSON(writer);

        res.body = sb.GetString();
    }

    return {SUCCESS, 202};
}

std::tuple<Error, uint16_t> RpcServer::getBlockTemplateTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const uint64_t reserveSize = getUint64FromJSON(body, "reserveSize");

    if (reserveSize > 255)
    {
        return {Error(API_INVALID_ARGUMENT, "Reserved size is too large, maximum permitted is 255."), 400};
    }

    const std::string address = getStringFromJSON(body, "address");

    Error addressError = validateAddresses({address}, false);

    if (addressError)
    {
        return {Error(API_INVALID_ARGUMENT, addressError.getErrorMessage()), 400};
    }

    const auto [publicSpendKey, publicViewKey] = Utilities::addressToKeys(address);

    CryptoNote::BlockTemplate blockTemplate;
    std::vector<uint8_t> blobReserve;
    blobReserve.resize(reserveSize, 0);

    uint64_t difficulty;
    uint32_t height;

    const auto [success, error] =
        m_core->getBlockTemplate(blockTemplate, publicViewKey, publicSpendKey, blobReserve, difficulty, height);

    if (!success)
    {
        return {Error(API_INTERNAL_ERROR, "Failed to create block template: " + error), 500};
    }

    std::vector<uint8_t> blockBlob = CryptoNote::toBinaryArray(blockTemplate);

    const auto transactionPrivateKey = Utilities::getTransactionPublicKeyFromExtra(blockTemplate.baseTransaction.extra);

    uint64_t reservedOffset = 0;

    if (reserveSize > 0)
    {
        /* Find where in the block blob the transaction private key is */
        const auto it = std::search(
            blockBlob.begin(),
            blockBlob.end(),
            std::begin(transactionPrivateKey.data),
            std::end(transactionPrivateKey.data));

        /* The reserved offset is past the transactionPublicKey, then past
         * the extra nonce tags */
        reservedOffset = (it - blockBlob.begin()) + sizeof(transactionPrivateKey) + 2;

        if (reservedOffset + reserveSize > blockBlob.size())
        {
            return {
                Error(
                    API_INTERNAL_ERROR,
                    "Internal error: failed to create block template, not enough space for reserved bytes"),
                500};
        }
    }

    writer.StartObject();
    {
        writer.Key("difficulty");
        writer.Uint64(difficulty);

        writer.Key("height");
        writer.Uint(height);

        writer.Key("reservedOffset");
        writer.Uint64(reservedOffset);

        writer.Key("blob");
        writer.String(Common::toHex(blockBlob));
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 201};
}

std::tuple<Error, uint16_t> RpcServer::getRandomOutsTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    const uint64_t numOutputs = getUint64FromJSON(body, "count");

    writer.StartArray();
    {
        for (const auto &jsonAmount : getArrayFromJSON(body, "amounts"))
        {
            writer.StartObject();

            const uint64_t amount = jsonAmount.GetUint64();

            std::vector<uint32_t> globalIndexes;

            std::vector<Crypto::PublicKey> publicKeys;

            const auto [success, error] =
                m_core->getRandomOutputs(amount, static_cast<uint16_t>(numOutputs), globalIndexes, publicKeys);

            if (!success)
            {
                return {Error(CANT_GET_FAKE_OUTPUTS, error), 500};
            }

            if (globalIndexes.size() != numOutputs)
            {
                std::stringstream stream;

                stream
                    << "Failed to get enough matching outputs for amount " << amount << " ("
                    << Utilities::formatAmount(amount) << "). Requested outputs: " << numOutputs
                    << ", found outputs: " << globalIndexes.size()
                    << ". Further explanation here: https://gist.github.com/zpalmtree/80b3e80463225bcfb8f8432043cb594c"
                    << std::endl
                    << "Note: If you are a public node operator, you can safely ignore this message. "
                    << "It is only relevant to the user sending the transaction.";

                return {Error(CANT_GET_FAKE_OUTPUTS, stream.str()), 416};
            }

            writer.Key("amount");
            writer.Uint64(amount);

            writer.Key("outputs");
            writer.StartArray();
            {
                for (size_t i = 0; i < globalIndexes.size(); i++)
                {
                    writer.StartObject();
                    {
                        writer.Key("index");
                        writer.Uint64(globalIndexes[i]);

                        writer.Key("key");
                        publicKeys[i].toJSON(writer);
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.EndObject();
        }
    }
    writer.EndArray();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getWalletSyncDataTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    std::vector<Crypto::Hash> blockHashCheckpoints;

    if (hasMember(body, "checkpoints"))
    {
        for (const auto &jsonHash : getArrayFromJSON(body, "checkpoints"))
        {
            std::string hashStr = jsonHash.GetString();
            Crypto::Hash hash {};

            Common::podFromHex(hashStr, hash);

            blockHashCheckpoints.push_back(hash);
        }
    }

    const uint64_t startHeight = hasMember(body, "height") ? getUint64FromJSON(body, "height") : 0;
    const uint64_t startTimestamp = hasMember(body, "timestamp") ? getUint64FromJSON(body, "timestamp") : 0;
    const uint64_t blockCount = hasMember(body, "count") ? getUint64FromJSON(body, "count") : 100;
    const bool skipCoinbaseTransactions =
        hasMember(body, "skipCoinbaseTransactions") ? getBoolFromJSON(body, "skipCoinbaseTransactions") : false;

    std::vector<WalletTypes::WalletBlockInfo> walletBlocks;
    std::optional<WalletTypes::TopBlock> topBlockInfo;

    const bool success = m_core->getWalletSyncData(
        blockHashCheckpoints,
        startHeight,
        startTimestamp,
        blockCount,
        skipCoinbaseTransactions,
        walletBlocks,
        topBlockInfo);

    if (!success)
    {
        return {Error(API_INTERNAL_ERROR), 500};
    }

    writer.StartObject();
    {
        writer.Key("blocks");
        writer.StartArray();
        {
            for (const auto &block : walletBlocks)
            {
                writer.StartObject();

                writer.Key("hash");
                block.blockHash.toJSON(writer);

                writer.Key("height");
                writer.Uint64(block.blockHeight);

                writer.Key("timestamp");
                writer.Uint64(block.blockTimestamp);

                if (block.coinbaseTransaction)
                {
                    writer.Key("coinbaseTX");
                    writer.StartObject();
                    {
                        writer.Key("hash");
                        block.coinbaseTransaction->hash.toJSON(writer);

                        writer.Key("outputs");
                        writer.StartArray();
                        {
                            for (const auto &output : block.coinbaseTransaction->keyOutputs)
                            {
                                writer.StartObject();
                                {
                                    writer.Key("amount");
                                    writer.Uint64(output.amount);

                                    writer.Key("key");
                                    output.key.toJSON(writer);
                                }
                                writer.EndObject();
                            }
                        }
                        writer.EndArray();

                        writer.Key("publicKey");
                        block.coinbaseTransaction->transactionPublicKey.toJSON(writer);

                        writer.Key("unlockTime");
                        writer.Uint64(block.coinbaseTransaction->unlockTime);
                    }
                    writer.EndObject();
                }

                writer.Key("transactions");
                writer.StartArray();
                {
                    for (const auto &transaction : block.transactions)
                    {
                        writer.StartObject();
                        {
                            writer.Key("hash");
                            transaction.hash.toJSON(writer);

                            writer.Key("inputs");
                            writer.StartArray();
                            {
                                for (const auto &input : transaction.keyInputs)
                                {
                                    writer.StartObject();
                                    {
                                        writer.Key("amount");
                                        writer.Uint64(input.amount);

                                        writer.Key("keyImage");
                                        input.keyImage.toJSON(writer);
                                    }
                                    writer.EndObject();
                                }
                            }
                            writer.EndArray();

                            writer.Key("outputs");
                            writer.StartArray();
                            {
                                for (const auto &output : transaction.keyOutputs)
                                {
                                    writer.StartObject();
                                    {
                                        writer.Key("amount");
                                        writer.Uint64(output.amount);

                                        writer.Key("key");
                                        output.key.toJSON(writer);
                                    }
                                    writer.EndObject();
                                }
                            }
                            writer.EndArray();

                            writer.Key("paymentID");
                            writer.String(transaction.paymentID);

                            writer.Key("publicKey");
                            transaction.transactionPublicKey.toJSON(writer);

                            writer.Key("unlockTime");
                            writer.Uint64(transaction.unlockTime);
                        }
                        writer.EndObject();
                    }
                }
                writer.EndArray();

                writer.EndObject();
            }
        }
        writer.EndArray();

        writer.Key("synced");
        writer.Bool(walletBlocks.empty());

        if (topBlockInfo)
        {
            writer.Key("topBlock");
            writer.StartObject();
            {
                writer.Key("hash");
                topBlockInfo->hash.toJSON(writer);

                writer.Key("height");
                writer.Uint64(topBlockInfo->height);
            }
            writer.EndObject();
        }
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t>
    RpcServer::getRawBlocksTrtlApi(const httplib::Request &req, httplib::Response &res, const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    writer.StartObject();
    {
        std::vector<Crypto::Hash> blockHashCheckpoints;

        if (hasMember(body, "checkpoints"))
        {
            for (const auto &jsonHash : getArrayFromJSON(body, "checkpoints"))
            {
                std::string hashStr = jsonHash.GetString();

                Crypto::Hash hash;
                Common::podFromHex(hashStr, hash);

                blockHashCheckpoints.push_back(hash);
            }
        }

        const uint64_t startHeight = hasMember(body, "height") ? getUint64FromJSON(body, "height") : 0;
        const uint64_t startTimestamp = hasMember(body, "timestamp") ? getUint64FromJSON(body, "timestamp") : 0;
        const uint64_t blockCount = hasMember(body, "count") ? getUint64FromJSON(body, "count")
                                                             : CryptoNote::BLOCKS_SYNCHRONIZING_DEFAULT_COUNT;

        const bool skipCoinbaseTransactions =
            hasMember(body, "skipCoinbaseTransactions") ? getBoolFromJSON(body, "skipCoinbaseTransactions") : false;

        std::vector<CryptoNote::RawBlock> rawBlocks;
        std::optional<WalletTypes::TopBlock> topBlockInfo;

        const bool success = m_core->getRawBlocks(
            blockHashCheckpoints,
            startHeight,
            startTimestamp,
            blockCount,
            skipCoinbaseTransactions,
            rawBlocks,
            topBlockInfo);

        if (!success)
        {
            return {Error(API_INTERNAL_ERROR, "Failed to retrieve raw blocks from underlying storage."), 500};
        }

        writer.Key("blocks");
        writer.StartArray();
        {
            for (const auto &rawBlock : rawBlocks)
            {
                rawBlock.toJSON(writer);
            }
        }
        writer.EndArray();

        writer.Key("synced");
        writer.Bool(rawBlocks.empty());

        if (topBlockInfo)
        {
            writer.Key("topBlock");
            writer.StartObject();
            {
                writer.Key("hash");
                topBlockInfo->hash.toJSON(writer);

                writer.Key("height");
                writer.Uint64(topBlockInfo->height);
            }
            writer.EndObject();
        }
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::sendTransactionTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    std::vector<uint8_t> transaction;
    const std::string rawData = getStringFromJSON(body);

    if (!Common::fromHex(rawData, transaction))
    {
        return {Error(API_INVALID_ARGUMENT, "Failed to parse transaction from hex buffer"), 400};
    }

    const CryptoNote::CachedTransaction cachedTransaction(transaction);
    const auto hash = cachedTransaction.getTransactionHash();

    std::stringstream stream;

    stream << "Attempting to add transaction " << hash << " from /transaction to pool";

    Logger::logger.log(stream.str(), Logger::DEBUG, {Logger::DAEMON_RPC});

    const auto [success, error] = m_core->addTransactionToPool(transaction);

    if (!success)
    {
        return {Error(API_TRANSACTION_POOL_INSERT_FAILED, error), 409};
    }

    m_syncManager->relayTransactions({transaction});

    hash.toJSON(writer);

    res.body = sb.GetString();

    return {SUCCESS, 202};
}

std::tuple<Error, uint16_t> RpcServer::getPoolChangesTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    Crypto::Hash lastBlockHash;

    if (!Common::podFromHex(getStringFromJSON(body, "lastKnownBlock"), lastBlockHash))
    {
        return {Error(API_INVALID_ARGUMENT), 400};
    }

    std::vector<Crypto::Hash> knownHashes;

    for (const auto &hashStr : getArrayFromJSON(body, "transactions"))
    {
        Crypto::Hash hash;

        if (!Common::podFromHex(getStringFromJSON(hashStr), hash))
        {
            return {Error(API_INVALID_ARGUMENT), 400};
        }

        knownHashes.push_back(hash);
    }

    std::vector<CryptoNote::TransactionPrefixInfo> addedTransactions;
    std::vector<Crypto::Hash> deletedTransactions;

    const bool atTopOfChain =
        m_core->getPoolChangesLite(lastBlockHash, knownHashes, addedTransactions, deletedTransactions);

    writer.StartObject();
    {
        writer.Key("added");
        writer.StartArray();
        {
            for (const auto &transaction : addedTransactions)
            {
                const auto tx = CryptoNote::toBinaryArray(transaction);

                writer.String(Common::toHex(tx));
            }
        }
        writer.EndArray();

        writer.Key("deleted");
        writer.StartArray();
        {
            for (const auto hash : deletedTransactions)
            {
                hash.toJSON(writer);
            }
        }
        writer.EndArray();

        writer.Key("synced");
        writer.Bool(atTopOfChain);
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getTransactionsStatusTrtlApi(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer writer(sb);

    std::unordered_set<Crypto::Hash> transactionHashes;

    for (const auto &hashStr : getArrayFromJSON(body))
    {
        Crypto::Hash hash;

        if (!Common::podFromHex(getStringFromJSON(hashStr), hash))
        {
            return {Error(API_INVALID_ARGUMENT), 400};
        }

        transactionHashes.insert(hash);
    }

    std::unordered_set<Crypto::Hash> transactionsInPool;
    std::unordered_set<Crypto::Hash> transactionsInBlock;
    std::unordered_set<Crypto::Hash> transactionsUnknown;

    const bool success =
        m_core->getTransactionsStatus(transactionHashes, transactionsInPool, transactionsInBlock, transactionsUnknown);

    if (!success)
    {
        return {Error(API_INTERNAL_ERROR, "Could not retrieve transactions status."), 500};
    }

    writer.StartObject();
    {
        writer.Key("inBlock");
        writer.StartArray();
        {
            for (const auto &hash : transactionsInBlock)
            {
                hash.toJSON(writer);
            }
        }
        writer.EndArray();

        writer.Key("inPool");
        writer.StartArray();
        {
            for (const auto &hash : transactionsInPool)
            {
                hash.toJSON(writer);
            }
        }
        writer.EndArray();

        writer.Key("notFound");
        writer.StartArray();
        {
            for (const auto &hash : transactionsUnknown)
            {
                hash.toJSON(writer);
            }
        }
        writer.EndArray();
    }
    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::sendTransaction(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    std::vector<uint8_t> transaction;

    const std::string rawData = getStringFromJSON(body, "tx_as_hex");

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    if (!Common::fromHex(rawData, transaction))
    {
        writer.Key("status");
        writer.String("Failed");

        writer.Key("error");
        writer.String("Failed to parse transaction from hex buffer");
    }
    else
    {
        Crypto::Hash transactionHash = Crypto::cn_fast_hash(transaction.data(), transaction.size());

        writer.Key("transactionHash");
        writer.String(Common::podToHex(transactionHash));

        std::stringstream stream;

        stream << "Attempting to add transaction " << transactionHash << " from /sendrawtransaction to pool";

        Logger::logger.log(
            stream.str(),
            Logger::DEBUG,
            { Logger::DAEMON_RPC }
        );

        const auto [success, error] = m_core->addTransactionToPool(transaction);

        if (!success)
        {
            /* Empty stream */
            std::stringstream().swap(stream);

            stream << "Failed to add transaction " << transactionHash << " from /sendrawtransaction to pool: " << error;

            Logger::logger.log(
                stream.str(),
                Logger::INFO,
                { Logger::DAEMON_RPC }
            );

            writer.Key("status");
            writer.String("Failed");

            writer.Key("error");
            writer.String(error);
        }
        else
        {
            m_syncManager->relayTransactions({transaction});

            writer.Key("status");
            writer.String("OK");

            writer.Key("error");
            writer.String("");

        }
    }

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getRandomOuts(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    const uint64_t numOutputs = getUint64FromJSON(body, "outs_count");

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("outs");

    writer.StartArray();
    {
        for (const auto &jsonAmount : getArrayFromJSON(body, "amounts"))
        {
            writer.StartObject();

            const uint64_t amount = jsonAmount.GetUint64();

            std::vector<uint32_t> globalIndexes;
            std::vector<Crypto::PublicKey> publicKeys;

            const auto [success, error] = m_core->getRandomOutputs(
                amount, static_cast<uint16_t>(numOutputs), globalIndexes, publicKeys
            );

            if (!success)
            {
                return {Error(CANT_GET_FAKE_OUTPUTS, error), 200};
            }

            if (globalIndexes.size() != numOutputs)
            {
                std::stringstream stream;

                stream << "Failed to get enough matching outputs for amount " << amount << " ("
                       << Utilities::formatAmount(amount) << "). Requested outputs: " << numOutputs
                       << ", found outputs: " << globalIndexes.size()
                       << ". Further explanation here: https://gist.github.com/zpalmtree/80b3e80463225bcfb8f8432043cb594c"
                       << std::endl
                       << "Note: If you are a public node operator, you can safely ignore this message. "
                       << "It is only relevant to the user sending the transaction.";

                return {Error(CANT_GET_FAKE_OUTPUTS, stream.str()), 200};
            }

            writer.Key("amount");
            writer.Uint64(amount);

            writer.Key("outs");
            writer.StartArray();
            {
                for (size_t i = 0; i < globalIndexes.size(); i++)
                {
                    writer.StartObject();
                    {
                        writer.Key("global_amount_index");
                        writer.Uint64(globalIndexes[i]);

                        writer.Key("out_key");
                        writer.String(Common::podToHex(publicKeys[i]));
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.EndObject();
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getWalletSyncData(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    std::vector<Crypto::Hash> blockHashCheckpoints;

    if (hasMember(body, "blockHashCheckpoints"))
    {
        for (const auto &jsonHash : getArrayFromJSON(body, "blockHashCheckpoints"))
        {
            std::string hashStr = jsonHash.GetString();

            Crypto::Hash hash;
            Common::podFromHex(hashStr, hash);

            blockHashCheckpoints.push_back(hash);
        }
    }

    const uint64_t startHeight = hasMember(body, "startHeight")
        ? getUint64FromJSON(body, "startHeight")
        : 0;

    const uint64_t startTimestamp = hasMember(body, "startTimestamp")
        ? getUint64FromJSON(body, "startTimestamp")
        : 0;

    const uint64_t blockCount = hasMember(body, "blockCount")
        ? getUint64FromJSON(body, "blockCount")
        : 100;

    const bool skipCoinbaseTransactions = hasMember(body, "skipCoinbaseTransactions")
        ? getBoolFromJSON(body, "skipCoinbaseTransactions")
        : false;

    std::vector<WalletTypes::WalletBlockInfo> walletBlocks;
    std::optional<WalletTypes::TopBlock> topBlockInfo;

    const bool success = m_core->getWalletSyncData(
        blockHashCheckpoints,
        startHeight,
        startTimestamp,
        blockCount,
        skipCoinbaseTransactions,
        walletBlocks,
        topBlockInfo
    );

    if (!success)
    {
        return {SUCCESS, 500};
    }

    writer.Key("items");
    writer.StartArray();
    {
        for (const auto &block : walletBlocks)
        {
            writer.StartObject();

            if (block.coinbaseTransaction)
            {
                writer.Key("coinbaseTX");
                writer.StartObject();
                {
                    writer.Key("outputs");
                    writer.StartArray();
                    {
                        for (const auto &output : block.coinbaseTransaction->keyOutputs)
                        {
                            writer.StartObject();
                            {
                                writer.Key("key");
                                writer.String(Common::podToHex(output.key));

                                writer.Key("amount");
                                writer.Uint64(output.amount);
                            }
                            writer.EndObject();
                        }
                    }
                    writer.EndArray();

                    writer.Key("hash");
                    writer.String(Common::podToHex(block.coinbaseTransaction->hash));

                    writer.Key("txPublicKey");
                    writer.String(Common::podToHex(block.coinbaseTransaction->transactionPublicKey));

                    writer.Key("unlockTime");
                    writer.Uint64(block.coinbaseTransaction->unlockTime);
                }
                writer.EndObject();
            }

            writer.Key("transactions");
            writer.StartArray();
            {
                for (const auto &transaction : block.transactions)
                {
                    writer.StartObject();
                    {
                        writer.Key("outputs");
                        writer.StartArray();
                        {
                            for (const auto &output : transaction.keyOutputs)
                            {
                                writer.StartObject();
                                {
                                    writer.Key("key");
                                    writer.String(Common::podToHex(output.key));

                                    writer.Key("amount");
                                    writer.Uint64(output.amount);
                                }
                                writer.EndObject();
                            }
                        }
                        writer.EndArray();

                        writer.Key("hash");
                        writer.String(Common::podToHex(transaction.hash));

                        writer.Key("txPublicKey");
                        writer.String(Common::podToHex(transaction.transactionPublicKey));

                        writer.Key("unlockTime");
                        writer.Uint64(transaction.unlockTime);

                        writer.Key("paymentID");
                        writer.String(transaction.paymentID);

                        writer.Key("inputs");
                        writer.StartArray();
                        {
                            for (const auto &input : transaction.keyInputs)
                            {
                                writer.StartObject();
                                {
                                    writer.Key("amount");
                                    writer.Uint64(input.amount);

                                    writer.Key("key_offsets");
                                    writer.StartArray();
                                    {
                                        for (const auto &offset : input.outputIndexes)
                                        {
                                            writer.Uint64(offset);
                                        }
                                    }
                                    writer.EndArray();

                                    writer.Key("k_image");
                                    writer.String(Common::podToHex(input.keyImage));
                                }
                                writer.EndObject();
                            }
                        }
                        writer.EndArray();
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.Key("blockHeight");
            writer.Uint64(block.blockHeight);

            writer.Key("blockHash");
            writer.String(Common::podToHex(block.blockHash));

            writer.Key("blockTimestamp");
            writer.Uint64(block.blockTimestamp);

            writer.EndObject();
        }
    }
    writer.EndArray();

    if (topBlockInfo)
    {
        writer.Key("topBlock");
        writer.StartObject();
        {
            writer.Key("hash");
            writer.String(Common::podToHex(topBlockInfo->hash));

            writer.Key("height");
            writer.Uint64(topBlockInfo->height);
        }
        writer.EndObject();
    }

    writer.Key("synced");
    writer.Bool(walletBlocks.empty());

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getGlobalIndexes(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const uint64_t startHeight = getUint64FromJSON(body, "startHeight");
    const uint64_t endHeight = getUint64FromJSON(body, "endHeight");

    std::unordered_map<Crypto::Hash, std::vector<uint64_t>> indexes;

    const bool success = m_core->getGlobalIndexesForRange(startHeight, endHeight, indexes);

    writer.StartObject();

    if (!success)
    {
        writer.Key("status");
        writer.String("Failed");

        res.body = sb.GetString();

        return {SUCCESS, 500};
    }

    writer.Key("indexes");

    writer.StartArray();
    {
        for (const auto &[hash, globalIndexes] : indexes)
        {
            writer.StartObject();

            writer.Key("key");
            writer.String(Common::podToHex(hash));

            writer.Key("value");
            writer.StartArray();
            {
                for (const auto index : globalIndexes)
                {
                    writer.Uint64(index);
                }
            }
            writer.EndArray();

            writer.EndObject();
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlockTemplateJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getObjectFromJSON(body, "params");

    writer.StartObject();

    const uint64_t reserveSize = getUint64FromJSON(params, "reserve_size");

    if (reserveSize > 255)
    {
        failJsonRpcRequest(
            -3,
            "Too big reserved size, maximum allowed is 255",
            res
        );

        return {SUCCESS, 200};
    }

    const std::string address = getStringFromJSON(params, "wallet_address");

    Error addressError = validateAddresses({address}, false);

    if (addressError)
    {
        failJsonRpcRequest(
            -4,
            addressError.getErrorMessage(),
            res
        );

        return {SUCCESS, 200};
    }

    const auto [publicSpendKey, publicViewKey] = Utilities::addressToKeys(address);

    CryptoNote::BlockTemplate blockTemplate;

    std::vector<uint8_t> blobReserve;
    blobReserve.resize(reserveSize, 0);

    uint64_t difficulty;
    uint32_t height;

    const auto [success, error] = m_core->getBlockTemplate(
        blockTemplate, publicViewKey, publicSpendKey, blobReserve, difficulty, height
    );

    if (!success)
    {
        failJsonRpcRequest(
            -5,
            "Failed to create block template: " + error,
            res
        );

        return {SUCCESS, 200};
    }

    std::vector<uint8_t> blockBlob = CryptoNote::toBinaryArray(blockTemplate);

    const auto transactionPublicKey = Utilities::getTransactionPublicKeyFromExtra(
        blockTemplate.baseTransaction.extra
    );

    uint64_t reservedOffset = 0;

    if (reserveSize > 0)
    {
        /* Find where in the block blob the transaction public key is */
        const auto it = std::search(
            blockBlob.begin(),
            blockBlob.end(),
            std::begin(transactionPublicKey.data),
            std::end(transactionPublicKey.data)
        );

        /* The reserved offset is past the transactionPublicKey, then past
         * the extra nonce tags */
        reservedOffset = (it - blockBlob.begin()) + sizeof(transactionPublicKey) + 2;

        if (reservedOffset + reserveSize > blockBlob.size())
        {
            failJsonRpcRequest(
                -5,
                "Internal error: failed to create block template, not enough space for reserved bytes",
                res
            );

            return {SUCCESS, 200};
        }
    }

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("height");
        writer.Uint(height);

        writer.Key("difficulty");
        writer.Uint64(difficulty);

        writer.Key("reserved_offset");
        writer.Uint64(reservedOffset);

        writer.Key("blocktemplate_blob");
        writer.String(Common::toHex(blockBlob));

        writer.Key("status");
        writer.String("OK");
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::submitBlockJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getArrayFromJSON(body, "params");

    if (params.Size() != 1)
    {
        failJsonRpcRequest(
            -1,
            "You must submit one and only one block blob! (Found " + std::to_string(params.Size()) + ")",
            res
        );

        return {SUCCESS, 200};
    }

    const std::string blockBlob = getStringFromJSONString(params[0]);

    std::vector<uint8_t> rawBlob;

    if (!Common::fromHex(blockBlob, rawBlob))
    {
        failJsonRpcRequest(
            -6,
            "Submitted block blob is not hex!",
            res
        );

        return {SUCCESS, 200};
    }

    const auto submitResult = m_core->submitBlock(rawBlob);

    if (submitResult != CryptoNote::error::AddBlockErrorCondition::BLOCK_ADDED)
    {
        failJsonRpcRequest(
            -7,
            "Block not accepted",
            res
        );

        return {SUCCESS, 200};
    }

    if (submitResult == CryptoNote::error::AddBlockErrorCode::ADDED_TO_MAIN
        || submitResult == CryptoNote::error::AddBlockErrorCode::ADDED_TO_ALTERNATIVE_AND_SWITCHED)
    {
        CryptoNote::NOTIFY_NEW_BLOCK::request newBlockMessage;

        CryptoNote::BlockTemplate blockTemplate;
        CryptoNote::fromBinaryArray(blockTemplate, rawBlob);
        newBlockMessage.block = CryptoNote::RawBlockLegacy(rawBlob, blockTemplate, m_core);
        newBlockMessage.hop = 0;
        newBlockMessage.current_blockchain_height = m_core->getTopBlockIndex() + 1;

        m_syncManager->relayBlock(newBlockMessage);
    }

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlockCountJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("count");
        writer.Uint64(m_core->getTopBlockIndex() + 1);
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getLastBlockHeaderJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto height = m_core->getTopBlockIndex();
    const auto hash = m_core->getBlockHashByIndex(height);
    const auto topBlock = m_core->getBlockByHash(hash);
    const auto outputs = topBlock.baseTransaction.outputs;
    const auto extraDetails = m_core->getBlockDetails(hash);

    const uint64_t reward = std::accumulate(outputs.begin(), outputs.end(), 0ull,
        [](const auto acc, const auto out) {
            return acc + out.amount;
        }
    );

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("block_header");
        writer.StartObject();
        {
            writer.Key("major_version");
            writer.Uint64(topBlock.majorVersion);

            writer.Key("minor_version");
            writer.Uint64(topBlock.minorVersion);

            writer.Key("timestamp");
            writer.Uint64(topBlock.timestamp);

            writer.Key("prev_hash");
            writer.String(Common::podToHex(topBlock.previousBlockHash));

            writer.Key("nonce");
            writer.Uint64(topBlock.nonce);

            writer.Key("orphan_status");
            writer.Bool(extraDetails.isAlternative);

            writer.Key("height");
            writer.Uint64(height);

            writer.Key("depth");
            writer.Uint64(0);

            writer.Key("hash");
            writer.String(Common::podToHex(hash));

            writer.Key("difficulty");
            writer.Uint64(m_core->getBlockDifficulty(height));

            writer.Key("reward");
            writer.Uint64(reward);

            writer.Key("num_txes");
            writer.Uint64(extraDetails.transactions.size());

            writer.Key("block_size");
            writer.Uint64(extraDetails.blockSize);
        }
        writer.EndObject();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlockHeaderByHashJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getObjectFromJSON(body, "params");
    const auto hashStr = getStringFromJSON(params, "hash");
    const auto topHeight = m_core->getTopBlockIndex();

    Crypto::Hash hash;

    if (!Common::podFromHex(hashStr, hash))
    {
        failJsonRpcRequest(
            -1,
            "Block hash specified is not a valid hex!",
            res
        );

        return {SUCCESS, 200};
    }

    CryptoNote::BlockTemplate block;

    try
    {
        block = m_core->getBlockByHash(hash);
    }
    catch (const std::runtime_error &)
    {
        failJsonRpcRequest(
            -5,
            "Block hash specified does not exist!",
            res
        );

        return {SUCCESS, 200};
    }

    CryptoNote::CachedBlock cachedBlock(block);

    const auto height = cachedBlock.getBlockIndex();
    const auto outputs = block.baseTransaction.outputs;
    const auto extraDetails = m_core->getBlockDetails(hash);

    const uint64_t reward = std::accumulate(outputs.begin(), outputs.end(), 0ull,
        [](const auto acc, const auto out) {
            return acc + out.amount;
        }
    );

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("block_header");
        writer.StartObject();
        {
            writer.Key("major_version");
            writer.Uint64(block.majorVersion);

            writer.Key("minor_version");
            writer.Uint64(block.minorVersion);

            writer.Key("timestamp");
            writer.Uint64(block.timestamp);

            writer.Key("prev_hash");
            writer.String(Common::podToHex(block.previousBlockHash));

            writer.Key("nonce");
            writer.Uint64(block.nonce);

            writer.Key("orphan_status");
            writer.Bool(extraDetails.isAlternative);

            writer.Key("height");
            writer.Uint64(height);

            writer.Key("depth");
            writer.Uint64(topHeight - height);

            writer.Key("hash");
            writer.String(Common::podToHex(hash));

            writer.Key("difficulty");
            writer.Uint64(m_core->getBlockDifficulty(height));

            writer.Key("reward");
            writer.Uint64(reward);

            writer.Key("num_txes");
            writer.Uint64(extraDetails.transactions.size());

            writer.Key("block_size");
            writer.Uint64(extraDetails.blockSize);
        }
        writer.EndObject();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlockHeaderByHeightJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getObjectFromJSON(body, "params");
    const auto height = getUint64FromJSON(params, "height");
    const auto topHeight = m_core->getTopBlockIndex();

    if (height > topHeight)
    {
        failJsonRpcRequest(
            -2,
            "Requested block header for a height that is higher than the current "
            "blockchain height! Current height: " + std::to_string(topHeight),
            res
        );

        return {SUCCESS, 200};
    }

    const auto hash = m_core->getBlockHashByIndex(height);
    const auto block = m_core->getBlockByHash(hash);

    const auto outputs = block.baseTransaction.outputs;
    const auto extraDetails = m_core->getBlockDetails(hash);

    const uint64_t reward = std::accumulate(outputs.begin(), outputs.end(), 0ull,
        [](const auto acc, const auto out) {
            return acc + out.amount;
        }
    );

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("block_header");
        writer.StartObject();
        {
            writer.Key("major_version");
            writer.Uint64(block.majorVersion);

            writer.Key("minor_version");
            writer.Uint64(block.minorVersion);

            writer.Key("timestamp");
            writer.Uint64(block.timestamp);

            writer.Key("prev_hash");
            writer.String(Common::podToHex(block.previousBlockHash));

            writer.Key("nonce");
            writer.Uint64(block.nonce);

            writer.Key("orphan_status");
            writer.Bool(extraDetails.isAlternative);

            writer.Key("height");
            writer.Uint64(height);

            writer.Key("depth");
            writer.Uint64(topHeight - height);

            writer.Key("hash");
            writer.String(Common::podToHex(hash));

            writer.Key("difficulty");
            writer.Uint64(m_core->getBlockDifficulty(height));

            writer.Key("reward");
            writer.Uint64(reward);

            writer.Key("num_txes");
            writer.Uint64(extraDetails.transactions.size());

            writer.Key("block_size");
            writer.Uint64(extraDetails.blockSize);
        }
        writer.EndObject();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlocksByHeightJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getObjectFromJSON(body, "params");
    const auto height = getUint64FromJSON(params, "height");
    const auto topHeight = m_core->getTopBlockIndex();

    if (height > topHeight)
    {
        failJsonRpcRequest(
            -2,
            "Requested block header for a height that is higher than the current "
            "blockchain height! Current height: " + std::to_string(topHeight),
            res
        );

        return {SUCCESS, 200};
    }

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        const uint64_t MAX_BLOCKS_COUNT = 30;
        const uint64_t startHeight = height < MAX_BLOCKS_COUNT ? 0 : height - MAX_BLOCKS_COUNT;

        writer.Key("blocks");
        writer.StartArray();
        {
            for (uint64_t i = height; i >= startHeight; i--)
            {
                writer.StartObject();

                const auto hash = m_core->getBlockHashByIndex(i);
                const auto block = m_core->getBlockByHash(hash);
                const auto extraDetails = m_core->getBlockDetails(hash);

                writer.Key("cumul_size");
                writer.Uint64(extraDetails.blockSize);

                writer.Key("difficulty");
                writer.Uint64(extraDetails.difficulty);

                writer.Key("hash");
                writer.String(Common::podToHex(hash));

                writer.Key("height");
                writer.Uint64(i);

                writer.Key("timestamp");
                writer.Uint64(block.timestamp);

                /* Plus one for coinbase tx */
                writer.Key("tx_count");
                writer.Uint64(block.transactionHashes.size() + 1);

                writer.EndObject();
            }
        }
        writer.EndArray();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getBlockDetailsByHashJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getObjectFromJSON(body, "params");
    const auto hashStr = getStringFromJSON(params, "hash");
    const auto topHeight = m_core->getTopBlockIndex();

    Crypto::Hash hash;

    if (hashStr.length() == 64)
    {
        if (!Common::podFromHex(hashStr, hash))
        {
            failJsonRpcRequest(
                -1,
                "Block hash specified is not a valid hex!",
                res
            );

            return {SUCCESS, 200};
        }
    }
    else
    {
        /* Hash parameter can be both a hash string, and a number... because cryptonote.. */
        try
        {
            uint64_t height = std::stoull(hashStr);

            hash = m_core->getBlockHashByIndex(height - 1);

            if (hash == Constants::NULL_HASH)
            {
                failJsonRpcRequest(
                    -2,
                    "Requested hash for a height that is higher than the current "
                    "blockchain height! Current height: " + std::to_string(topHeight),
                    res
                );

                return {SUCCESS, 200};
            }
        }
        catch (const std::out_of_range &)
        {
            failJsonRpcRequest(
                -1,
                "Block hash specified is not valid!",
                res
            );

            return {SUCCESS, 200};
        }
        catch (const std::invalid_argument &)
        {
            failJsonRpcRequest(
                -1,
                "Block hash specified is not valid!",
                res
            );

            return {SUCCESS, 200};
        }
    }

    const auto block = m_core->getBlockByHash(hash);
    const auto extraDetails = m_core->getBlockDetails(hash);
    const auto height = CryptoNote::CachedBlock(block).getBlockIndex();
    const auto outputs = block.baseTransaction.outputs;

    const uint64_t reward = std::accumulate(outputs.begin(), outputs.end(), 0ull,
        [](const auto acc, const auto out) {
            return acc + out.amount;
        }
    );

    const uint64_t blockSizeMedian = std::max(
        extraDetails.sizeMedian,
        static_cast<uint64_t>(
            m_core->getCurrency().blockGrantedFullRewardZoneByBlockVersion(block.majorVersion)
        )
    );

    std::vector<Crypto::Hash> ignore;
    std::vector<std::vector<uint8_t>> transactions;

    m_core->getTransactions(block.transactionHashes, transactions, ignore);

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("block");
        writer.StartObject();
        {
            writer.Key("major_version");
            writer.Uint64(block.majorVersion);

            writer.Key("minor_version");
            writer.Uint64(block.minorVersion);

            writer.Key("timestamp");
            writer.Uint64(block.timestamp);

            writer.Key("prev_hash");
            writer.String(Common::podToHex(block.previousBlockHash));

            writer.Key("nonce");
            writer.Uint64(block.nonce);

            writer.Key("orphan_status");
            writer.Bool(extraDetails.isAlternative);

            writer.Key("height");
            writer.Uint64(height);

            writer.Key("depth");
            writer.Uint64(topHeight - height);

            writer.Key("hash");
            writer.String(Common::podToHex(hash));

            writer.Key("difficulty");
            writer.Uint64(m_core->getBlockDifficulty(height));

            writer.Key("reward");
            writer.Uint64(reward);

            writer.Key("blockSize");
            writer.Uint64(extraDetails.blockSize);

            writer.Key("transactionsCumulativeSize");
            writer.Uint64(extraDetails.transactionsCumulativeSize);

            writer.Key("alreadyGeneratedCoins");
            writer.String(std::to_string(extraDetails.alreadyGeneratedCoins));

            writer.Key("alreadyGeneratedTransactions");
            writer.Uint64(extraDetails.alreadyGeneratedTransactions);

            writer.Key("sizeMedian");
            writer.Uint64(extraDetails.sizeMedian);

            writer.Key("baseReward");
            writer.Uint64(extraDetails.baseReward);

            writer.Key("penalty");
            writer.Double(extraDetails.penalty);

            writer.Key("effectiveSizeMedian");
            writer.Uint64(blockSizeMedian);

            uint64_t totalFee = 0;

            writer.Key("transactions");
            writer.StartArray();
            {
                /* Coinbase transaction */
                writer.StartObject();
                {
                    const auto txOutputs = block.baseTransaction.outputs;

                    const uint64_t outputAmount = std::accumulate(txOutputs.begin(), txOutputs.end(), 0ull,
                        [](const auto acc, const auto out) {
                            return acc + out.amount;
                        }
                    );

                    writer.Key("hash");
                    writer.String(Common::podToHex(getObjectHash(block.baseTransaction)));

                    writer.Key("fee");
                    writer.Uint64(0);

                    writer.Key("amount_out");
                    writer.Uint64(outputAmount);

                    writer.Key("size");
                    writer.Uint64(getObjectBinarySize(block.baseTransaction));
                }
                writer.EndObject();

                for (const std::vector<uint8_t> &rawTX : transactions)
                {
                    writer.StartObject();
                    {
                        CryptoNote::Transaction tx;

                        fromBinaryArray(tx, rawTX);

                        const uint64_t outputAmount = std::accumulate(tx.outputs.begin(), tx.outputs.end(), 0ull,
                            [](const auto acc, const auto out) {
                                return acc + out.amount;
                            }
                        );

                        const uint64_t inputAmount = std::accumulate(tx.inputs.begin(), tx.inputs.end(), 0ull,
                            [](const auto acc, const auto in) {
                                if (in.type() == typeid(CryptoNote::KeyInput))
                                {
                                    return acc + boost::get<CryptoNote::KeyInput>(in).amount;
                                }

                                return acc;
                            }
                        );

                        const uint64_t fee = inputAmount - outputAmount;

                        writer.Key("hash");
                        writer.String(Common::podToHex(getObjectHash(tx)));

                        writer.Key("fee");
                        writer.Uint64(fee);

                        writer.Key("amount_out");
                        writer.Uint64(outputAmount);

                        writer.Key("size");
                        writer.Uint64(getObjectBinarySize(tx));

                        totalFee += fee;
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.Key("totalFeeAmount");
            writer.Uint64(totalFee);
        }
        writer.EndObject();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getTransactionDetailsByHashJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    const auto params = getObjectFromJSON(body, "params");
    const auto hashStr = getStringFromJSON(params, "hash");

    Crypto::Hash hash;

    if (!Common::podFromHex(hashStr, hash))
    {
        failJsonRpcRequest(
            -1,
            "Block hash specified is not a valid hex!",
            res
        );

        return {SUCCESS, 200};
    }

    std::vector<Crypto::Hash> ignore;
    std::vector<std::vector<uint8_t>> rawTXs;
    std::vector<Crypto::Hash> hashes { hash };

    m_core->getTransactions(hashes, rawTXs, ignore);

    if (rawTXs.size() != 1)
    {
        failJsonRpcRequest(
            -1,
            "Block hash specified does not exist!",
            res
        );

        return {SUCCESS, 200};
    }

    CryptoNote::Transaction transaction;
    CryptoNote::TransactionDetails txDetails = m_core->getTransactionDetails(hash);

    const uint64_t blockHeight = txDetails.blockIndex;
    const auto blockHash = m_core->getBlockHashByIndex(blockHeight);
    const auto block = m_core->getBlockByHash(blockHash);
    const auto extraDetails = m_core->getBlockDetails(blockHash);

    fromBinaryArray(transaction, rawTXs[0]);

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("block");
        writer.StartObject();
        {
            writer.Key("cumul_size");
            writer.Uint64(extraDetails.blockSize);

            writer.Key("difficulty");
            writer.Uint64(extraDetails.difficulty);

            writer.Key("hash");
            writer.String(Common::podToHex(blockHash));

            writer.Key("height");
            writer.Uint64(blockHeight);

            writer.Key("timestamp");
            writer.Uint64(block.timestamp);

            /* Plus one for coinbase tx */
            writer.Key("tx_count");
            writer.Uint64(block.transactionHashes.size() + 1);
        }
        writer.EndObject();

        writer.Key("tx");
        writer.StartObject();
        {
            writer.Key("extra");
            writer.String(Common::podToHex(transaction.extra));

            writer.Key("unlock_time");
            writer.Uint64(transaction.unlockTime);

            writer.Key("version");
            writer.Uint64(transaction.version);

            writer.Key("vin");
            writer.StartArray();
            {
                for (const auto &input : transaction.inputs)
                {
                    const auto type = input.type() == typeid(CryptoNote::BaseInput)
                        ? "ff"
                        : "02";

                    writer.StartObject();
                    {
                        writer.Key("type");
                        writer.String(type);

                        writer.Key("value");
                        writer.StartObject();
                        {
                            if (input.type() == typeid(CryptoNote::BaseInput))
                            {
                                writer.Key("height");
                                writer.Uint64(boost::get<CryptoNote::BaseInput>(input).blockIndex);
                            }
                            else
                            {
                                const auto keyInput = boost::get<CryptoNote::KeyInput>(input);

                                writer.Key("k_image");
                                writer.String(Common::podToHex(keyInput.keyImage));

                                writer.Key("amount");
                                writer.Uint64(keyInput.amount);

                                writer.Key("key_offsets");
                                writer.StartArray();
                                {
                                    for (const auto index : keyInput.outputIndexes)
                                    {
                                        writer.Uint(index);
                                    }
                                }
                                writer.EndArray();
                            }
                        }
                        writer.EndObject();
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.Key("vout");
            writer.StartArray();
            {
                for (const auto &output : transaction.outputs)
                {
                    writer.StartObject();
                    {
                        writer.Key("amount");
                        writer.Uint64(output.amount);

                        writer.Key("target");
                        writer.StartObject();
                        {
                            writer.Key("data");
                            writer.StartObject();
                            {
                                writer.Key("key");
                                writer.String(Common::podToHex(boost::get<CryptoNote::KeyOutput>(output.target).key));
                            }
                            writer.EndObject();

                            writer.Key("type");
                            writer.String("02");
                        }
                        writer.EndObject();
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();
        }
        writer.EndObject();

        writer.Key("txDetails");
        writer.StartObject();
        {
            writer.Key("hash");
            writer.String(Common::podToHex(txDetails.hash));

            writer.Key("amount_out");
            writer.Uint64(txDetails.totalOutputsAmount);

            writer.Key("fee");
            writer.Uint64(txDetails.fee);

            writer.Key("mixin");
            writer.Uint64(txDetails.mixin);

            writer.Key("paymentId");
            if (txDetails.paymentId == Constants::NULL_HASH)
            {
                writer.String("");
            }
            else
            {
                writer.String(Common::podToHex(txDetails.paymentId));
            }

            writer.Key("size");
            writer.Uint64(txDetails.size);
        }
        writer.EndObject();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getTransactionsInPoolJsonRpc(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("jsonrpc");
    writer.String("2.0");

    writer.Key("result");
    writer.StartObject();
    {
        writer.Key("status");
        writer.String("OK");

        writer.Key("transactions");
        writer.StartArray();
        {
            for (const auto &tx : m_core->getPoolTransactions())
            {
                writer.StartObject();

                const uint64_t outputAmount = std::accumulate(tx.outputs.begin(), tx.outputs.end(), 0ull,
                    [](const auto acc, const auto out) {
                        return acc + out.amount;
                    }
                );

                const uint64_t inputAmount = std::accumulate(tx.inputs.begin(), tx.inputs.end(), 0ull,
                    [](const auto acc, const auto in) {
                        if (in.type() == typeid(CryptoNote::KeyInput))
                        {
                            return acc + boost::get<CryptoNote::KeyInput>(in).amount;
                        }

                        return acc;
                    }
                );

                const uint64_t fee = inputAmount - outputAmount;

                writer.Key("hash");
                writer.String(Common::podToHex(getObjectHash(tx)));

                writer.Key("fee");
                writer.Uint64(fee);

                writer.Key("amount_out");
                writer.Uint64(outputAmount);

                writer.Key("size");
                writer.Uint64(getObjectBinarySize(tx));

                writer.EndObject();
            }
        }
        writer.EndArray();
    }
    writer.EndObject();

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::queryBlocksLite(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    uint64_t timestamp = 0;

    if (hasMember(body, "timestamp"))
    {
        timestamp = getUint64FromJSON(body, "timestamp");
    }

    std::vector<Crypto::Hash> knownBlockHashes;

    if (hasMember(body, "blockIds"))
    {
        for (const auto &hashStrJson : getArrayFromJSON(body, "blockIds"))
        {
            Crypto::Hash hash;

            if (!Common::podFromHex(getStringFromJSONString(hashStrJson), hash))
            {
                failRequest(400, "Block hash specified is not a valid hex string!", res);
                return {SUCCESS, 400};
            }

            knownBlockHashes.push_back(hash);
        }
    }

    uint32_t startHeight;
    uint32_t currentHeight;
    uint32_t fullOffset;

    std::vector<CryptoNote::BlockShortInfo> blocks;

    if (!m_core->queryBlocksLite(knownBlockHashes, timestamp, startHeight, currentHeight, fullOffset, blocks))
    {
        failRequest(500, "Internal error: failed to queryblockslite", res);
        return {SUCCESS, 500};
    }

    writer.StartObject();

    writer.Key("fullOffset");
    writer.Uint64(fullOffset);

    writer.Key("currentHeight");
    writer.Uint64(currentHeight);

    writer.Key("startHeight");
    writer.Uint64(startHeight);

    writer.Key("items");
    writer.StartArray();
    {
        for (const auto &block : blocks)
        {
            writer.StartObject();
            {
                writer.Key("blockShortInfo.block");
                writer.StartArray();
                {
                    for (const auto c : block.block)
                    {
                        writer.Uint64(c);
                    }
                }
                writer.EndArray();

                writer.Key("blockShortInfo.blockId");
                writer.String(Common::podToHex(block.blockId));

                writer.Key("blockShortInfo.txPrefixes");
                writer.StartArray();
                {
                    for (const auto &prefix : block.txPrefixes)
                    {
                        writer.StartObject();
                        {
                            writer.Key("transactionPrefixInfo.txHash");
                            writer.String(Common::podToHex(prefix.txHash));

                            writer.Key("transactionPrefixInfo.txPrefix");
                            writer.StartObject();
                            {
                                writer.Key("extra");
                                writer.String(Common::toHex(prefix.txPrefix.extra));

                                writer.Key("unlock_time");
                                writer.Uint64(prefix.txPrefix.unlockTime);

                                writer.Key("version");
                                writer.Uint64(prefix.txPrefix.version);

                                writer.Key("vin");
                                writer.StartArray();
                                {
                                    for (const auto &input : prefix.txPrefix.inputs)
                                    {
                                        const auto type = input.type() == typeid(CryptoNote::BaseInput)
                                            ? "ff"
                                            : "02";

                                        writer.StartObject();
                                        {
                                            writer.Key("type");
                                            writer.String(type);

                                            writer.Key("value");
                                            writer.StartObject();
                                            {
                                                if (input.type() == typeid(CryptoNote::BaseInput))
                                                {
                                                    writer.Key("height");
                                                    writer.Uint64(boost::get<CryptoNote::BaseInput>(input).blockIndex);
                                                }
                                                else
                                                {
                                                    const auto keyInput = boost::get<CryptoNote::KeyInput>(input);

                                                    writer.Key("k_image");
                                                    writer.String(Common::podToHex(keyInput.keyImage));

                                                    writer.Key("amount");
                                                    writer.Uint64(keyInput.amount);

                                                    writer.Key("key_offsets");
                                                    writer.StartArray();
                                                    {
                                                        for (const auto index : keyInput.outputIndexes)
                                                        {
                                                            writer.Uint(index);
                                                        }
                                                    }
                                                    writer.EndArray();
                                                }
                                            }
                                            writer.EndObject();
                                        }
                                        writer.EndObject();
                                    }
                                }
                                writer.EndArray();

                                writer.Key("vout");
                                writer.StartArray();
                                {
                                    for (const auto &output : prefix.txPrefix.outputs)
                                    {
                                        writer.StartObject();
                                        {
                                            writer.Key("amount");
                                            writer.Uint64(output.amount);

                                            writer.Key("target");
                                            writer.StartObject();
                                            {
                                                writer.Key("data");
                                                writer.StartObject();
                                                {
                                                    writer.Key("key");
                                                    writer.String(Common::podToHex(boost::get<CryptoNote::KeyOutput>(output.target).key));
                                                }
                                                writer.EndObject();

                                                writer.Key("type");
                                                writer.String("02");
                                            }
                                            writer.EndObject();
                                        }
                                        writer.EndObject();
                                    }
                                }
                                writer.EndArray();
                            }
                            writer.EndObject();
                        }
                        writer.EndObject();
                    }
                }
                writer.EndArray();
            }
            writer.EndObject();
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getTransactionsStatus(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    std::unordered_set<Crypto::Hash> transactionHashes;

    for (const auto &hashStr : getArrayFromJSON(body, "transactionHashes"))
    {
        Crypto::Hash hash;

        if (!Common::podFromHex(getStringFromJSONString(hashStr), hash))
        {
            failRequest(400, "Transaction hash specified is not a valid hex string!", res);
            return {SUCCESS, 400};
        }

        transactionHashes.insert(hash);
    }

    std::unordered_set<Crypto::Hash> transactionsInPool;
    std::unordered_set<Crypto::Hash> transactionsInBlock;
    std::unordered_set<Crypto::Hash> transactionsUnknown;

    const bool success = m_core->getTransactionsStatus(
        transactionHashes, transactionsInPool, transactionsInBlock, transactionsUnknown
    );

    if (!success)
    {
        failRequest(500, "Internal error: failed to getTransactionsStatus", res);
        return {SUCCESS, 500};
    }

    writer.StartObject();

    writer.Key("transactionsInBlock");
    writer.StartArray();
    {
        for (const auto &hash : transactionsInBlock)
        {
            writer.String(Common::podToHex(hash));
        }
    }
    writer.EndArray();

    writer.Key("transactionsInPool");
    writer.StartArray();
    {
        for (const auto &hash : transactionsInPool)
        {
            writer.String(Common::podToHex(hash));
        }
    }
    writer.EndArray();

    writer.Key("transactionsUnknown");
    writer.StartArray();
    {
        for (const auto &hash : transactionsUnknown)
        {
            writer.String(Common::podToHex(hash));
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getPoolChanges(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    Crypto::Hash lastBlockHash;

    if (!Common::podFromHex(getStringFromJSON(body, "tailBlockId"), lastBlockHash))
    {
        failRequest(400, "tailBlockId specified is not a valid hex string!", res);
        return {SUCCESS, 400};
    }

    std::vector<Crypto::Hash> knownHashes;

    for (const auto &hashStr : getArrayFromJSON(body, "knownTxsIds"))
    {
        Crypto::Hash hash;

        if (!Common::podFromHex(getStringFromJSONString(hashStr), hash))
        {
            failRequest(400, "Transaction hash specified is not a valid hex string!", res);
            return {SUCCESS, 400};
        }

        knownHashes.push_back(hash);
    }

    std::vector<CryptoNote::TransactionPrefixInfo> addedTransactions;
    std::vector<Crypto::Hash> deletedTransactions;

    const bool atTopOfChain = m_core->getPoolChangesLite(
        lastBlockHash, knownHashes, addedTransactions, deletedTransactions
    );

    writer.StartObject();

    writer.Key("addedTxs");
    writer.StartArray();
    {
        for (const auto &prefix: addedTransactions)
        {
            writer.StartObject();
            {
                writer.Key("transactionPrefixInfo.txHash");
                writer.String(Common::podToHex(prefix.txHash));

                writer.Key("transactionPrefixInfo.txPrefix");
                writer.StartObject();
                {
                    writer.Key("extra");
                    writer.String(Common::toHex(prefix.txPrefix.extra));

                    writer.Key("unlock_time");
                    writer.Uint64(prefix.txPrefix.unlockTime);

                    writer.Key("version");
                    writer.Uint64(prefix.txPrefix.version);

                    writer.Key("vin");
                    writer.StartArray();
                    {
                        for (const auto &input : prefix.txPrefix.inputs)
                        {
                            const auto type = input.type() == typeid(CryptoNote::BaseInput)
                                ? "ff"
                                : "02";

                            writer.StartObject();
                            {
                                writer.Key("type");
                                writer.String(type);

                                writer.Key("value");
                                writer.StartObject();
                                {
                                    if (input.type() == typeid(CryptoNote::BaseInput))
                                    {
                                        writer.Key("height");
                                        writer.Uint64(boost::get<CryptoNote::BaseInput>(input).blockIndex);
                                    }
                                    else
                                    {
                                        const auto keyInput = boost::get<CryptoNote::KeyInput>(input);

                                        writer.Key("k_image");
                                        writer.String(Common::podToHex(keyInput.keyImage));

                                        writer.Key("amount");
                                        writer.Uint64(keyInput.amount);

                                        writer.Key("key_offsets");
                                        writer.StartArray();
                                        {
                                            for (const auto &index : keyInput.outputIndexes)
                                            {
                                                writer.Uint(index);
                                            }
                                        }
                                        writer.EndArray();
                                    }
                                }
                                writer.EndObject();
                            }
                            writer.EndObject();
                        }
                    }
                    writer.EndArray();

                    writer.Key("vout");
                    writer.StartArray();
                    {
                        for (const auto &output : prefix.txPrefix.outputs)
                        {
                            writer.StartObject();
                            {
                                writer.Key("amount");
                                writer.Uint64(output.amount);

                                writer.Key("target");
                                writer.StartObject();
                                {
                                    writer.Key("data");
                                    writer.StartObject();
                                    {
                                        writer.Key("key");
                                        writer.String(Common::podToHex(boost::get<CryptoNote::KeyOutput>(output.target).key));
                                    }
                                    writer.EndObject();

                                    writer.Key("type");
                                    writer.String("02");
                                }
                                writer.EndObject();
                            }
                            writer.EndObject();
                        }
                    }
                    writer.EndArray();
                }
                writer.EndObject();
            }
            writer.EndObject();
        }
    }
    writer.EndArray();

    writer.Key("deletedTxsIds");
    writer.StartArray();
    {
        for (const auto &hash : deletedTransactions)
        {
            writer.String(Common::podToHex(hash));
        }
    }
    writer.EndArray();

    writer.Key("isTailBlockActual");
    writer.Bool(atTopOfChain);

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::queryBlocksDetailed(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    uint64_t timestamp = 0;

    if (hasMember(body, "timestamp"))
    {
        timestamp = getUint64FromJSON(body, "timestamp");
    }

    std::vector<Crypto::Hash> knownBlockHashes;

    if (hasMember(body, "blockIds"))
    {
        for (const auto &hashStrJson : getArrayFromJSON(body, "blockIds"))
        {
            Crypto::Hash hash;

            if (!Common::podFromHex(getStringFromJSONString(hashStrJson), hash))
            {
                failRequest(400, "Block hash specified is not a valid hex string!", res);
                return {SUCCESS, 400};
            }

            knownBlockHashes.push_back(hash);
        }
    }

    uint64_t startHeight;
    uint64_t currentHeight;
    uint64_t fullOffset;

    uint64_t blockCount = CryptoNote::BLOCKS_SYNCHRONIZING_DEFAULT_COUNT;

    if (hasMember(body, "blockCount"))
    {
        blockCount = getUint64FromJSON(body, "blockCount");
    }

    std::vector<CryptoNote::BlockDetails> blocks;

    if (!m_core->queryBlocksDetailed(knownBlockHashes, timestamp, startHeight, currentHeight, fullOffset, blocks, blockCount))
    {
        failRequest(500, "Internal error: failed to queryblockslite", res);
        return {SUCCESS, 500};
    }

    writer.StartObject();

    writer.Key("fullOffset");
    writer.Uint64(fullOffset);

    writer.Key("currentHeight");
    writer.Uint64(currentHeight);

    writer.Key("startHeight");
    writer.Uint64(startHeight);

    writer.Key("blocks");
    writer.StartArray();
    {
        for (const auto &block : blocks)
        {
            writer.StartObject();
            {
                writer.Key("major_version");
                writer.Uint64(block.majorVersion);

                writer.Key("minor_version");
                writer.Uint64(block.minorVersion);

                writer.Key("timestamp");
                writer.Uint64(block.timestamp);

                writer.Key("prevBlockHash");
                writer.String(Common::podToHex(block.prevBlockHash));

                writer.Key("index");
                writer.Uint64(block.index);

                writer.Key("hash");
                writer.String(Common::podToHex(block.hash));

                writer.Key("difficulty");
                writer.Uint64(block.difficulty);

                writer.Key("reward");
                writer.Uint64(block.reward);

                writer.Key("blockSize");
                writer.Uint64(block.blockSize);

                writer.Key("alreadyGeneratedCoins");
                writer.String(std::to_string(block.alreadyGeneratedCoins));

                writer.Key("alreadyGeneratedTransactions");
                writer.Uint64(block.alreadyGeneratedTransactions);

                writer.Key("sizeMedian");
                writer.Uint64(block.sizeMedian);

                writer.Key("baseReward");
                writer.Uint64(block.baseReward);

                writer.Key("nonce");
                writer.Uint64(block.nonce);

                writer.Key("totalFeeAmount");
                writer.Uint64(block.totalFeeAmount);

                writer.Key("transactionsCumulativeSize");
                writer.Uint64(block.transactionsCumulativeSize);

                writer.Key("transactions");
                writer.StartArray();
                {
                    for (const auto &tx : block.transactions)
                    {
                        writer.StartObject();
                        {
                            writer.Key("blockHash");
                            writer.String(Common::podToHex(block.hash));

                            writer.Key("blockIndex");
                            writer.Uint64(block.index);

                            writer.Key("extra");
                            writer.StartObject();
                            {
                                writer.Key("nonce");
                                writer.StartArray();
                                {
                                    for (const auto &c : tx.extra.nonce)
                                    {
                                        writer.Uint64(c);
                                    }
                                }
                                writer.EndArray();

                                writer.Key("publicKey");
                                writer.String(Common::podToHex(tx.extra.publicKey));

                                writer.Key("raw");
                                writer.String(Common::toHex(tx.extra.raw));
                            }
                            writer.EndObject();

                            writer.Key("fee");
                            writer.Uint64(tx.fee);

                            writer.Key("hash");
                            writer.String(Common::podToHex(tx.hash));

                            writer.Key("inBlockchain");
                            writer.Bool(tx.inBlockchain);

                            writer.Key("inputs");
                            writer.StartArray();
                            {
                                for (const auto &input : tx.inputs)
                                {
                                    const auto type = input.type() == typeid(CryptoNote::BaseInputDetails)
                                        ? "ff"
                                        : "02";

                                    writer.StartObject();
                                    {
                                        writer.Key("type");
                                        writer.String(type);

                                        writer.Key("data");
                                        writer.StartObject();
                                        {
                                            if (input.type() == typeid(CryptoNote::BaseInputDetails))
                                            {
                                                const auto in = boost::get<CryptoNote::BaseInputDetails>(input);

                                                writer.Key("amount");
                                                writer.Uint64(in.amount);

                                                writer.Key("input");
                                                writer.StartObject();
                                                {
                                                    writer.Key("height");
                                                    writer.Uint64(in.input.blockIndex);
                                                }
                                                writer.EndObject();
                                            }
                                            else
                                            {
                                                const auto in = boost::get<CryptoNote::KeyInputDetails>(input);

                                                writer.Key("input");
                                                writer.StartObject();
                                                {
                                                    writer.Key("amount");
                                                    writer.Uint64(in.input.amount);

                                                    writer.Key("k_image");
                                                    writer.String(Common::podToHex(in.input.keyImage));

                                                    writer.Key("key_offsets");
                                                    writer.StartArray();
                                                    {
                                                        for (const auto &index : in.input.outputIndexes)
                                                        {
                                                            writer.Uint(index);
                                                        }
                                                    }
                                                    writer.EndArray();

                                                }
                                                writer.EndObject();

                                                writer.Key("mixin");
                                                writer.Uint64(in.mixin);

                                                writer.Key("output");
                                                writer.StartObject();
                                                {
                                                    writer.Key("transactionHash");
                                                    writer.String(Common::podToHex(in.output.transactionHash));

                                                    writer.Key("number");
                                                    writer.Uint64(in.output.number);
                                                }
                                                writer.EndObject();
                                            }
                                        }
                                        writer.EndObject();
                                    }
                                    writer.EndObject();
                                }
                            }
                            writer.EndArray();

                            writer.Key("mixin");
                            writer.Uint64(tx.mixin);

                            writer.Key("outputs");
                            writer.StartArray();
                            {
                                for (const auto &output : tx.outputs)
                                {
                                    writer.StartObject();
                                    {
                                        writer.Key("globalIndex");
                                        writer.Uint64(output.globalIndex);

                                        writer.Key("output");
                                        writer.StartObject();
                                        {
                                            writer.Key("amount");
                                            writer.Uint64(output.output.amount);

                                            writer.Key("target");
                                            writer.StartObject();
                                            {
                                                writer.Key("data");
                                                writer.StartObject();
                                                {
                                                    writer.Key("key");
                                                    writer.String(Common::podToHex(boost::get<CryptoNote::KeyOutput>(output.output.target).key));
                                                }
                                                writer.EndObject();

                                                writer.Key("type");
                                                writer.String("02");
                                            }
                                            writer.EndObject();
                                        }
                                        writer.EndObject();
                                    }
                                    writer.EndObject();
                                }
                            }
                            writer.EndArray();

                            writer.Key("paymentId");
                            writer.String(Common::podToHex(tx.paymentId));

                            writer.Key("signatures");
                            writer.StartArray();
                            {
                                int i = 0;

                                for (const auto &sigs : tx.signatures)
                                {
                                    for (const auto &sig : sigs)
                                    {
                                        writer.StartObject();
                                        {
                                            writer.Key("first");
                                            writer.Uint64(i);

                                            writer.Key("second");
                                            writer.String(Common::podToHex(sig));
                                        }
                                        writer.EndObject();
                                    }

                                    i++;
                                }
                            }
                            writer.EndArray();

                            writer.Key("signaturesSize");
                            writer.Uint64(tx.signatures.size());

                            writer.Key("size");
                            writer.Uint64(tx.size);

                            writer.Key("timestamp");
                            writer.Uint64(tx.timestamp);

                            writer.Key("totalInputsAmount");
                            writer.Uint64(tx.totalInputsAmount);

                            writer.Key("totalOutputsAmount");
                            writer.Uint64(tx.totalOutputsAmount);

                            writer.Key("unlockTime");
                            writer.Uint64(tx.unlockTime);
                        }
                        writer.EndObject();
                    }
                }
                writer.EndArray();
            }
            writer.EndObject();
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

/* Deprecated. Use getGlobalIndexes instead. */
std::tuple<Error, uint16_t> RpcServer::getGlobalIndexesDeprecated(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    Crypto::Hash hash;

    if (!Common::podFromHex(getStringFromJSON(body, "txid"), hash))
    {
        failRequest(400, "txid specified is not a valid hex string!", res);
        return {SUCCESS, 400};
    }

    std::vector<uint32_t> indexes;

    const bool success = m_core->getTransactionGlobalIndexes(hash, indexes);

    if (!success)
    {
        failRequest(500, "Internal error: Failed to getTransactionGlobalIndexes", res);
        return {SUCCESS, 500};
    }

    writer.StartObject();

    writer.Key("o_indexes");

    writer.StartArray();
    {
        for (const auto &index : indexes)
        {
            writer.Uint64(index);
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getRawBlocks(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    std::vector<Crypto::Hash> blockHashCheckpoints;

    if (hasMember(body, "blockHashCheckpoints"))
    {
        for (const auto &jsonHash : getArrayFromJSON(body, "blockHashCheckpoints"))
        {
            std::string hashStr = jsonHash.GetString();

            Crypto::Hash hash;
            Common::podFromHex(hashStr, hash);

            blockHashCheckpoints.push_back(hash);
        }
    }

    const uint64_t startHeight = hasMember(body, "startHeight")
        ? getUint64FromJSON(body, "startHeight")
        : 0;

    const uint64_t startTimestamp = hasMember(body, "startTimestamp")
        ? getUint64FromJSON(body, "startTimestamp")
        : 0;

    const uint64_t blockCount = hasMember(body, "blockCount")
        ? getUint64FromJSON(body, "blockCount")
        : 100;

    const bool skipCoinbaseTransactions = hasMember(body, "skipCoinbaseTransactions")
        ? getBoolFromJSON(body, "skipCoinbaseTransactions")
        : false;

    std::vector<CryptoNote::RawBlock> blocks;
    std::optional<WalletTypes::TopBlock> topBlockInfo;

    const bool success = m_core->getRawBlocks(
        blockHashCheckpoints,
        startHeight,
        startTimestamp,
        blockCount,
        skipCoinbaseTransactions,
        blocks,
        topBlockInfo
    );

    if (!success)
    {
        return {SUCCESS, 500};
    }

    writer.Key("items");
    writer.StartArray();
    {
        for (const auto &block : blocks)
        {
            writer.StartObject();

            writer.Key("block");
            writer.String(Common::toHex(block.block));

            writer.Key("transactions");
            writer.StartArray();
            for (const auto &transaction : block.transactions)
            {
                writer.String(Common::toHex(transaction));
            }
            writer.EndArray();

            writer.EndObject();
        }
    }
    writer.EndArray();

    if (topBlockInfo)
    {
        writer.Key("topBlock");
        writer.StartObject();
        {
            writer.Key("hash");
            writer.String(Common::podToHex(topBlockInfo->hash));

            writer.Key("height");
            writer.Uint64(topBlockInfo->height);
        }
        writer.EndObject();
    }

    writer.Key("synced");
    writer.Bool(blocks.empty());

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.body = sb.GetString();

    return {SUCCESS, 200};
}
