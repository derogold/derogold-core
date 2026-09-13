// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "CommonLogger.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace Logging
{
    namespace
    {
        /* Render the local calendar date, replacing boost::posix_time's
           ptime::date(). */
        std::string formatDate(std::chrono::system_clock::time_point time)
        {
            const std::time_t asTimeT = std::chrono::system_clock::to_time_t(time);

            std::tm local {};

#ifdef _WIN32
            localtime_s(&local, &asTimeT);
#else
            localtime_r(&asTimeT, &local);
#endif

            std::ostringstream s;
            s << std::put_time(&local, "%Y-%m-%d");
            return s.str();
        }

        /* Render the local time of day to microsecond resolution, replacing
           ptime::time_of_day(). */
        std::string formatTimeOfDay(std::chrono::system_clock::time_point time)
        {
            const std::time_t asTimeT = std::chrono::system_clock::to_time_t(time);

            std::tm local {};

#ifdef _WIN32
            localtime_s(&local, &asTimeT);
#else
            localtime_r(&asTimeT, &local);
#endif

            const auto sinceEpoch = time.time_since_epoch();
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch);
            const auto microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch - seconds).count();

            std::ostringstream s;
            s << std::put_time(&local, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(6) << microseconds;
            return s.str();
        }

        std::string formatPattern(
            const std::string &pattern,
            const std::string &category,
            Level level,
            std::chrono::system_clock::time_point time)
        {
            std::stringstream s;

            for (const char *p = pattern.c_str(); p && *p != 0; ++p)
            {
                if (*p == '%')
                {
                    ++p;
                    switch (*p)
                    {
                        case 0:
                            break;
                        case 'C':
                            s << category;
                            break;
                        case 'D':
                            s << formatDate(time);
                            break;
                        case 'T':
                            s << formatTimeOfDay(time);
                            break;
                        case 'L':
                            s << std::setw(7) << std::left << ILogger::LEVEL_NAMES[level];
                            break;
                        default:
                            s << *p;
                    }
                }
                else
                {
                    s << *p;
                }
            }

            return s.str();
        }

    } // namespace

    void CommonLogger::
        operator()(const std::string &category, Level level, std::chrono::system_clock::time_point time, const std::string &body)
    {
        if (level <= logLevel && disabledCategories.count(category) == 0)
        {
            std::string body2 = body;
            if (!pattern.empty())
            {
                size_t insertPos = 0;
                if (!body2.empty() && body2[0] == ILogger::COLOR_DELIMETER)
                {
                    size_t delimPos = body2.find(ILogger::COLOR_DELIMETER, 1);
                    if (delimPos != std::string::npos)
                    {
                        insertPos = delimPos + 1;
                    }
                }

                body2.insert(insertPos, formatPattern(pattern, category, level, time));
            }

            doLogString(body2);
        }
    }

    void CommonLogger::setPattern(const std::string &pattern)
    {
        this->pattern = pattern;
    }

    void CommonLogger::disableCategory(const std::string &category)
    {
        disabledCategories.insert(category);
    }

    void CommonLogger::setMaxLevel(Level level)
    {
        logLevel = level;
    }

    CommonLogger::CommonLogger(Level level): logLevel(level), pattern("%D %T %L [%C] ") {}

    void CommonLogger::doLogString(const std::string &message) {}

} // namespace Logging
