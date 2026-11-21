// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

/////////////////////////////////////
#include <walletapi/ParseArguments.h>
/////////////////////////////////////

#include "version.h"

#include <common/IpcSocket.h>
#include <config/CliHeader.h>
#include <config/Config.h>
#include <config/CryptoNoteConfig.h>
#include <config/WalletConfig.h>
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>
#include <thread>

ApiConfig parseArguments(int argc, char **argv)
{
    ApiConfig config;

    cxxopts::Options options(argv[0], CryptoNote::getProjectCLIHeader());

    bool help, version, scanCoinbaseTransactions, noConsole;

    int logLevel;

    unsigned int threads;

    std::string logFilePath;

    options.add_options("Core")(
        "h,help", "Display this help message", cxxopts::value<bool>(help)->implicit_value("true"))

        ("log-level",
         "Specify log level",
         cxxopts::value<int>(logLevel)->default_value(std::to_string(config.logLevel)),
         "#")

        ("log-file",
         "Specify filepath to log to. Logging to file is disabled by default",
         cxxopts::value<std::string>(logFilePath),
         "<file>")

        ("no-console",
         "If set, will not provide an interactive console",
         cxxopts::value<bool>(noConsole)->default_value("false")->implicit_value("true"))

        ("scan-coinbase-transactions",
         "Scan miner/coinbase transactions",
         cxxopts::value<bool>(scanCoinbaseTransactions)->default_value("false")->implicit_value("true"))

        ("threads",
         "Specify number of wallet sync threads",
         cxxopts::value<unsigned int>(threads)->default_value(
             std::to_string(std::max(1u, std::thread::hardware_concurrency()))),
         "#")

        ("v,version",
         "Output software version information",
         cxxopts::value<bool>(version)->default_value("false")->implicit_value("true"));

    options.add_options("Daemon")(
        "daemon-address",
        "The daemon to use for node operations, for requests that do not name "
        "one. A host, or the path of a daemon's IPC socket.",
        cxxopts::value<std::string>(config.daemonHost)->default_value(config.daemonHost),
        "<host|path>")

        ("daemon-port",
         "The daemon RPC port to use for node operations. Ignored for a socket.",
         cxxopts::value<uint16_t>(config.daemonPort)->default_value(std::to_string(config.daemonPort)),
         "<port>")

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            ("daemon-ssl",
             "Use SSL when connecting to the daemon.",
             cxxopts::value<bool>(config.daemonSSL)->default_value("false")->implicit_value("true"))
#endif
        ;

    options.add_options("Network")(
        "p,port",
        "The port to listen on for http requests",
        cxxopts::value<uint16_t>(config.port)->default_value(std::to_string(CryptoNote::SERVICE_DEFAULT_PORT)),
        "<port>")

        ("rpc-bind-ip",
         "Interface IP address for the RPC service",
         cxxopts::value<std::string>(config.rpcBindIp)->default_value("127.0.0.1"));

    options.add_options("RPC")(
        "enable-cors",
        "Adds header 'Access-Control-Allow-Origin' to the RPC responses. Uses the value specified as the domain. Use * "
        "for all.",
        cxxopts::value<std::string>(config.corsHeader),
        "<domain>")

        ("r,rpc-password",
         "Specify the <password> to access the RPC server.",
         cxxopts::value<std::string>(config.rpcPassword),
         "<password>");

    try
    {
        const auto result = options.parse(argc, argv);

        /* Rpc password must be specified if not --help or --version */
        if (result.count("rpc-password") == 0 && !(help || version))
        {
            std::cout << "You must specify an rpc-password!" << std::endl;
            std::cout << options.help({}) << std::endl;
            exit(1);
        }
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        std::cout << "Error: Unable to parse command line argument options: " << e.what() << std::endl << std::endl;
        std::cout << options.help({}) << std::endl;
        exit(1);
    }

    if (help) // Do we want to display the help message?
    {
        std::cout << options.help({}) << std::endl;
        exit(0);
    }
    else if (version) // Do we want to display the software version?
    {
        std::cout << CryptoNote::getProjectCLIHeader() << std::endl;
        exit(0);
    }

    if (logLevel < Logger::DISABLED || logLevel > Logger::TRACE)
    {
        std::cout << "Log level must be between " << Logger::DISABLED << " and " << Logger::TRACE << "!" << std::endl;
        exit(1);
    }
    else
    {
        config.logLevel = static_cast<Logger::LogLevel>(logLevel);
    }

    if (logFilePath != "")
    {
        config.loggingFilePath = logFilePath;

        std::ofstream logFile(logFilePath, std::ios_base::app);

        if (!logFile)
        {
            std::cout << "Failed to open log file. Please ensure you specified "
                      << "a valid filepath and have permissions to create files "
                      << "in this directory. Error: " << strerror(errno) << std::endl;

            exit(1);
        }
    }

    if (noConsole)
    {
        config.noConsole = true;
    }

    if (threads == 0)
    {
        std::cout << "Thread count must be at least 1" << std::endl;
        exit(1);
    }
    else
    {
        config.threads = threads;
    }

    if (scanCoinbaseTransactions)
    {
        Config::config.wallet.skipCoinbaseTransactions = false;
    }

    /* Said here rather than at the first request that uses it, so an operator
       who has pointed this at a socket the build cannot open finds out at
       launch. */
    if (std::string error; !Common::Ipc::validateClientAddress(config.daemonHost, error))
    {
        std::cout << "Cannot use --daemon-address " << config.daemonHost << ": " << error << std::endl;
        exit(1);
    }

    return config;
}
