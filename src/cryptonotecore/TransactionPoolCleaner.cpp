// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "TransactionPoolCleaner.h"

#include "TransactionPoolPolicy.h"
#include "Core.h"
#include "Mixins.h"
#include "common/StringTools.h"

#include <system/InterruptedException.h>
#include <system/Timer.h>

namespace CryptoNote
{
    TransactionPoolCleanWrapper::TransactionPoolCleanWrapper(
        std::unique_ptr<ITransactionPool> &&transactionPool,
        std::unique_ptr<ITimeProvider> &&timeProvider,
        std::shared_ptr<Logging::ILogger> logger,
        uint64_t timeout):
        transactionPool(std::move(transactionPool)),
        timeProvider(std::move(timeProvider)),
        logger(logger, "TransactionPoolCleanWrapper"),
        timeout(timeout)
    {
        assert(this->timeProvider);
    }

    TransactionPoolCleanWrapper::~TransactionPoolCleanWrapper() {}

    bool TransactionPoolCleanWrapper::pushTransaction(
        CachedTransaction &&tx,
        TransactionValidatorState &&transactionState)
    {
        return !isTransactionRecentlyDeleted(tx.getTransactionHash())
               && transactionPool->pushTransaction(std::move(tx), std::move(transactionState));
    }

    const CachedTransaction &TransactionPoolCleanWrapper::getTransaction(const Crypto::Hash &hash) const
    {
        return transactionPool->getTransaction(hash);
    }

    const std::optional<CachedTransaction> TransactionPoolCleanWrapper::tryGetTransaction(const Crypto::Hash &hash) const
    {
        return transactionPool->tryGetTransaction(hash);
    }

    bool TransactionPoolCleanWrapper::removeTransaction(const Crypto::Hash &hash)
    {
        return transactionPool->removeTransaction(hash);
    }

    size_t TransactionPoolCleanWrapper::getTransactionCount() const
    {
        return transactionPool->getTransactionCount();
    }

    std::vector<Crypto::Hash> TransactionPoolCleanWrapper::getTransactionHashes() const
    {
        return transactionPool->getTransactionHashes();
    }

    bool TransactionPoolCleanWrapper::checkIfTransactionPresent(const Crypto::Hash &hash) const
    {
        return transactionPool->checkIfTransactionPresent(hash);
    }

    const TransactionValidatorState &TransactionPoolCleanWrapper::getPoolTransactionValidationState() const
    {
        return transactionPool->getPoolTransactionValidationState();
    }

    std::vector<CachedTransaction> TransactionPoolCleanWrapper::getPoolTransactions() const
    {
        return transactionPool->getPoolTransactions();
    }

    std::tuple<std::vector<CachedTransaction>, std::vector<CachedTransaction>>
        TransactionPoolCleanWrapper::getPoolTransactionsForBlockTemplate() const
    {
        return transactionPool->getPoolTransactionsForBlockTemplate();
    }

    uint64_t TransactionPoolCleanWrapper::getTransactionReceiveTime(const Crypto::Hash &hash) const
    {
        return transactionPool->getTransactionReceiveTime(hash);
    }

    std::vector<Crypto::Hash>
        TransactionPoolCleanWrapper::getTransactionHashesByPaymentId(const Crypto::Hash &paymentId) const
    {
        return transactionPool->getTransactionHashesByPaymentId(paymentId);
    }

    void TransactionPoolCleanWrapper::flush()
    {
        return transactionPool->flush();
    }

    std::vector<Crypto::Hash> TransactionPoolCleanWrapper::clean(const uint32_t height)
    {
        try
        {
            uint64_t currentTime = timeProvider->now();
            auto transactionHashes = transactionPool->getTransactionHashes();

            std::vector<Crypto::Hash> deletedTransactions;
            for (const auto &hash : transactionHashes)
            {
                const uint64_t receiveTime = transactionPool->getTransactionReceiveTime(hash);
                const bool expired = TransactionPoolPolicy::isExpired(currentTime, receiveTime, timeout);
                if (expired)
                {
                    logger(Logging::DEBUGGING) << "Deleting transaction " << Common::podToHex(hash) << " from pool";
                    recentlyDeletedTransactions.insert_or_assign(hash, currentTime);
                    if (transactionPool->removeTransaction(hash))
                    {
                        deletedTransactions.emplace_back(hash);
                    }
                    continue;
                }

                CachedTransaction transaction = transactionPool->getTransaction(hash);
                std::vector<CachedTransaction> transactions;
                transactions.emplace_back(transaction);

                auto [success, error] = Mixins::validate(transactions, height);

                if (!success)
                {
                    logger(Logging::DEBUGGING)
                        << "Deleting invalid transaction " << Common::podToHex(hash) << " from pool." << error;
                    recentlyDeletedTransactions.insert_or_assign(hash, currentTime);
                    if (transactionPool->removeTransaction(hash))
                    {
                        deletedTransactions.emplace_back(hash);
                    }
                }
            }

            cleanRecentlyDeletedTransactions(currentTime);
            return deletedTransactions;
        }
        catch (System::InterruptedException &)
        {
            throw;
        }
        catch (std::exception &e)
        {
            logger(Logging::WARNING) << "Caught an exception: " << e.what() << ", stopping cleaning procedure cycle";
            throw;
        }
    }

    bool TransactionPoolCleanWrapper::isTransactionRecentlyDeleted(const Crypto::Hash &hash) const
    {
        const auto it = recentlyDeletedTransactions.find(hash);
        if (it == recentlyDeletedTransactions.end())
        {
            return false;
        }

        const uint64_t currentTime = timeProvider->now();
        return TransactionPoolPolicy::isRecentlyDeleted(currentTime, it->second, timeout);
    }

    void TransactionPoolCleanWrapper::cleanRecentlyDeletedTransactions(uint64_t currentTime)
    {
        for (auto it = recentlyDeletedTransactions.begin(); it != recentlyDeletedTransactions.end();)
        {
            if (!TransactionPoolPolicy::isRecentlyDeleted(currentTime, it->second, timeout))
            {
                it = recentlyDeletedTransactions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

} // namespace CryptoNote
