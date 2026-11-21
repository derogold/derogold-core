// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include "TransactionPool.h"
#include "TransactionPoolPolicy.h"

#include "CryptoNoteBasicImpl.h"
#include "common/TransactionExtra.h"
#include "common/int-util.h"

namespace CryptoNote
{
    /* Is the left hand side preferred over the right hand side? */
    bool TransactionPool::TransactionPriorityComparator::
        operator()(const PendingTransactionInfo &lhs, const PendingTransactionInfo &rhs) const
    {
        const CachedTransaction &left = lhs.cachedTransaction;
        const CachedTransaction &right = rhs.cachedTransaction;

        return TransactionPoolPolicy::hasHigherPriority(
            left.getTransactionFee(),
            lhs.receiveTime,
            left.getTransactionHash(),
            right.getTransactionFee(),
            rhs.receiveTime,
            right.getTransactionHash());
    }

    const Crypto::Hash &TransactionPool::PendingTransactionInfo::getTransactionHash() const
    {
        return cachedTransaction.getTransactionHash();
    }

    size_t TransactionPool::PaymentIdHasher::operator()(const std::optional<Crypto::Hash> &paymentId) const
    {
        if (!paymentId)
        {
            return std::numeric_limits<size_t>::max();
        }

        return std::hash<Crypto::Hash> {}(*paymentId);
    }

    bool TransactionPool::insertTransaction(PendingTransactionInfo &&transaction)
    {
        const Crypto::Hash hash = transaction.getTransactionHash();

        const auto [position, inserted] = m_transactions.emplace(hash, std::move(transaction));

        if (!inserted)
        {
            return false;
        }

        const PendingTransactionInfo *stored = &position->second;

        m_byPriority.insert(stored);
        m_byPaymentId.emplace(stored->paymentId, stored);

        return true;
    }

    bool TransactionPool::eraseTransaction(const Crypto::Hash &hash)
    {
        const auto position = m_transactions.find(hash);

        if (position == m_transactions.end())
        {
            return false;
        }

        const PendingTransactionInfo *stored = &position->second;

        /* The priority view is ordered by value, so several entries can
           compare equivalent to this one. Walk that equivalent range and drop
           the entry that is this exact transaction. */
        const auto priorityRange = m_byPriority.equal_range(stored);

        for (auto it = priorityRange.first; it != priorityRange.second; ++it)
        {
            if (*it == stored)
            {
                m_byPriority.erase(it);
                break;
            }
        }

        const auto paymentRange = m_byPaymentId.equal_range(stored->paymentId);

        for (auto it = paymentRange.first; it != paymentRange.second; ++it)
        {
            if (it->second == stored)
            {
                m_byPaymentId.erase(it);
                break;
            }
        }

        m_transactions.erase(position);

        return true;
    }

    TransactionPool::TransactionPool(std::shared_ptr<Logging::ILogger> logger):
        logger(logger, "TransactionPool")
    {
    }

    bool TransactionPool::pushTransaction(CachedTransaction &&transaction, TransactionValidatorState &&transactionState)
    {
        auto pendingTx = PendingTransactionInfo {static_cast<uint64_t>(time(nullptr)), std::move(transaction)};

        Crypto::Hash paymentId;
        if (getPaymentIdFromTxExtra(pendingTx.cachedTransaction.getTransaction().extra, paymentId))
        {
            pendingTx.paymentId = paymentId;
        }

        std::scoped_lock lock(m_transactionsMutex);

        if (m_transactions.count(pendingTx.getTransactionHash()) > 0)
        {
            logger(Logging::DEBUGGING) << "pushTransaction: transaction hash already present in index";
            return false;
        }

        if (hasIntersections(poolState, transactionState))
        {
            logger(Logging::DEBUGGING) << "pushTransaction: failed to merge states, some keys already used";
            return false;
        }

        mergeStates(poolState, transactionState);

        logger(Logging::DEBUGGING) << "pushed transaction " << pendingTx.getTransactionHash() << " to pool";

        return insertTransaction(std::move(pendingTx));
    }

