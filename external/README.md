# Vendored dependencies

Everything DeroGold builds against, apart from OpenSSL, lives here. There is no
package manager and no submodules: a clone has all the sources, and nothing is
downloaded during the build.

| Directory | Version | Built how |
| --- | --- | --- |
| `cpp-linenoise/` | unversioned upstream snapshot | Header only |
| `cryptopp/` | 8.9.0 | Compiled with the project |
| `cxxopts.hpp` | 3.2.0 | Header only |
| `httplib.h` | 0.14.3 | Header only |
| `miniupnpc/` | 2.2.8 | Compiled with the project |
| `nlohmann-json/` | 3.2.0 | Header only |
| `rapidjson/` | 1.1.0 | Header only |
| `rocksdb/` | 11.8.1 | Compiled with the project |
| `zstd/` | 1.5.7 | Compiled with the project, RocksDB needs it |

Versions come from each tree's own version header, so they can be re-checked
with a grep rather than trusted from this table:
`config_ver.h`, `VERSION`, `zstd.h`, `rocksdb/version.h`, `json.hpp`,
`rapidjson.h`, `cxxopts.hpp`, `httplib.h`.

## Local modifications

Anything changed relative to upstream belongs in this list. Keep it accurate:
without it, the next version bump silently drops the change.

### nlohmann-json 3.2.0

`json.hpp`, in `serializer::dump_integer`:

```diff
-        const bool is_negative = (x <= 0) and (x != 0);  // see issue #755
+        const bool is_negative = !(x>=0);
```

Behaviour is unchanged. `dump_integer` returns early when `x == 0`, so over the
values that reach this line the two expressions agree. The rewrite avoids
comparing an unsigned `NumberType` against zero in a way that draws a
tautological-comparison warning when the template is instantiated for
`number_unsigned_t`.

## Re-vendoring

Replace the tree with the upstream release, then re-apply everything under
"Local modifications" above and update the version in the table. RocksDB is the
one that needs care: it is a trimmed copy, so compare against the previous
tree rather than dropping an upstream tarball in whole.

Note that parts of `rocksdb/tools/` and `rocksdb/db_stress_tool/` are listed in
RocksDB's core library sources even though `WITH_TOOLS`, `WITH_CORE_TOOLS` and
`WITH_TESTS` are all forced off in `CMake/DeroGoldDependencies.cmake`. They
cannot be deleted to save space without editing RocksDB's `CMakeLists.txt`.
