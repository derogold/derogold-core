# Dependency resolution for DeroGold.
#
# Each function below produces the imported target that src/CMakeLists.txt
# links against, so the link lines do not care where a library came from.
#
# Everything here uses plain CMake and system packages. There is deliberately
# no package manager and nothing is downloaded during the build: everything
# that is not a plain system package lives under external/ and is compiled with
# the project.

include(FindPackageHandleStandardArgs)

# Version of the RocksDB copy checked into external/rocksdb. Informational;
# change it only when that tree is replaced.
set(DEROGOLD_ROCKSDB_VERSION "11.8.1")

# Oldest RocksDB accepted when using a system install. RocksDBWrapper calls
# WaitForCompact(WaitForCompactOptions), which was added in 8.1.
set(DEROGOLD_ROCKSDB_MINIMUM "8.1")

# Emit a consistent "install this" message and stop.
function(_derogold_missing_dependency name debian fedora arch brew)
    message(FATAL_ERROR
        "Could not find ${name}.\n"
        "Install it with one of:\n"
        "  Debian/Ubuntu: sudo apt install ${debian}\n"
        "  Fedora/RHEL:   sudo dnf install ${fedora}\n"
        "  Arch:          sudo pacman -S ${arch}\n"
        "  macOS:         brew install ${brew}\n"
        "See BUILDING.md for the full list.")
endfunction()

function(derogold_require_openssl)
    # Shipped with CMake, so nothing to shim.
    find_package(OpenSSL QUIET)

    if(NOT OpenSSL_FOUND)
        _derogold_missing_dependency("OpenSSL"
            "libssl-dev" "openssl-devel" "openssl" "openssl@3")
    endif()

    message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
endfunction()