    const std::optional<CachedTransaction> TransactionPool::tryGetTransaction(const Crypto::Hash &hash) const
    {
        std::scoped_lock lock(m_transactionsMutex);

        const auto it = m_transactions.find(hash);

        if (it != m_transactions.end())
        {
            return it->second.cachedTransaction;
        }

        return std::nullopt;
    }

    const CachedTransaction &TransactionPool::getTransaction(const Crypto::Hash &hash) const
    {
        std::scoped_lock lock(m_transactionsMutex);

        const auto it = m_transactions.find(hash);
        assert(it != m_transactions.end());

        return it->second.cachedTransaction;
    }

    bool TransactionPool::removeTransaction(const Crypto::Hash &hash)
    {
        std::scoped_lock lock(m_transactionsMutex);

        const auto it = m_transactions.find(hash);
        if (it == m_transactions.end())
        {
            logger(Logging::DEBUGGING) << "removeTransaction: transaction not found";
            return false;
        }

        excludeFromState(poolState, it->second.cachedTransaction);
        eraseTransaction(hash);

        logger(Logging::DEBUGGING) << "transaction " << hash << " removed from pool";
        return true;
    }

    size_t TransactionPool::getTransactionCount() const
    {
        std::scoped_lock lock(m_transactionsMutex);

        return m_transactions.size();
    }

    std::vector<Crypto::Hash> TransactionPool::getTransactionHashes() const
    {
        std::scoped_lock lock(m_transactionsMutex);

        std::vector<Crypto::Hash> hashes;
        for (const PendingTransactionInfo *transaction : m_byPriority)
        {
            hashes.push_back(transaction->getTransactionHash());
        }

        return hashes;
    }

    bool TransactionPool::checkIfTransactionPresent(const Crypto::Hash &hash) const
    {
        std::scoped_lock lock(m_transactionsMutex);

        return m_transactions.find(hash) != m_transactions.end();
    }

    const TransactionValidatorState &TransactionPool::getPoolTransactionValidationState() const
    {
        return poolState;
    }

    std::vector<CachedTransaction> TransactionPool::getPoolTransactions() const
    {
        std::scoped_lock lock(m_transactionsMutex);

        std::vector<CachedTransaction> result;
        result.reserve(m_byPriority.size());

        for (const PendingTransactionInfo *transaction : m_byPriority)
        {
            result.emplace_back(transaction->cachedTransaction);
        }

        return result;
    }

    std::tuple<std::vector<CachedTransaction>, std::vector<CachedTransaction>>
        TransactionPool::getPoolTransactionsForBlockTemplate() const
    {
        std::vector<CachedTransaction> regularTransactions;

        std::vector<CachedTransaction> fusionTransactions;

        std::scoped_lock lock(m_transactionsMutex);

        for (const PendingTransactionInfo *transaction : m_byPriority)
        {
            uint64_t transactionFee = transaction->cachedTransaction.getTransactionFee();

            if (transactionFee != 0)
            {
                regularTransactions.emplace_back(transaction->cachedTransaction);
            }
            else
            {
                fusionTransactions.emplace_back(transaction->cachedTransaction);
            }
        }

        return {regularTransactions, fusionTransactions};
    }

    uint64_t TransactionPool::getTransactionReceiveTime(const Crypto::Hash &hash) const
    {
        std::scoped_lock lock(m_transactionsMutex);

        const auto it = m_transactions.find(hash);
        assert(it != m_transactions.end());

        return it->second.receiveTime;
    }

    std::vector<Crypto::Hash> TransactionPool::getTransactionHashesByPaymentId(const Crypto::Hash &paymentId) const
    {
        std::scoped_lock lock(m_transactionsMutex);

        std::optional<Crypto::Hash> p(paymentId);

        const auto range = m_byPaymentId.equal_range(p);
        std::vector<Crypto::Hash> transactionHashes;
        transactionHashes.reserve(std::distance(range.first, range.second));
        for (auto it = range.first; it != range.second; ++it)
        {
            transactionHashes.push_back(it->second->getTransactionHash());
        }

        return transactionHashes;
    }

    void TransactionPool::flush()
    {
        const auto txns = getTransactionHashes();

        for (const auto tx : txns)
        {
            removeTransaction(tx);
        }
    }

} // namespace CryptoNote
