// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2018-2026, The WrkzCoin developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "httplib_fwd.h"

#include <cstdint>
#include <memory>
#include <streambuf>
#include <string>

/* An IPC endpoint here means an AF_UNIX stream socket, and the thing that
   decides who may talk to it is the mode on the socket file. Windows has had
   AF_UNIX since Windows 10 1803, but with no SO_PEERCRED and no dependable
   permission enforcement on the socket file that access control would not
   exist, so a socket opened there would be reachable by every process on the
   machine. The vendored cpp-httplib compiles its own AF_UNIX handling out on
   Windows for the same reason. Both are compiled out rather than shipped
   insecure; callers get unsupportedReason() to print. */
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
#define DEROGOLD_IPC_SOCKET_SUPPORTED 0
#else
#define DEROGOLD_IPC_SOCKET_SUPPORTED 1
#endif

namespace Common
{
    namespace Ipc
    {
        /* Owner only. With no token in front of the socket this mode is the
           entire security model, so it is what an operator gets unless they
           deliberately widen it. */
        constexpr uint32_t DEFAULT_MODE = 0600;

        bool supported();

        std::string unsupportedReason();

        /* Linux writes abstract namespace sockets as "@name". They live in a
           kernel namespace instead of the filesystem, which means no owner and
           no mode: every process in the network namespace can connect. */
        bool isAbstract(const std::string &path);

        /* Accepts "600", "0600" and "0o600"; rejects anything that is not a
           whole octal permission triple. */
        bool parseMode(const std::string &text, uint32_t &mode);

        std::string formatMode(uint32_t mode);

        bool validatePath(const std::string &path, std::string &error);

        /* Clears a socket file left behind by a previous run. Refuses to touch
           anything that is not a socket, and refuses to steal a path another
           process is still listening on. */
        bool removeStaleSocket(const std::string &path, std::string &error);

        bool applyPermissions(
            const std::string &path,
            const uint32_t mode,
            const std::string &group,
            std::string &error);

        /* Best effort unlink for shutdown paths; never throws. */
        void cleanup(const std::string &path);

        /* Binds server to path with mode/group already in force, ready for
           listen_after_bind(). Keeps the umask window in one place. */
        bool bindServer(
            httplib::Server &server,
            const std::string &path,
            const uint32_t mode,
            const std::string &group,
            std::string &error);

        /* Points an already constructed client at an AF_UNIX path. */
        void configureClient(httplib::Client &client);

        /* Connects to a daemon's socket and hands back a blocking stream over
           it, for the callers that speak HTTP through a std::iostream instead
           of through cpp-httplib. Null on failure, with the reason in error.
           The buffer owns the descriptor and closes it when destroyed. */
        std::unique_ptr<std::streambuf> connectStream(const std::string &path, std::string &error);

        /* True when a daemon address is a socket path rather than a hostname.
           An absolute path or an "@name" abstract socket; a hostname can be
           neither, so the two can never be confused. */
        bool looksLikePath(const std::string &address);

        /* Describes an endpoint for logs and status output. */
        std::string describe(const std::string &path);

        /* True when a client can reach this daemon address on this build. A
           hostname always can. A socket path needs the AF_UNIX support that
           Windows does not get, and saying so where the address is read beats
           resolving it as a hostname and reporting the daemon unreachable. */
        bool validateClientAddress(const std::string &address, std::string &error);
    } // namespace Ipc
} // namespace Common
