# DeroGold Consensus Snapshot v1

This document specifies the fixed-height consensus-state snapshot format for the
initial supported mainnet anchor at height `2700000`.

PR 1 is specification and codec only. It does not change daemon startup,
bootstrap, validation, RPC, database, P2P, wallet, or mining behavior.

## Trust Model

The DeroGold chain does not commit a complete state root at height `2700000`.
The anchor block hash alone therefore does not prove the complete historical
state required for future consensus. Snapshot trust comes from deterministic
independent exports from archive nodes, comparison of their logical roots and
counts, and a release-approved root compiled into software in later PRs. Snapshot
download locations are untrusted; every imported artifact must be verified
against compiled expectations before it can affect consensus.

The initial supported snapshot is DeroGold mainnet state immediately after block
`2700000`.

Anchor hash:

```text
088438ac255023638252a926c0ff4177a90224a90453d5764902d1f844e3394c
```

The hash is present in `src/config/CryptoNoteCheckpoints.h` on development at
height `2700000`. Future PRs must still reject any snapshot whose manifest,
chunks, and logical root do not match the compiled approved values.

## Current State Inventory

The existing `--sync-from-height` implementation is synthetic and unsafe. The
new snapshot state must replace the data read by the following paths.

### Bootstrap And Metadata

`src/daemon/DaemonConfiguration.cpp` parses `--sync-from-height` and checks
`src/config/SyncBootstrapCheckpoints.h`.

`src/daemon/Daemon.cpp` looks up the bootstrap entry, parses its anchor hash,
and calls `Core::bootstrapFromHeight()`.

`src/cryptonotecore/Core.cpp` implements `Core::bootstrapFromHeight()` and
`Core::getSyncFloorHeight()`.

`src/cryptonotecore/DatabaseBlockchainCache.cpp` implements
`DatabaseBlockchainCache::injectBootstrapAnchor()` and `getSyncFloorHeight()`.
The current implementation writes synthetic cached block info, dummy hashes,
an empty raw anchor block, and a `sync_floor_height` metadata key.

Snapshot import must instead write explicit snapshot metadata:

- node mode;
- history floor;
- consensus-valid-from height;
- snapshot format name and version;
- anchor hash;
- approved logical state root.

### Transaction Validation And Mempool

`src/cryptonotecore/Core.cpp` calls `ValidateTransaction` from
`Core::validateTransaction()` and passes `getSyncFloorHeight()`.

`src/cryptonotecore/ValidateTransaction.cpp` validates:

- key image domain and duplicate key images in `validateTransactionInputs()`;
- mixin rules in `validateTransactionMixin()`;
- key-image spent status, output lookup, unlock status, signature count, and
  ring signatures in `validateTransactionInputsExpensive()`.

Current sync-floor bypasses return success when historical output data is
missing or partial. Snapshot state must make these normal reads complete and
must not permit missing ring members:

- `IBlockchainCache::checkIfSpent()`;
- `IBlockchainCache::extractKeyOutputKeys()`;
- `IBlockchainCache::isTransactionSpendTimeUnlocked()`;
- `IBlockchainCache::extractKeyOutputs()`.

### Historical Outputs

`src/cryptonotecore/DatabaseCacheData.h` defines `KeyOutputInfo`:

- output public key;
- originating transaction hash;
- unlock time;
- transaction output index.

`src/cryptonotecore/DatabaseCacheData.cpp` serializes those fields for the
native database.

`src/cryptonotecore/DatabaseBlockchainCache.cpp` writes historical output info
from `pushTransaction()` via `BlockchainWriteBatch::insertKeyOutputInfo()` and
reads it from:

- `extractKeyOutputKeys()`;
- `extractKeyOutputs()`;
- `getRandomOutsByAmount()`;
- `getKeyOutputsCountForAmount()`.

The snapshot must contain every `(amount, global output index)` through the
anchor, including spent outputs, because CryptoNote ring signatures may use
old or spent outputs as decoys.

### Historical Spent Key Images

`src/cryptonotecore/BlockchainWriteBatch.cpp` stores key image to spending
block index entries and block index to key-image lists for rewind/index use.

`src/cryptonotecore/BlockchainReadBatch.cpp` reads key image spend indexes
through `requestBlockIndexBySpentKeyImage()`.

