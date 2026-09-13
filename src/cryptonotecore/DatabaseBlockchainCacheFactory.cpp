// Copyright (c) 2018-2024, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "DatabaseBlockchainCacheFactory.h"

#include "BlockchainCache.h"
#include "DatabaseBlockchainCache.h"

namespace CryptoNote
{
    DatabaseBlockchainCacheFactory::DatabaseBlockchainCacheFactory(
        IDataBase &database,
        const std::shared_ptr<Logging::ILogger> &logger,
        const uint32_t liteHeight) :
        database(database),
        logger(logger),
        liteHeight(liteHeight)
    {
    }

    DatabaseBlockchainCacheFactory::~DatabaseBlockchainCacheFactory() = default;

    std::unique_ptr<IBlockchainCache>
        DatabaseBlockchainCacheFactory::createRootBlockchainCache(const Currency &currency)
    {
        return std::make_unique<DatabaseBlockchainCache>(currency, database, *this, logger, liteHeight);
    }

    std::unique_ptr<IBlockchainCache> DatabaseBlockchainCacheFactory::createBlockchainCache(
        const Currency &currency,
        IBlockchainCache *parent,
        uint32_t startIndex)
    {
        return std::make_unique<BlockchainCache>("", currency, logger, parent, startIndex);
    }

} // namespace CryptoNote
