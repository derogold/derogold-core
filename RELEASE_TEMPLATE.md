# Change Logs from v1.0.0

## Daemon
- Changed seed node address.
- Allow upnp to port forward regardless if it is behind a NAT devices.
- Fixed daemon rpc method would fail due to missing transactions.
- Fixed `--db-*` options such that it will no longer get overridden by default values.
- Added `--db-optimize` option to optimize your database for reading.
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

## Wallet
- Removed the `optimize` command, and with it the fusion transactions the wallet used to send.
- Added `sweep` command to sweep an amount to an address, taking the fees out of the amount swept rather than adding them on top.
- Added `sweep_all` command to sweep everything that can be spent to an address.
- Changed `send_all` to sweep, so it no longer needs a balance beyond what is being sent to pay the fee.
- Changed a transfer too large to fit in a block to be offered as several smaller transactions, instead of optimizing the wallet first.

## P2P
- Changed p2p block downloading to dynamic block rate based on system load.

## RocksDB
- Update RocksDB to v11.8.1, now vendored in `external/rocksdb` and compiled with the project rather than downloaded during the build.
- Changed RocksDB default read/write buffer to 256 MB and 64 MB respectively.
- Changed RocksDB logger output and reduced the file history to 1.

## External Dependencies Version

All vendored in `external/` and built with the project, except OpenSSL. There
are no submodules and nothing is downloaded during the build. See
[external/README.md](external/README.md).

- cpp-httplib 0.14.3
- cpp-linenoise (upstream snapshot)
- cryptopp 8.9.0
- cxxopts 3.2.0
- miniupnpc 2.2.8
- nlohmann-json 3.2.0
- openssl (system package)
- rapidjson 1.1.0
- rocksdb 11.8.1
- zstd 1.5.7

# Install Notes

Release binaries are built for Linux. Windows and macOS are supported by the
build itself — see [BUILDING.md](BUILDING.md) — but are not published here.

## For Linux user: (x64)
- (**Debian package installer**) Download `DeroGold-linux-x64-gcc.deb` to install via `sudo apt install ./DeroGold-linux-x64-gcc.deb`.
- Download `DeroGold-linux-x64-gcc.tar.gz` and use `tar -xf DeroGold-linux-x64-gcc.tar.gz` to unzip.

## For Linux user: (arm64 / aarch64 — OrangePi5, Raspberry Pi, etc.)
- (**Debian package installer**) Download `DeroGold-linux-arm64-gcc-cross.deb` to install via `sudo apt install ./DeroGold-linux-arm64-gcc-cross.deb`.
- Download `DeroGold-linux-arm64-gcc-cross.tar.gz` and use `tar -xf DeroGold-linux-arm64-gcc-cross.tar.gz` to unzip.

# Fast Sync Note

New in this release: you can skip downloading the full chain history by using `--sync-from-height`:

```
./DeroGoldd --sync-from-height=2700000
```

See the [README](README.md#fast-sync---sync-from-height) for full details and all supported heights.

# **USAGE WARNING:**
- Pre-released versions are not guaranteed to be stable. Use with caution.