`src/cryptonotecore/DatabaseBlockchainCache.cpp` uses those records in
`checkIfSpent()` and `getPushedBlockInfo()`.

The snapshot must contain every key image spent through the anchor and enough
height data to rebuild current lookup directions.

### Block Validation, Difficulty, Reward, And Reorg

`src/cryptonotecore/Core.cpp` validates parent linkage and block rules in
`Core::addBlock()` and block validation helpers.

`Core::getDifficultyForNextBlock()` reads:

- `IBlockchainCache::getLastTimestamps()`;
- `IBlockchainCache::getLastCumulativeDifficulties()`;
- `Currency::getNextDifficulty()`.

`Core::validateBlock()` reads timestamp windows using
`IBlockchainCache::getLastTimestamps()` and currently bypasses median timestamp
checks near `syncFloorHeight`.

Reward and block size windows use:

- `IBlockchainCache::getLastBlocksSizes()`;
- `IBlockchainCache::getAlreadyGeneratedCoins()`;
- `Currency` reward calculation paths called from block template and block
  validation.

Reorg and fork handling use:

- `findSegmentContainingBlock()`;
- `findMainChainSegmentContainingBlock()`;
- `IBlockchainCache::getBlockHash()`;
- `IBlockchainCache::getBlockIndex()`;
- `IBlockchainCache::getPushedBlockInfo()`;
- `IBlockchainCache::getCurrentCumulativeDifficulty()`;
- `IBlockchainCache::split()`, `popBlock()`, and child segment operations.

Snapshot import must supply real anchor and lookback records needed to validate
block `2700001`, reject forks at or below `2700000`, and permit normal validated
reorgs above the floor.

### P2P Synchronization

`src/cryptonoteprotocol/CryptoNoteProtocolHandler.cpp` builds and exchanges
chain locators through:

- `m_core.buildSparseChain()`;
- `Core::findBlockchainSupplement()`;
- `Core::getBlockHashes()`;
- `handle_request_chain()`;
- `handle_response_chain_entry()`;
- `request_missing_objects()`.

The current handler silently treats blocks below `syncFloorHeight` as already
known. Snapshot mode must instead advertise the real trusted anchor and request
block `2700001` onward without fabricated pre-anchor local history.

`src/p2p/` contains transport, peerlist, connection context, and levin protocol
plumbing. It does not define consensus state but must continue to carry chain
requests and object responses without claiming unavailable pre-floor history.

### Mining Templates

`src/cryptonotecore/Core.cpp::getBlockTemplate()` reads top hash, next height,
next difficulty, transaction pool state, block size median, emission, and reward
state to build mining templates.

`src/rpc/RpcServer.cpp::getBlockTemplateJsonRpc()` exposes template creation.

`src/daemon/StratumServer.cpp` serves mining jobs and submits found blocks via
core submit paths.

Snapshot mining is safe only after the imported state contains all output,
key-image, rolling difficulty, reward, and parent-linkage data needed to make
the same decisions as an archive node, and after PR 5 differential proof.

### RPC And Wallet History Boundaries

RPC methods that read raw blocks or transactions by height/hash flow through
`src/rpc/RpcServer.cpp` and `Core` methods such as:

- `getRawBlock()`;
- `getBlockDetails()`;
- `getTransactionDetails()`;
- `getBlocks()`;
- `getGlobalIndexesForRange()`;
- `getTransactionGlobalIndexes()`;
- `getRawTransactions()`.

Snapshot nodes must return explicit historical-data-unavailable responses for
raw pre-floor history. They must not return fabricated empty raw blocks or claim
to be archive nodes.

Wallet scanning from height `2700000` can recover outputs visible from that
height onward. Historical wallet recovery and raw pre-floor inspection require
an archive node.

## Canonical Format

All multi-byte integers are little-endian. Fixed hashes, keys, and key images
are encoded as their 32 raw bytes in native CryptoNote byte order. No raw C++
structs, object archives, pointer-sized values, or compiler-dependent layouts
are part of the external format.

Every record starts with an eight-byte ASCII magic. Unsupported magic or version
is a parse failure.

### Manifest Record

Magic: `DGSNAPM1`

Fields:

