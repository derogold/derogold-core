// Copyright (c) 2018-2021, The DeroGold Developers
//
// Please see the included LICENSE file for more information.

//////////////////////////////
#include <zedwallet++/Sweep.h>
//////////////////////////////

#include <chrono>
#include <config/WalletConfig.h>
#include <iostream>
#include <thread>
#include <utilities/ColouredMsg.h>
#include <utilities/FormatTools.h>
#include <utilities/Input.h>
#include <zedwallet++/GetInput.h>
#include <zedwallet++/SweepMath.h>
#include <zedwallet++/Utilities.h>

namespace
{
    /* How many times we will wait for the change of a transaction we just sent
       to come back before giving up on it. Blocks are ten seconds, so this is
       a long time to be waiting. */
    const int maxWaitAttempts = 40;

    /* How many times in a row we will wait without managing to send anything
       in between */
    const int maxWaitRounds = 3;

    /* Give up rather than loop forever splitting a transaction that is never
       going to fit in a block */
    const uint64_t maxDivider = 1024;

    void cancel()
    {
        std::cout << WarningMsg("Cancelling sweep.\n");
    }

    /* The change of a transaction comes back to us locked. Wait for it, so the
       rest of the send can carry on. Returns whether more funds became
       spendable - if nothing is on its way back, waiting will not help. */
    bool waitForFunds(const std::shared_ptr<WalletBackend> walletBackend, const uint64_t spendableBefore)
    {
        for (int attempt = 0; attempt < maxWaitAttempts; attempt++)
        {
            const auto [unlockedBalance, lockedBalance] = walletBackend->getTotalBalance();

            if (lockedBalance == 0)
            {
                return false;
            }

            std::cout << InformationMsg("Waiting for the change of the last transaction to unlock:\n")

                      << WarningMsg("\nLocked balance: ") << WarningMsg(Utilities::formatAmount(lockedBalance))

                      << SuccessMsg("\nSpendable balance: ") << SuccessMsg(Utilities::formatAmount(spendableBefore))

                      << InformationMsg("\nWill check again in 15 seconds...\n\n");

            std::this_thread::sleep_for(std::chrono::seconds(15));

            if (walletBackend->getSpendableBalance() > spendableBefore)
            {
                return true;
            }
        }

        return false;
    }
} // namespace

void sweep(const std::shared_ptr<WalletBackend> walletBackend, const bool sweepAll)
{
    std::cout << InformationMsg("Note: You can type cancel at any time to "
                                "cancel the sweep\n\n");

    const bool integratedAddressesAllowed(true), cancelAllowed(true);

    /* nodeFee will be zero if using a node without a fee, so we can add this
       safely */
    const auto [nodeFee, nodeAddress] = walletBackend->getNodeFee();

    /* What each transaction of the sweep costs us on top of what arrives */
    const uint64_t costPerTx = WalletConfig::defaultFee + nodeFee;

    /* The unlocked balance includes inputs that can never be spent, so it is
       no use for working out what we can sweep */
    const uint64_t spendableBalance = walletBackend->getSpendableBalance();

    if (spendableBalance <= costPerTx || spendableBalance - costPerTx < WalletConfig::minimumSend)
    {
        std::stringstream stream;

        stream << "You need at least " << Utilities::formatAmount(costPerTx + WalletConfig::minimumSend)
               << " of spendable balance to sweep, to cover the fees and still send something,\n"
               << "but only " << Utilities::formatAmount(spendableBalance) << " of your balance can be spent!\n";

        std::cout << WarningMsg(stream.str());

        return cancel();
    }

    std::string address =
        getAddress("What address do you want to sweep to?: ", integratedAddressesAllowed, cancelAllowed);

    if (address == "cancel")
    {
        return cancel();
    }

    std::cout << "\n";

    std::string paymentID;

    if (address.length() == WalletConfig::standardAddressLength)
    {
        paymentID = getPaymentID(
            "What payment ID do you want to use?\n"
            "These are usually used for sending to exchanges.",
            cancelAllowed);

        if (paymentID == "cancel")
        {
            return cancel();
        }

        std::cout << "\n";
    }

    /* How much leaves the wallet, fees included */
    uint64_t amountToSweep = spendableBalance;

    if (!sweepAll)
    {
        std::stringstream prompt;

        prompt << "How much " << WalletConfig::ticker << " do you want to sweep?\n"
               << "The fees come out of this, so this is what leaves your wallet: ";

        const auto [success, amount] = getAmountToAtomic(prompt.str(), cancelAllowed);

        std::cout << "\n";

        if (!success)
        {
            return cancel();
        }

        if (amount > spendableBalance)
        {
            std::stringstream stream;

            stream << "You cannot sweep " << Utilities::formatAmount(amount) << ", only "
                   << Utilities::formatAmount(spendableBalance) << " of your balance can be spent!\n";

            std::cout << WarningMsg(stream.str());

            return cancel();
        }

        if (amount <= costPerTx || amount - costPerTx < WalletConfig::minimumSend)
        {
            std::stringstream stream;

            stream << "A sweep has to cover the fees of " << Utilities::formatAmount(costPerTx)
                   << " and still send at least " << Utilities::formatAmount(WalletConfig::minimumSend)
                   << ",\nso the smallest sweep possible is "
                   << Utilities::formatAmount(costPerTx + WalletConfig::minimumSend) << "!\n";

            std::cout << WarningMsg(stream.str());

            return cancel();
        }

        amountToSweep = amount;
    }

    if (!confirmSweep(walletBackend, address, amountToSweep, paymentID, nodeFee))
    {
        return cancel();
    }

    const bool feeFromAmount = true;

    sendInChunks(walletBackend, address, paymentID, amountToSweep, feeFromAmount);
}

