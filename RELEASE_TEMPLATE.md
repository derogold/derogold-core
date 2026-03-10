# Change Logs from v1.0.0

## Daemon
- Changed seed node address.
- Allow upnp to port forward regardless if it is behind a NAT devices.
- Fixed daemon rpc method would fail due to missing transactions.
- Fixed `--db-*` options such that it will no longer get overridden by default values.
- Added `--db-optimize` option to optimize your database for reading.
- Added `--enable-trtl-rpc` option to enable turtlecoin rpc api.
- Changed `--db-enable-compression` to true by default.
- Changed `status` command behaviour so that it doesn't rely on rpc anymore.
- Added `--sync-from-height=<height>` option to bootstrap a fresh node from a recent checkpoint, skipping the full 350 GB+ historical chain download. Supported floor heights: 1,000,000 / 1,500,000 / 2,000,000 / 2,500,000 / 2,700,000 (recommended).
- Added `export_bootstrap_state <height>` daemon console command to extract anchor data from a fully-synced node for use in `SyncBootstrapCheckpoints.h`.
- Added `--prune` option to enable pruned-node mode, retaining only a recent window of raw block data to reduce disk usage.
- Added `--prune-depth=<blocks>` option to configure how many recent blocks to retain (minimum 2016 ≈ 7 days; default 2016).
- Added `--background-prune` option (default on) to run the prune task periodically in the background.
- Added `prune_status` daemon console command to show prune mode, depth, and current prune floor height.
- Added `compact_db [start|status|wait|stop]` daemon console command to manually manage RocksDB compaction.
- Added `db_status` daemon console command to show database size and file statistics.
- Added automatic adaptive RocksDB compaction scheduler (60s interval during sync, 30min near chain tip).

## P2P
- Changed p2p block downloading to dynamic block rate based on system load.

## RocksDB
- Update RocksDB provider to v9.2.1.
- Changed RocksDB default read/write buffer to 256 MB and 64 MB respectively.
- Changed RocksDB logger output and reduced the file history to 1.

## External Dependencies Version
- boost 1.90.0
- cpp-httplib 0.37.0
- cryptopp 2026-03-02
- cxxopts 3.3.1
- miniupnpc 2.3.2
- openssl 3.6.1
- rapidjson 2025-02-26
- rocksdb 9.10.0

# Install Notes

## For Windows user: (x64)
- Download `DeroGold-windows-x64-msvc.zip` and unzip.

## For Linux user: (x64)
- (**Debian package installer**) Download `DeroGold-linux-x64-gcc.deb` to install via `sudo apt install ./DeroGold-linux-x64-gcc.deb`.
- Download `DeroGold-linux-x64-gcc.tar.gz` and use `tar -xf DeroGold-linux-x64-gcc.tar.gz` to unzip.

## For Linux user: (arm64 / aarch64 — OrangePi5, Raspberry Pi, etc.)
- Download `DeroGold-linux-arm64-gcc.tar.gz` and use `tar -xf DeroGold-linux-arm64-gcc.tar.gz` to unzip.

## For MacOS user: (x64)
- Download `DeroGold-osx-x64-clang.tar.gz` and use `tar -xf DeroGold-osx-x64-clang.tar.gz` to unzip.

# Fast Sync Note

New in this release: you can skip downloading the full chain history by using `--sync-from-height`:

```
./DeroGoldd --sync-from-height=2700000
```

See the [README](README.md#fast-sync---sync-from-height) for full details and all supported heights.

# **USAGE WARNING:**
- Pre-released versions are not guaranteed to be stable. Use with caution.
