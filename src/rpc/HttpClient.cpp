// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "HttpClient.h"

#include <common/IpcSocket.h>
#include <http/HttpParser.h>
#include <system/Ipv4Address.h>
#include <system/Ipv4Resolver.h>
#include <system/TcpConnector.h>

namespace CryptoNote
{
    HttpClient::HttpClient(System::Dispatcher &dispatcher, const std::string &address, uint16_t port):
        m_dispatcher(dispatcher),
        m_address(address),
        m_port(port)
    {
    }

    HttpClient::~HttpClient()
    {
        if (m_connected)
        {
            disconnect();
        }
    }

    void HttpClient::request(const HttpRequest &req, HttpResponse &res)
    {
        std::scoped_lock lock(m_mutex);

        if (!m_connected)
        {
            connect();
        }

        try
        {
            std::iostream stream(m_streamBuf.get());
            HttpParser parser;
            stream << req;
            stream.flush();
            parser.receiveResponse(stream, res);
        }
        catch (const std::exception &)
        {
            disconnect();
            throw;
        }
    }

    void HttpClient::connect()
    {
        try
        {
            /* An absolute path or an "@name" where a host goes names a
               daemon's local socket. Nothing resolvable looks like either, so
               a hostname can never be taken for one. Without this the resolver
               below was handed the path, failed to make an address of it, and
               reported the daemon unreachable - which is what
               --daemon-address /run/derogold/daemon.sock has always done here,
               documented or not. */
            if (Common::Ipc::looksLikePath(m_address))
            {
                std::string error;

                auto streamBuf = Common::Ipc::connectStream(m_address, error);

                if (!streamBuf)
                {
                    throw std::runtime_error(
                        "Could not connect to " + Common::Ipc::describe(m_address) + ": " + error);
                }

                m_streamBuf = std::move(streamBuf);
                m_ipc = true;
                m_connected = true;

                return;
            }

            auto ipAddr = System::Ipv4Resolver(m_dispatcher).resolve(m_address);
            m_connection = System::TcpConnector(m_dispatcher).connect(ipAddr, m_port);
            m_streamBuf.reset(new System::TcpStreambuf(m_connection));
            m_ipc = false;
            m_connected = true;
        }
        catch (const std::exception &e)
        {
            throw ConnectException(e.what());
        }
    }

    bool HttpClient::isConnected() const
    {
        return m_connected;
    }

    void HttpClient::disconnect()
    {
        /* Closes the descriptor for a socket connection, which is the whole
           of the shutdown there - m_connection was never used. */
        m_streamBuf.reset();

        if (!m_ipc)
        {
            try
            {
                m_connection.write(nullptr, 0); // Socket shutdown.
            }
            catch (std::exception &)
            {
                // Ignoring possible exception.
            }

            try
            {
                m_connection = System::TcpConnection();
            }
            catch (std::exception &)
            {
                // Ignoring possible exception.
            }
        }

        m_ipc = false;
        m_connected = false;
    }

    ConnectException::ConnectException(const std::string &whatArg): std::runtime_error(whatArg.c_str()) {}

} // namespace CryptoNote
