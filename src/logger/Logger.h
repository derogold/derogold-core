// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Logger
{
    enum LogLevel
    {
        TRACE = 5,
        DEBUG = 4,
        INFO = 3,
        WARNING = 2,
        FATAL = 1,
        DISABLED = 0,
    };

    enum LogCategory
    {
        SYNC,
        TRANSACTIONS,
        FILESYSTEM,
        SAVE,
        DAEMON,
        DAEMON_RPC,
        DATABASE,
    };

    std::string logLevelToString(LogLevel level);

    LogLevel stringToLogLevel(std::string level);

    std::string logCategoryToString(LogCategory category);

    class Logger
    {
      public:
        Logger() = default;

        void log(const std::string &message, LogLevel level, const std::vector<LogCategory> &categories) const;

        void setLogLevel(LogLevel level);

        /* The level currently in effect. Lets callers skip building an
           expensive message that would only be discarded. */
        LogLevel getLogLevel() const;

        void setLogCallback(const std::function<void(std::string prettyMessage,
                                                     std::string message,
                                                     LogLevel level,
                                                     std::vector<LogCategory> categories)> &callback);

      private:
        /* Logging disabled by default */
        LogLevel m_logLevel = DISABLED;

        std::function<void(std::string prettyMessage,
                           std::string message,
                           LogLevel level,
                           std::vector<LogCategory> categories)> m_callback;
    };

    /* Global logger instance */
    extern Logger logger;
} // namespace Logger