# Crypto++ and miniupnpc are built from the copies checked into external/.
# Both are small and neither is reliably packaged across the platforms this
# project targets, so vendoring them removes two install steps and any question
# of which version is present. Nothing here consults the system for them.
function(derogold_add_cryptopp)
    if(TARGET cryptopp::cryptopp)
        return()
    endif()

    set(_src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../external/cryptopp")

    if(NOT EXISTS "${_src}/CMakeLists.txt")
        message(FATAL_ERROR "external/cryptopp is missing from the checkout.")
    endif()

    # Static only, and none of the extras. These option names are prefixed in
    # the vendored CMakeLists precisely so setting them here cannot disturb the
    # enclosing project.
    set(CRYPTOPP_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(CRYPTOPP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(CRYPTOPP_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CRYPTOPP_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)

    add_subdirectory("${_src}" "${CMAKE_BINARY_DIR}/external/cryptopp" EXCLUDE_FROM_ALL)

    if(NOT TARGET cryptopp-static)
        message(FATAL_ERROR "The vendored Crypto++ did not produce a static library target.")
    endif()

    add_library(cryptopp::cryptopp ALIAS cryptopp-static)

    # DeroGold includes <cryptopp/sha.h>, so the headers have to be reachable
    # under a cryptopp/ prefix. They sit flat in the source tree, so mirror
    # them into the build tree once and expose that.
    set(_compat "${CMAKE_BINARY_DIR}/external/include")

    file(GLOB _cryptopp_headers "${_src}/*.h")
    file(COPY ${_cryptopp_headers} DESTINATION "${_compat}/cryptopp")

    target_include_directories(cryptopp-static INTERFACE "$<BUILD_INTERFACE:${_compat}>")

    message(STATUS "Crypto++: building the copy in external/cryptopp")
endfunction()

function(derogold_add_miniupnpc)
    if(TARGET miniupnpc::miniupnpc)
        return()
    endif()

    set(_src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../external/miniupnpc")

    if(NOT EXISTS "${_src}/CMakeLists.txt")
        message(FATAL_ERROR "external/miniupnpc is missing from the checkout.")
    endif()

    set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
    set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)

    add_subdirectory("${_src}" "${CMAKE_BINARY_DIR}/external/miniupnpc" EXCLUDE_FROM_ALL)

    if(NOT TARGET libminiupnpc-static)
        message(FATAL_ERROR "The vendored miniupnpc did not produce a static library target.")
    endif()

    # Upstream already aliases miniupnpc::miniupnpc when only the static
    # library is built; define it ourselves if that ever changes.
    if(NOT TARGET miniupnpc::miniupnpc)
        add_library(miniupnpc::miniupnpc ALIAS libminiupnpc-static)
    endif()

    # Same prefix problem as Crypto++: DeroGold includes
    # <miniupnpc/miniupnpc.h> because that is how the headers are installed
    # system-wide, but in the source tree they are flat under include/.
    set(_compat "${CMAKE_BINARY_DIR}/external/include")

    file(GLOB _miniupnpc_headers "${_src}/include/*.h")
    file(COPY ${_miniupnpc_headers} DESTINATION "${_compat}/miniupnpc")

    target_include_directories(libminiupnpc-static INTERFACE "$<BUILD_INTERFACE:${_compat}>")

    message(STATUS "miniupnpc: building the copy in external/miniupnpc")
endfunction()

# zstd is built from the copy in external/zstd. RocksDB needs it because the
# blockchain wrapper writes with kZSTD when compression is on, so an existing
# database cannot be opened without it.
#
# This has to run before RocksDB is added. RocksDB looks for zstd with its own
# find module, which searches the system and fails hard when WITH_ZSTD is on,
# so the results it expects are published here in advance.
function(derogold_add_zstd)
    if(TARGET zstd::zstd)
        return()
    endif()

    set(_src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../external/zstd")

    if(NOT EXISTS "${_src}/build/cmake/CMakeLists.txt")
        message(FATAL_ERROR "external/zstd is missing from the checkout.")
    endif()

    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

    add_subdirectory("${_src}/build/cmake" "${CMAKE_BINARY_DIR}/external/zstd" EXCLUDE_FROM_ALL)

    if(NOT TARGET libzstd_static)
        message(FATAL_ERROR "The vendored zstd did not produce a static library target.")
    endif()

    add_library(zstd::zstd ALIAS libzstd_static)

    # What RocksDB's Findzstd module produces. Setting these in the cache stops
    # its find_path and find_library searching the system at all, and lets
    # find_package_handle_standard_args succeed. Its own
    # "if(ZSTD_FOUND AND NOT TARGET zstd::zstd)" then leaves the alias above
    # alone rather than declaring an imported target over the top.
    set(ZSTD_FOUND TRUE CACHE BOOL "" FORCE)
    set(ZSTD_INCLUDE_DIRS "${_src}/lib" CACHE PATH "" FORCE)
    set(ZSTD_LIBRARIES libzstd_static CACHE STRING "" FORCE)

    message(STATUS "zstd: building the copy in external/zstd")
endfunction()

function(derogold_require_rocksdb)
    if(DEROGOLD_SYSTEM_ROCKSDB)
        find_package(RocksDB CONFIG QUIET)

        if(NOT TARGET RocksDB::rocksdb)
            _derogold_missing_dependency("RocksDB ${DEROGOLD_ROCKSDB_MINIMUM} or newer"
                "librocksdb-dev" "rocksdb-devel" "rocksdb" "rocksdb")
        endif()

        if(RocksDB_VERSION AND RocksDB_VERSION VERSION_LESS DEROGOLD_ROCKSDB_MINIMUM)
            message(FATAL_ERROR
                "Found RocksDB ${RocksDB_VERSION}, but this code needs "
                "${DEROGOLD_ROCKSDB_MINIMUM} or newer. Re-run without "
                "-D DEROGOLD_SYSTEM_ROCKSDB=ON to build the "
                "${DEROGOLD_ROCKSDB_VERSION} copy in external/rocksdb instead.")
        endif()

        message(STATUS "RocksDB: system install ${RocksDB_VERSION}")
        return()
    endif()

    message(STATUS "RocksDB: building the ${DEROGOLD_ROCKSDB_VERSION} copy in external/rocksdb")

    # zstd first, so its target and the variables RocksDB's find module looks
    # for already exist by the time RocksDB is added.
    derogold_add_zstd()

    # Compression: the wrapper writes with kZSTD when compression is enabled,
    # so an existing database cannot be opened without zstd support. Keep the
    # rest off; nothing here uses them.
    set(WITH_ZSTD ON CACHE BOOL "" FORCE)
    set(WITH_SNAPPY OFF CACHE BOOL "" FORCE)
    set(WITH_LZ4 OFF CACHE BOOL "" FORCE)
    set(WITH_BZ2 OFF CACHE BOOL "" FORCE)
    set(WITH_ZLIB OFF CACHE BOOL "" FORCE)

    # Trim everything we do not link against; this is most of the build time.
    set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_ALL_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_BENCHMARK_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_CORE_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

    # Static, and portable rather than tuned for the building machine, so a
    # binary built on one host still runs on another.
    set(ROCKSDB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(PORTABLE ON CACHE STRING "" FORCE)
    set(USE_RTTI ON CACHE BOOL "" FORCE)
    set(FAIL_ON_WARNINGS OFF CACHE BOOL "" FORCE)

    # RocksDB 10 and newer declare C++20 as their own default. Let it have
    # that rather than forcing this project's C++17 on it, which is not a
    # combination upstream builds or tests. This assignment is scoped to this
    # function, so it reaches the RocksDB subdirectory and nothing else; the
    # two standards share an ABI under the same compiler, and only RocksDB's
    # own API crosses the boundary.
    #
    # This is what raises the compiler floor. Say so here rather than letting
    # an old compiler fail hundreds of lines into a RocksDB header, which is
    # what used to happen: GCC 9 has no defaulted comparison operators, so it
    # stops on `auto operator==(...) const = default` with nothing to suggest
    # the compiler is the problem.
    #
    # GCC 11 and Clang 14, matching what BUILDING.md has always said. The
    # binding constraint is `using enum SizeApproximationFlags;` in RocksDB's
    # db.h - that C++20 feature (P1099R5) arrived in GCC 11 and Clang 13, so
    # GCC 10 gets most of the way through the header and then stops on
    # "expected nested-name-specifier before 'enum'", which says nothing about
    # the compiler being the problem.
    #
    # Ubuntu 20.04 tops out at gcc-10 in its own repositories, so reaching this
    # floor there needs ppa:ubuntu-toolchain-r/test. That is worth doing,
    # because 20.04 is also how you get a binary that runs on an older glibc.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
        message(FATAL_ERROR
            "GCC ${CMAKE_CXX_COMPILER_VERSION} cannot build the bundled RocksDB, which "
            "compiles as C++20 and uses `using enum` - that needs GCC 11 or newer.\n"
            "On Ubuntu 20.04, whose newest own gcc is 10:\n"
            "  sudo add-apt-repository ppa:ubuntu-toolchain-r/test\n"
            "  sudo apt install g++-11 gcc-11\n"
            "then configure with CC=gcc-11 CXX=g++-11.\n"
            "Alternatively pass -D DEROGOLD_SYSTEM_ROCKSDB=ON to link a system "
            "RocksDB 8.1 or newer instead. See BUILDING.md.")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        message(FATAL_ERROR
            "Clang ${CMAKE_CXX_COMPILER_VERSION} is below the supported floor for the "
            "bundled RocksDB, which compiles as C++20 and needs Clang 14 or newer.\n"
            "Alternatively pass -D DEROGOLD_SYSTEM_ROCKSDB=ON to link a system "
            "RocksDB 8.1 or newer instead. See BUILDING.md.")
    endif()

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    set(_src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../external/rocksdb")

    if(NOT EXISTS "${_src}/CMakeLists.txt")
        message(FATAL_ERROR "external/rocksdb is missing from the checkout.")
    endif()

    add_subdirectory("${_src}" "${CMAKE_BINARY_DIR}/external/rocksdb" EXCLUDE_FROM_ALL)

    # RocksDB's own CMake exports "rocksdb"; the rest of this project links the
    # namespaced name, so bridge the two.
    if(TARGET rocksdb AND NOT TARGET RocksDB::rocksdb)
        add_library(RocksDB::rocksdb ALIAS rocksdb)
    endif()

    if(NOT TARGET RocksDB::rocksdb)
        message(FATAL_ERROR "The vendored RocksDB did not define a usable target.")
    endif()

    # No target_include_directories here on purpose. RocksDB already exposes
    # its own include directory, correctly wrapped:
    #
    #   target_include_directories(rocksdb PUBLIC
    #     $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
    #
    # Adding a bare source path on top broke generation, because the target is
    # part of an install(EXPORT) and CMake refuses a source-tree path in
    # INTERFACE_INCLUDE_DIRECTORIES that is not guarded by BUILD_INTERFACE.

    # RocksDB 10 and newer use defaulted comparison operators in their *public*
    # headers, so it is not only RocksDB's own sources that need C++20: every
    # file of ours that includes one does too. Requiring it on the interface
    # raises those translation units to C++20 and leaves the rest of the
    # project on C++17.
    target_compile_features(rocksdb INTERFACE cxx_std_20)
endfunction()
