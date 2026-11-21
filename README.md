<div align="center">
  <img src="https://i.imgur.com/4FlvRAt.png" width="180" alt="DeroGold logo">

# DeroGold Core

**The reference node, wallet and mining software for the DeroGold network.**

[![Build DeroGold](https://github.com/derogold/derogold-core/actions/workflows/build-derogold.yml/badge.svg?branch=master)](https://github.com/derogold/derogold-core/actions/workflows/build-derogold.yml)
[![Docker](https://github.com/derogold/derogold-core/actions/workflows/docker.yml/badge.svg?branch=master)](https://github.com/derogold/derogold-core/actions/workflows/docker.yml)
[![Latest release](https://img.shields.io/github/v/release/derogold/derogold-core?display_name=tag&sort=semver)](https://github.com/derogold/derogold-core/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/derogold/derogold-core/total)](https://github.com/derogold/derogold-core/releases)
[![License](https://img.shields.io/github/license/derogold/derogold-core)](LICENSE)

[Website](https://derogold.com/) · [Documentation](https://docs.derogold.online/) · [Releases](https://github.com/derogold/derogold-core/releases) · [Discord](https://discordapp.com/invite/j2aSNFn)
</div>

> [!IMPORTANT]
> **v1.0.2.0 is a mandatory network upgrade.** Fork index 11 activates at block
> **3,000,000** and requires P2P protocol version 11. Older nodes are
> intentionally disconnected. Upgrade before the fork height.

DeroGold Core is the C++ implementation of the DeroGold privacy network. This
repository contains the daemon, command-line wallet, wallet API and standalone
miner used by node operators, pools and integrators.

The current release also changes competing-chain handling. Greater cumulative
work still wins. From block 3,000,000, equal-work branches converge
deterministically by comparing the first differing block after their common
ancestor. See the [v1.0.2.0 release notes](https://github.com/derogold/derogold-core/releases/tag/v1.0.2.0)
for the full upgrade detail.

## Software included

| Binary | Purpose |
|---|---|
| `DeroGoldd` | Full node daemon, P2P service, RPC server and optional built-in Stratum |
| `degwallet` | Interactive command-line wallet |
| `wallet-api` | HTTP wallet service for applications and integrations |
| `miner` | Standalone CPU miner |
| `cryptotest` | Cryptographic and proof-of-work validation utility |

## Get started

### Download a release

Linux x64 packages and archives are published on the
[releases page](https://github.com/derogold/derogold-core/releases/latest).
The build system also supports Linux ARM64, Windows through MSYS2/MinGW and
macOS; see [BUILDING.md](BUILDING.md) when building those targets yourself.

### Run the container

The daemon image is built from `master` and published to GitHub Container
Registry:

```bash
docker pull ghcr.io/derogold/derogold-core:latest

docker run -d \
  --name derogoldd \
  --restart unless-stopped \
  -p 42069:42069 \
  -p 6969:6969 \
  -v derogold-data:/data \
  ghcr.io/derogold/derogold-core:latest \
  --data-dir=/data
```

The named volume keeps the chain database across container replacements. The
P2P port is `42069`; daemon RPC is `6969`. Do not expose RPC to the public
internet without an access-control layer.

### Build from source

The source tree is self-contained apart from OpenSSL. Vendored dependencies,
including RocksDB, Crypto++ and zstd, are built locally. There are no Git
submodules and the build does not download dependencies.

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build git libssl-dev

git clone https://github.com/derogold/derogold-core.git
cd derogold-core
cmake -G Ninja -D CMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The binaries are written to `build/src`. Native builds optimise for the
machine compiling them; add `-D ARCH=default` when portability matters more
than host-specific optimisation.

For CMake presets, cross-compilation, packaging and platform-specific
prerequisites, read [BUILDING.md](BUILDING.md).

## Running a node

### Full node

```bash
./DeroGoldd
```

A full node retains the complete chain and is the correct choice for mining,
public wallet infrastructure and archival operation.

### Pruned node

```bash
./DeroGoldd --prune
```

Pruned mode keeps the chain indexes and a recent window of raw block data. The
default retention depth is 2,016 blocks. It remains useful to wallets and the
network while consuming less disk, but it cannot serve old raw blocks that
have been discarded.

```bash
./DeroGoldd --prune --prune-depth=8640
```

Pruning must be selected on a fresh database or together with `--resync`.
Use `prune_status` and `db_status` in the daemon console to inspect it.

### Lite node

```bash
./DeroGoldd --lite --lite-height=2700000
```

Lite mode stores full block data only from the selected height onward. It can
sync, relay and validate later blocks, but cannot serve or rescan below its
floor. The setting is permanent for that database and cannot be combined with
pruning or explorer mode. Read [LITENODE.md](LITENODE.md) before using it.

### Bootstrap from a checkpoint

The historical chain is more than 350 GB. A fresh non-mining node can start
from a built-in trusted floor:

```bash
./DeroGoldd --sync-from-height=2700000 --enable-mining=false
```

Supported floors are `1000000`, `1500000`, `2000000`, `2500000` and
`2700000`. The daemon deliberately rejects bootstrap mode together with
mining, mining RPC or built-in Stratum. Pools and miners must run a full node.

## Local RPC socket

On POSIX systems the daemon can expose RPC and its console through an AF_UNIX
socket. File permissions provide a cleaner security boundary than a loopback
TCP port on multi-user hosts.

```bash
./DeroGoldd \
  --rpc-ipc-path /run/derogold/daemon.sock \
  --rpc-ipc-mode 0660 \
  --rpc-ipc-group derogold

./degwallet --remote-daemon /run/derogold/daemon.sock
./DeroGoldd --attach /run/derogold/daemon.sock
```

`exit` or `quit` detaches the remote console. `stop` stops the daemon.
Console commands are never exposed over TCP. Windows builds reject these flags
because Windows AF_UNIX sockets do not provide the required ownership and peer
credential guarantees.

## Database operation

DeroGold uses the vendored RocksDB 11.8.1 by default. RocksDB performs its
normal incremental background compaction. The old scheduler that repeatedly
requested full-range compaction has been removed.

Useful daemon console commands:

| Command | Purpose |
|---|---|
| `db_status` | Database size and file statistics |
| `prune_status` | Prune mode, retention depth and current floor |
| `compact_db start` | Start deliberate full-range maintenance |
| `compact_db status` | Inspect a running compaction |
| `compact_db wait` | Wait for it to finish |
| `compact_db stop` | Request cancellation |

Use `--db-optimize` for an operator-requested optimisation pass. Full-range
compaction can be I/O intensive; it is maintenance, not a routine scheduler.

## Development

`master` is the releasable branch. Normal changes are integrated through
`development` and promoted after review and CI.

The active build matrix covers:

- Linux x64 on Ubuntu 22.04 and 24.04
- Linux ARM64 cross-builds
- Windows x64 through MSYS2/MinGW
- the production daemon container

Open an issue before a large behavioural or consensus change. Pull requests
should describe what changed, how it was tested and whether it affects
consensus, P2P compatibility, database format or wallet behaviour.

## Documentation

- [User and operator documentation](https://docs.derogold.online/)
- [Build and packaging guide](BUILDING.md)
- [Lite-node design and limitations](LITENODE.md)
- [Latest release notes](https://github.com/derogold/derogold-core/releases/latest)
- [Technical documentation](docs/)

## Security

Do not publish wallet seeds, private keys, RPC credentials or production
configuration in issues or logs. Treat unauthenticated RPC as trusted-network
only. If a report would expose an exploitable weakness, contact the maintainers
privately before opening a public issue.

## License and lineage

The codebase contains work under several compatible upstream licences. See
[LICENSE](LICENSE) for the exact terms and attribution.

DeroGold builds on work from the CryptoNote, Bytecoin, Monero, Forknote,
TurtleCoin and WRKZCoin projects.
