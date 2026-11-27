# DeroGold v1.0.2.1

Pool and node operators: upgrade before block **3,000,000**. Version 1.0.2.0 rejected older seed and ordinary peers too early. This patch advertises P2P 11 and accepts protocols 9 and 10 while the local validated tip is below the fork. From block 3,000,000, the daemon requires P2P 11 and drops existing legacy connections. Fork height and chain-selection rules are unchanged.
