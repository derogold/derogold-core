// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "IBlockchainCacheFactory.h"

#include <logging/LoggerMessage.h>

namespace CryptoNote
{
    class IDataBase;

    class DatabaseBlockchainCacheFactory : public IBlockchainCacheFactory
    {
      public:
        /* liteHeight of 0 means full storage; see DatabaseBlockchainCache. */
        explicit DatabaseBlockchainCacheFactory(
            IDataBase &database,
            const std::shared_ptr<Logging::ILogger> &logger,
            uint32_t liteHeight = 0);

        virtual ~DatabaseBlockchainCacheFactory();

        virtual std::unique_ptr<IBlockchainCache> createRootBlockchainCache(const Currency &currency) override;

        virtual std::unique_ptr<IBlockchainCache>
            createBlockchainCache(const Currency &currency, IBlockchainCache *parent, uint32_t startIndex = 0) override;

      private:
        IDataBase &database;

        std::shared_ptr<Logging::ILogger> logger;

        uint32_t liteHeight;
    };

} // namespace CryptoNote
