# Lite Node

A lite node stores **full block data only from a chosen height upward**. Below that
height it keeps just the indexes that later blocks actually need: key output info,
spent key images, per-amount output counts, and block headers. The block bodies,
transaction records, payment ID index, timestamp index and wallet sync archive for
those heights are never written.

This is a different feature from `--prune`, and the two cannot be combined.

| | `--prune` | `--lite` |
|---|---|---|
| What it drops | Raw block bodies older than `--prune-depth` | Bodies, transaction records, payment IDs, timestamp and wallet-sync indexes below `--lite-height` |
| When | A pass after syncing, repeatedly | At write time, from the very first block |
| Reversible | Yes, by resyncing | No — permanent for the database |
| Wallet rescan below the cut | Works, via the wallet sync archive | Does not work |

## Usage

```
--lite                  Enable lite mode. Permanent for this database.
--lite-height <H>       Height at and above which full block data is kept. Required.
```

Example — a wallet created at height 4,000,000, and a node just for it:

```
DeroGoldd --lite --lite-height 4000000
```

That node syncs, mines, relays and validates exactly like a full node, and the
wallet syncs against it normally.

## Choosing H

**Set `H` at or below your wallet's creation height.** A wallet cannot sync from
any point below the lite height against this node, so a value above the wallet's
birthday makes the node useless for it.

Two hard limits are enforced:

- `H` must be at least `MIN_LITE_FULL_BLOCK_DEPTH` (14 days, 4032 blocks at the
  current 300-second target) below the network top. A lite node cannot undo a
  block below `H` — the records an undo needs were never written — so a reorg
  must never be able to reach that far. The check runs at the opening handshake,
  once several peers have reported their heights, and stops the daemon if the
  configured height is too shallow.
- `H` is fixed for the life of the database. It is written to the database the
  first time the node starts, and a later run with a different `--lite-height`,
  or with none, refuses to start rather than treat an index-only chain as a
  complete one. Changing it means deleting the data directory and resyncing.

### With `--sync-from-height`

The two floors are different things and can be combined. `--sync-from-height`
means no block below the anchor was ever downloaded; `--lite-height` means blocks
below it were downloaded and stored as indexes only. Setting `H` at or below the
bootstrap anchor therefore does nothing, because no block is written down there
either way.

## Who should run one

| | |
|---|---|
| A user, own node for own wallet | **Yes.** This is what lite mode is for. |
| An operator, public node others connect to | **No — run a full node**, with `--prune` if disk is tight. |
| An operator, private or service node, wallets you control | **Yes**, good fit. |

## What is lost

Below `H`, permanently:

- Block bodies and transaction records. `f_block_json`, `f_transaction_json` and
  the rest of the explorer methods cannot answer for those heights, which is why
  `--lite` and `--daemon-mode explorer` are refused together.
- The payment ID index, so `f_transactions_by_payment_id_json` cannot find a
  transaction below `H`.
- The timestamp index, so a wallet cannot start a scan from a date below `H`.
- The wallet sync archive, so a wallet cannot sync or rescan from below `H`.
- `KeyOutputInfo.transactionHash`, which is zeroed. Only a rescan or an explorer
  reads it back, and a lite node offers neither down there. It is 32 bytes of
  high-entropy data per key output that nothing can ever read, and the one part
  of the database a compressor cannot help with — zeroed rather than removed, so
  the record layout and schema version stay exactly as they are.
- The block-index-to-key-image list used to undo a block. The key image to block
  index entries are kept, and those are what double-spend checks actually read.

Genesis is always stored in full: the chain is anchored on it.

## What is kept

Everything consensus needs. The key output info, the per-amount global indexes
and the amount list are all written at every height, and those are what ring
member resolution and decoy selection read. Spent key images are indexed at every
height, so double-spend checking is unaffected. Block headers, difficulties and
cumulative work are all present, so validation is identical to a full node's.

The chain-wide transaction count is still correct: index-only transactions are
counted even though their records are not stored.

## Serving peers

A lite node reports its lite height as its **prune floor**, which is the question
every caller is actually asking — the lowest height a block body can be read
from. That means the peer-serving, wallet-sync and `/getrawblocks` paths that
already handle a pruned node handle the lite region too.

The same caveat applies as for `--prune`: nothing in the P2P handshake advertises
that a node cannot serve low blocks, so a peer that asks for them sees missed
objects and drops the connection. This is why a lite node is a poor choice for a
public node. Advertising the floor in the handshake would need a P2P version
bump, which is a network-wide decision and is not part of this change.

## Checking a running node

```
sync_info
```

prints the lite height when one is set. `/info` carries it as `lite_node_height`;
`0` means the node holds every block.
