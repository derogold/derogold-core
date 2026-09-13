// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

/////////////////////////////
#include <zedwallet++/Sync.h>
/////////////////////////////

#include <chrono>
#include <common/SignalHandler.h>
#include <config/WalletConfig.h>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <tuple>
#include <utilities/ColouredMsg.h>
#include <utilities/FormatTools.h>
#include <zedwallet++/CommandImplementations.h>

namespace
{
    /* Return current local time as "YYYY-MM-DD HH:MM:SS" */
    std::string nowTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&t));
        return std::string(buf);
    }
} // anonymous namespace

void syncWallet(const std::shared_ptr<WalletBackend> walletBackend)
{
    auto [walletBlockCount, localDaemonBlockCount, networkBlockCount] = walletBackend->getSyncStatus();

    /* Fully synced */
    if (walletBlockCount == networkBlockCount)
    {
        return;
    }

    if (localDaemonBlockCount + 1 <= networkBlockCount)
    {
        std::cout << "Your " << WalletConfig::daemonName << " isn't fully "
                  << "synced yet!\n"
                  << "Until you are fully synced, you won't be able to send "
                  << "transactions,\nand your balance may be missing or "
                  << "incorrect!\n\n";
    }

    if (walletBlockCount == 0)
    {
        std::stringstream stream;

        stream << "Scanning through the blockchain to find transactions "
               << "that belong to you.\n"
               << "Please wait, this will take some time.\n\n";

        std::cout << InformationMsg(stream.str());
    }
    else
    {
        std::stringstream stream;

        stream << "Scanning through the blockchain to find any new "
               << "transactions you received\nwhilst your wallet wasn't open.\n"
               << "Please wait, this may take some time.\n\n";

        std::cout << InformationMsg(stream.str());
    }

    uint64_t lastSavedBlock = walletBlockCount;

    /* Amount of times we have looped without getting any new blocks */
    uint32_t stuckCounter = 0;

    /* Whether we have already shown the prune floor warning this session */
    bool shownPruneWarning = false;

    while (walletBlockCount < localDaemonBlockCount)
    {
        /* Update the outer variables rather than declaring new ones. A
           structured binding here shadowed localDaemonBlockCount, so the loop
           condition kept testing the value read before the loop started and the
           foreground sync stopped at whatever height the daemon had on entry. */
        const auto syncStatus = walletBackend->getSyncStatus();

        const uint64_t tmpWalletBlockCount = std::get<0>(syncStatus);

        localDaemonBlockCount = std::get<1>(syncStatus);
        networkBlockCount = std::get<2>(syncStatus);

        /* Show a one-time warning when the daemon's prune floor is detected.
           Raw block data below the prune floor is not available, so transactions
           in that range cannot be detected. Sync continues from the prune floor. */
        if (!shownPruneWarning)
        {
            const uint64_t pruneFloor = walletBackend->getPruneFloor();

            if (pruneFloor > 0)
            {
                shownPruneWarning = true;

                std::cout << WarningMsg(
                    "\nNote: This daemon has pruned raw block data below height " +
                    std::to_string(pruneFloor) + ".\n"
                    "Blocks 0 to " + std::to_string(pruneFloor - 1) +
                    " are synced from the daemon's compact archive.\n"
                    "If that archive predates this daemon version it may not carry the\n"
                    "output indexes needed to spend those inputs. Balances shown for that\n"
                    "range are correct either way; re-sync against a node holding the full\n"
                    "chain if a spend reports insufficient funds.\n")
                    << std::endl;
            }
        }

        std::cout << "[" << nowTimestamp() << "] "
                  << SuccessMsg(tmpWalletBlockCount) << " of " << InformationMsg(localDaemonBlockCount) << std::endl;

        if (walletBlockCount == tmpWalletBlockCount)
        {
            stuckCounter++;
        }
        else
        {
            stuckCounter = 0;
        }

        /* Get any transactions in between the previous height and the new
           height — display as compact one-liners during sync */
        for (const auto &tx : walletBackend->getTransactionsRange(walletBlockCount, tmpWalletBlockCount))
        {
            /* Don't print out fusion transactions */
            if (!tx.isFusionTransaction())
            {
                std::stringstream txStream;

                if (tx.totalAmount() < 0)
                {
                    const int64_t amount = std::abs(tx.totalAmount());
                    txStream << "[" << nowTimestamp() << "] OUT "
                        << Utilities::formatAmount(amount) << " (fee " << Utilities::formatAmount(tx.fee)
                        << ") | height " << tx.blockHeight
                        << " | tx: " << tx.hash;
                    if (!tx.paymentID.empty())
                    {
                        txStream << " | payment id: " << tx.paymentID;
                    }
                    std::cout << WarningMsg(txStream.str()) << std::endl;
                }
                else
                {
                    txStream << "[" << nowTimestamp() << "] IN  "
                        << Utilities::formatAmount(tx.totalAmount())
                        << " | height " << tx.blockHeight
                        << " | tx: " << tx.hash;
                    if (!tx.paymentID.empty())
                    {
                        txStream << " | payment id: " << tx.paymentID;
                    }
                    std::cout << SuccessMsg(txStream.str()) << std::endl;
                }
            }
        }

        walletBlockCount = tmpWalletBlockCount;

        /* Save every 10k blocks */
        if (walletBlockCount > lastSavedBlock + 10000)
        {
            std::cout << InformationMsg("\nSaving progress...\n\n");

            walletBackend->save();

            lastSavedBlock = walletBlockCount;
        }

        if (stuckCounter >= 20)
        {
            std::stringstream stream;

            stream << "Syncing may be stuck. Ensure your daemon or remote "
                      "node is online, and not syncing.\n(Syncing often stalls "
                      "wallet operation)\nGive the daemon a restart if possible.\n"
                   << "If this persists, visit " << WalletConfig::contactLink << " for support.";

            std::cout << WarningMsg(stream.str()) << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