bool sendInChunks(
    const std::shared_ptr<WalletBackend> walletBackend,
    const std::string address,
    const std::string paymentID,
    const uint64_t total,
    const bool feeFromAmount)
{
    const auto [nodeFee, nodeAddress] = walletBackend->getNodeFee();

    const uint64_t costPerTx = WalletConfig::defaultFee + nodeFee;

    /* How much of the send is left to do */
    uint64_t remaining = total;

    /* How much has arrived at the destination */
    uint64_t sent = 0;

    /* How many pieces we are splitting the remainder into. Doubled every time
       a transaction turns out to have too many inputs to fit in a block. */
    uint64_t divider = 1;

    int txNumber = 1;

    int waitRounds = 0;

    while (remaining > 0)
    {
        const uint64_t spendable = walletBackend->getSpendableBalance();

        const auto chunk = Sweep::calculateChunk(
            remaining, spendable, costPerTx, WalletConfig::minimumSend, divider, feeFromAmount);

        if (!chunk.possible)
        {
            /* What we just spent comes back to us as change - if any of it is
               still on its way, wait for it instead of giving up */
            if (waitRounds < maxWaitRounds && waitForFunds(walletBackend, spendable))
            {
                waitRounds++;
                continue;
            }

            break;
        }

        const auto [error, hash] = walletBackend->sendTransactionBasic(address, chunk.amount, paymentID);

        if (error == TOO_MANY_INPUTS_TO_FIT_IN_BLOCK)
        {
            divider *= 2;

            if (divider > maxDivider)
            {
                std::cout << WarningMsg("Could not split the transaction small "
                                        "enough to fit in a block, sorry.\n");

                break;
            }

            /* Picking the inputs and their mixins again takes a while, so let
               them know we have not frozen */
            std::cout << InformationMsg("Transaction is too large to fit in a block, "
                                        "splitting it into smaller ones...\n");

            continue;
        }

        if (error)
        {
            std::cout << WarningMsg("Failed to send transaction: ") << WarningMsg(error) << std::endl;

            break;
        }

        std::stringstream stream;

        stream << "Transaction number " << txNumber << " has been sent!\nHash: " << hash
               << "\nAmount: " << Utilities::formatAmount(chunk.amount) << "\n\n";

        std::cout << SuccessMsg(stream.str());

        sent += chunk.amount;

        /* The fees leave the wallet as well as the amount, so a sweep works
           through its total quicker than the destination receives it */
        remaining -= feeFromAmount ? chunk.walletCost : chunk.amount;

        /* Went well, revert to sending as much as possible at once */
        divider = 1;

        txNumber++;

        waitRounds = 0;
    }

    if (sent != 0)
    {
        std::stringstream stream;

        stream << "Sent " << Utilities::formatAmount(sent) << " to " << address << " in " << (txNumber - 1)
               << (txNumber == 2 ? " transaction.\n" : " transactions.\n");

        std::cout << SuccessMsg(stream.str());
    }

    if (remaining != 0)
    {
        std::stringstream stream;

        stream << Utilities::formatAmount(remaining)
               << " could not be sent - what is left of your balance cannot cover\n"
                  "another transaction fee, or cannot be spent.\n";

        std::cout << WarningMsg(stream.str());

        return false;
    }

    return true;
}

bool confirmSweep(
    const std::shared_ptr<WalletBackend> walletBackend,
    const std::string address,
    const uint64_t amountToSweep,
    const std::string paymentID,
    const uint64_t nodeFee)
{
    const uint64_t costPerTx = WalletConfig::defaultFee + nodeFee;

    std::cout << InformationMsg("\nConfirm Sweep?\n");

    std::cout << "You are sweeping " << SuccessMsg(Utilities::formatAmount(amountToSweep))
              << " out of your wallet.\nThe network fee of "
              << SuccessMsg(Utilities::formatAmount(WalletConfig::defaultFee)) << " and the node fee of "
              << SuccessMsg(Utilities::formatAmount(nodeFee))
              << " come out of that,\nonce for each transaction the sweep takes, so at most "
              << SuccessMsg(Utilities::formatAmount(amountToSweep - costPerTx)) << " will arrive";

    if (paymentID != "")
    {
        std::cout << ",\nwith a Payment ID of " << SuccessMsg(paymentID);
    }

    std::cout << ".\n\nFROM: " << SuccessMsg(walletBackend->getWalletLocation()) << "\nTO: " << SuccessMsg(address)
              << "\n\n";

    if (Utilities::confirm("Is this correct?"))
    {
        /* Use default message */
        ZedUtilities::confirmPassword(walletBackend, "Confirm your password: ");
        return true;
    }

    return false;
}
