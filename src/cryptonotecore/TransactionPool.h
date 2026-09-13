// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "ITransactionPool.h"
#include "TransactionValidatiorState.h"
#include "crypto/crypto.h"

#include <optional>
#include <set>
#include <unordered_map>
#include <logging/LoggerMessage.h>
#include <logging/LoggerRef.h>

namespace CryptoNote
{
    class TransactionPool : public ITransactionPool
    {
      public:
        TransactionPool(std::shared_ptr<Logging::ILogger> logger);

        virtual bool
            pushTransaction(CachedTransaction &&transaction, TransactionValidatorState &&transactionState) override;

        virtual const CachedTransaction &getTransaction(const Crypto::Hash &hash) const override;

        virtual const std::optional<CachedTransaction> tryGetTransaction(const Crypto::Hash &hash) const override;

        virtual bool removeTransaction(const Crypto::Hash &hash) override;

        virtual size_t getFusionTransactionCount() const override;

        virtual size_t getTransactionCount() const override;

        virtual std::vector<Crypto::Hash> getTransactionHashes() const override;

        virtual bool checkIfTransactionPresent(const Crypto::Hash &hash) const override;

        virtual const TransactionValidatorState &getPoolTransactionValidationState() const override;

        virtual std::vector<CachedTransaction> getPoolTransactions() const override;

        virtual std::tuple<std::vector<CachedTransaction>, std::vector<CachedTransaction>>
            getPoolTransactionsForBlockTemplate() const override;

        virtual uint64_t getTransactionReceiveTime(const Crypto::Hash &hash) const override;

        virtual std::vector<Crypto::Hash> getTransactionHashesByPaymentId(const Crypto::Hash &paymentId) const override;

        virtual void flush() override;

      private:
        TransactionValidatorState poolState;

        struct PendingTransactionInfo
        {
            uint64_t receiveTime;

            CachedTransaction cachedTransaction;

            std::optional<Crypto::Hash> paymentId;

            const Crypto::Hash &getTransactionHash() const;
        };

        struct TransactionPriorityComparator
        {
            // lhs > hrs
            bool operator()(const PendingTransactionInfo &lhs, const PendingTransactionInfo &rhs) const;
        };

        struct PaymentIdHasher
        {
            size_t operator()(const std::optional<Crypto::Hash> &paymentId) const;
        };

        /* Orders the pointers held by the cost view by comparing what they
           point at. */
        struct TransactionPriorityPtrComparator
        {
            bool operator()(const PendingTransactionInfo *lhs, const PendingTransactionInfo *rhs) const
            {
                return TransactionPriorityComparator {}(*lhs, *rhs);
            }
        };

        /* Three views over one set of transactions, kept in step by hand.
           This replaces a boost::multi_index_container with the same three
           indexes: unique on transaction hash, ordered by mining priority, and
           grouped by payment id.

           The hash map owns the transactions and is the only place they live.
           It is node based, so the addresses the other two views hold stay
           valid when it rehashes. Every mutation below goes through
           insertTransaction and eraseTransaction so the views cannot drift
           apart. */
        std::unordered_map<Crypto::Hash, PendingTransactionInfo> m_transactions;

        std::multiset<const PendingTransactionInfo *, TransactionPriorityPtrComparator> m_byPriority;

        std::unordered_multimap<std::optional<Crypto::Hash>, const PendingTransactionInfo *, PaymentIdHasher>
            m_byPaymentId;

        /* Adds to all three views. Returns false if the hash is already
           present, matching the unique hash index. */
        bool insertTransaction(PendingTransactionInfo &&transaction);

        /* Removes from all three views. Returns false if not present. */
        bool eraseTransaction(const Crypto::Hash &hash);

        mutable std::mutex m_transactionsMutex;

        Logging::LoggerRef logger;
    };

} // namespace CryptoNote
