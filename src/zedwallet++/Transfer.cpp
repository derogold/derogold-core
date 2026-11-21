// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

/////////////////////////////////
#include <zedwallet++/Transfer.h>
/////////////////////////////////

#include <config/WalletConfig.h>
#include <iostream>
#include <utilities/ColouredMsg.h>
#include <utilities/FormatTools.h>
#include <utilities/Input.h>
#include <zedwallet++/GetInput.h>
#include <zedwallet++/Sweep.h>
#include <zedwallet++/Utilities.h>

namespace
{
    void cancel()
    {
        std::cout << WarningMsg("Cancelling transaction.\n");
    }
} // namespace

void transfer(const std::shared_ptr<WalletBackend> walletBackend)
{
    std::cout << InformationMsg("Note: You can type cancel at any time to "
                                "cancel the transaction\n\n");

    const bool integratedAddressesAllowed(true), cancelAllowed(true);

    std::string address =
        getAddress("What address do you want to transfer to?: ", integratedAddressesAllowed, cancelAllowed);

    if (address == "cancel")
    {
        cancel();
        return;
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
            cancel();
            return;
        }

        std::cout << "\n";
    }

    const auto [success, amount] =
        getAmountToAtomic("How much " + WalletConfig::ticker + " do you want to send?: ", cancelAllowed);

    std::cout << "\n";

    if (!success)
    {
        cancel();
        return;
    }

    sendTransaction(walletBackend, address, amount, paymentID);
}

void sendTransaction(
    const std::shared_ptr<WalletBackend> walletBackend,
    const std::string address,
    const uint64_t amount,
    const std::string paymentID)
{
    const auto unlockedBalance = walletBackend->getTotalUnlockedBalance();

    /* nodeFee will be zero if using a node without a fee, so we can add this
       safely */
    const auto [nodeFee, nodeAddress] = walletBackend->getNodeFee();

    const uint64_t fee = WalletConfig::defaultFee;

    /* The total balance required with fees added */
    const uint64_t total = amount + nodeFee + fee;

    if (total > unlockedBalance)
    {
        std::cout << WarningMsg("\nYou don't have enough funds to cover "
                                "this transaction!\n\n")
                  << "Funds needed: " << InformationMsg(Utilities::formatAmount(amount + fee + nodeFee))
                  << " (Includes a network fee of " << InformationMsg(Utilities::formatAmount(fee))
                  << " and a node fee of " << InformationMsg(Utilities::formatAmount(nodeFee))
                  << ")\nFunds available: " << SuccessMsg(Utilities::formatAmount(unlockedBalance)) << "\n\n";

        return cancel();
    }

    if (!confirmTransaction(walletBackend, address, amount, paymentID, nodeFee))
    {
        return cancel();
    }

    Error error;

    Crypto::Hash hash;

    std::tie(error, hash) = walletBackend->sendTransactionBasic(address, amount, paymentID);

    if (error == TOO_MANY_INPUTS_TO_FIT_IN_BLOCK)
    {
        std::cout << WarningMsg("Your transaction is too large to be accepted "
                                "by the network!\n")
                  << InformationMsg("It can be sent as several smaller transactions "
                                    "instead.\n")
                  << WarningMsg("You will pay the network fee, and any node fee, once\n"
                                "for each of them, so this costs more than a single\n"
                                "transfer, and your balance may not cover it.\n");

        if (!Utilities::confirm("Do you want to split it up?"))
        {
            return cancel();
        }

        /* The amount is what the destination receives and the fees are paid on
           top of it, the same as the transfer we just tried to send */
        const bool feeFromAmount = false;

        sendInChunks(walletBackend, address, paymentID, amount, feeFromAmount);

        return;
    }

    if (error)
    {
        std::cout << WarningMsg("Failed to send transaction: ") << WarningMsg(error) << std::endl;
        return;
    }
    else
    {
        std::cout << SuccessMsg("Transaction has been sent!\nHash: ") << SuccessMsg(hash) << "\n";
    }
}

bool confirmTransaction(
    const std::shared_ptr<WalletBackend> walletBackend,
    const std::string address,
    const uint64_t amount,
    const std::string paymentID,
    const uint64_t nodeFee)
{
    std::cout << InformationMsg("\nConfirm Transaction?\n");

    std::cout << "You are sending " << SuccessMsg(Utilities::formatAmount(amount)) << ", with a network fee of "
              << SuccessMsg(Utilities::formatAmount(WalletConfig::defaultFee)) << ",\nand a node fee of "
              << SuccessMsg(Utilities::formatAmount(nodeFee));

    if (paymentID != "")
    {
        std::cout << ",\nand a Payment ID of " << SuccessMsg(paymentID);
    }
    else
    {
        std::cout << ".";
    }

    std::cout << "\n\nFROM: " << SuccessMsg(walletBackend->getWalletLocation()) << "\nTO: " << SuccessMsg(address)
              << "\n\n";

    if (Utilities::confirm("Is this correct?"))
    {
        /* Use default message */
        ZedUtilities::confirmPassword(walletBackend, "Confirm your password: ");
        return true;
    }

    return false;
}