- `uint16 format_version`, currently `1`;
- `uint16 min_reader_version`, currently `1`;
- `uint16 network_len`, followed by UTF-8 bytes;
- `hash32 genesis_hash`;
- `uint32 anchor_height`;
- `hash32 anchor_hash`;
- `hash32 anchor_previous_hash`;
- `uint64 anchor_timestamp`;
- `uint8 anchor_major_version`;
- `uint8 anchor_minor_version`;
- `uint64 cumulative_difficulty`;
- `uint64 already_generated_coins`;
- `uint64 already_generated_transactions`;
- `uint16 source_commit_len`, followed by ASCII/UTF-8 commit bytes;
- `uint32 source_db_schema_version`;
- `uint64 output_count`;
- `uint64 key_image_count`;
- `uint64 consensus_record_count`;
- `uint64 chunk_count`;
- `chunk_count` chunk descriptors;
- `hash32 logical_state_root`.

Chunk descriptor fields:

- `uint8 chunk_type`: `1` outputs, `2` key images, `3` consensus windows;
- `uint32 chunk_index`;
- `uint64 uncompressed_bytes`;
- `uint64 compressed_bytes`;
- `hash32 logical_chunk_hash`;
- `hash32 compressed_chunk_hash`;
- `uint16 path_len`, followed by portable relative path bytes.

Chunk descriptors are ordered by `(chunk_type, chunk_index)`. Duplicate
descriptors are invalid. Paths must be relative, must not contain backslashes,
empty components, `.`, or `..`, and must not be absolute.

### Historical Output Record

Magic: `DGOUT001`

Fields:

- `uint64 amount`;
- `uint32 global_output_index`;
- `public_key32 output_public_key`;
- `hash32 originating_transaction_hash`;
- `uint16 transaction_output_index`;
- `uint64 unlock_time`.

Canonical order is `(amount, global_output_index)`. Duplicates are invalid.

### Spent Key Image Record

Magic: `DGKI0001`

Fields:

- `key_image32 key_image`;
- `uint32 spending_block_height`.

Canonical order is lexicographic byte order of `key_image`. Duplicates are
invalid.

### Consensus Window Record

Magic: `DGCW0001`

Fields:

- `uint32 anchor_height`;
- `uint16 timestamp_count`, followed by `uint64` timestamps;
- `uint16 cumulative_difficulty_count`, followed by `uint64` cumulative
  difficulties;
- `uint16 block_size_count`, followed by `uint64` block sizes;
- `hash32 anchor_raw_block_hash`.

Canonical order is ascending `anchor_height`. Duplicates are invalid.

This v1 record carries the initial rolling windows. Later PRs may add native
database import details, but any additional external record type requires a new
compatible format version and explicit parser limits.

## Hash Construction

Per-record logical bytes are the canonical record bytes above.

Per-chunk logical hash is SHA-256 over the concatenation of the canonical
records in that chunk. Per-chunk compressed hash is SHA-256 over the compressed
chunk file bytes.

The logical state root is SHA-256 over:

```text
"DeroGold consensus snapshot v1 logical root" || 0x00 ||
manifest_without_logical_state_root ||
all_output_records ||
all_key_image_records ||
all_consensus_window_records
```

Compression bytes are not part of logical identity except through
`compressed_hash` verification for transport integrity.

## Parser Limits And Fail-Closed Rules

The v1 parser rejects:

- unsupported magic or version;
- truncated records;
- trailing bytes;
- counts above explicit limits;
- strings or paths above explicit limits;
- integer count/size bombs;
- duplicate chunks;
- duplicate logical records;
- non-canonical record order;
- path traversal or absolute paths;
- unsupported chunk types;
- manifest, record, or window sizes above parser limits.

The parser never silently drops unknown bytes or reorders unordered containers
to manufacture canonical output.

## Compatibility

Readers support format version `1` and `min_reader_version <= 1`. Future
versions must either remain backward-compatible under version `1` rules or use
a new version that older readers reject.

## Golden Vectors

Representative encoded bytes and SHA-256 hashes are committed in
`src/cryptotest/main.cpp`. They cover manifest, output, key-image, and
consensus-window records, deterministic round trips, duplicate/order failures,
truncation, path traversal, oversized counts, and unsupported versions.
