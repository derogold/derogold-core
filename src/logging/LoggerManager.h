// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "../common/JsonValue.h"
#include "LoggerGroup.h"

#include <memory>
#include <mutex>

namespace Logging
{
    class LoggerManager : public LoggerGroup
    {
      public:
        LoggerManager();

        void configure(const Common::JsonValue &val);

        void operator()(
            const std::string &category,
            Level level,
            std::chrono::system_clock::time_point time,
            const std::string &body) override;

      private:
        std::vector<std::unique_ptr<CommonLogger>> loggers;

        std::mutex reconfigureLock;
    };

} // namespace Logging
