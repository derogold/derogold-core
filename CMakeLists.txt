cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
    cmake_policy(SET CMP0167 NEW)
endif()

message(CHECK_START "Finding CMAKE_SOURCE_DIR")
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" CMAKE_SOURCE_DIR_NORMAL)
message(CHECK_PASS "${CMAKE_SOURCE_DIR_NORMAL}")

message(CHECK_START "Finding CMAKE_BINARY_DIR")
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" CMAKE_BINARY_DIR_NORMAL)

if("${CMAKE_SOURCE_DIR_NORMAL}" STREQUAL "${CMAKE_BINARY_DIR_NORMAL}")
    message(CHECK_FAIL "${CMAKE_BINARY_DIR_NORMAL}")
    message(FATAL_ERROR "CMAKE_BINARY_DIR cannot be the same as CMAKE_SOURCE_DIR.")
else()
    message(CHECK_PASS "${CMAKE_BINARY_DIR_NORMAL}")
endif()

message(CHECK_START "Finding vcpkg installation")

if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/vcpkg")
elseif(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
else()
    message(WARNING "vcpkg installation is missing from your source. Did you forget to clone with --recursive flag?")

    find_package(Git QUIET)

    if(Git_FOUND)
        if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
            execute_process(COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE GIT_SUBMODULE_INIT_RESULT)

            if(GIT_SUBMODULE_INIT_RESULT)
                message(CHECK_FAIL "not found")
                message(FATAL_ERROR "Unable to initialize vcpkg submodule.")
            endif()
        else()
            execute_process(COMMAND "${GIT_EXECUTABLE}" clone https://github.com/microsoft/vcpkg WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE GIT_CLONE_RESULT)

            if(GIT_CLONE_RESULT)
                message(CHECK_FAIL "not found")
                message(FATAL_ERROR "Unable to initialize vcpkg submodule.")
            endif()
        endif()
    else()
        message(CHECK_FAIL "not found")
        message(FATAL_ERROR "Git not found. Unable to initialize vcpkg submodule.")
    endif()

    set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/vcpkg")
endif()

message(CHECK_PASS "${VCPKG_ROOT}")

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    file(REAL_PATH ${CMAKE_TOOLCHAIN_FILE} CMAKE_TOOLCHAIN_FILE_ABSOLUTE)
    file(TO_CMAKE_PATH ${CMAKE_TOOLCHAIN_FILE_ABSOLUTE} CMAKE_TOOLCHAIN_FILE_NORMAL)
    
    if (NOT CMAKE_TOOLCHAIN_FILE_NORMAL STREQUAL "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        message(WARNING "You are trying to override CMAKE_TOOLCHAIN_FILE which is required for vcpkg. Set VCPKG_CHAINLOAD_TOOLCHAIN_FILE instead.")
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE_ABSOLUTE}")
    endif()
endif()

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

set(SUPPORTED_VCPKG_TARGET_TRIPLET
    "x64-windows-static"
    "x64-windows-static-release"
    "x64-mingw-static"
    "x64-mingw-static-release"
    "x64-linux"
    "x64-linux-clang"
    "x64-linux-release"
    "x64-linux-release-clang"
    "arm64-linux"
    "arm64-linux-clang"
    "arm64-linux-release"
    "arm64-linux-release-clang"
    "x64-osx"
    "x64-osx-release")

message(CHECK_START "Finding VCPKG_TARGET_TRIPLET")

if(NOT DEFINED VCPKG_TARGET_TRIPLET AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(CHECK_FAIL "not found")
    message(FATAL_ERROR "Please include -D VCPKG_TARGET_TRIPLET=x64-windows-static-release (MSVC) or -D VCPKG_TARGET_TRIPLET=x64-mingw-static-release (MINGW64/CLANG64) in your CMake command to continue.")
elseif(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET IN_LIST SUPPORTED_VCPKG_TARGET_TRIPLET)
    message(CHECK_FAIL "${VCPKG_TARGET_TRIPLET}")
    list(JOIN SUPPORTED_VCPKG_TARGET_TRIPLET "\n" SUPPORTED_VCPKG_TARGET_TRIPLET_STRING)
    message(FATAL_ERROR "Invalid VCPKG_TARGET_TRIPLET selected.\nSupported Triplet(s):\n${SUPPORTED_VCPKG_TARGET_TRIPLET_STRING}")
else()
    message(CHECK_PASS "${VCPKG_TARGET_TRIPLET}")
endif()

# Set vcpkg toolchain options
set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/ports")
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_SOURCE_DIR}/triplets")
set(VCPKG_INSTALL_OPTIONS "--clean-after-build")

project(DeroGold VERSION 0.7.2.4 HOMEPAGE_URL https://derogold.com LANGUAGES C CXX ASM)

include(CTest)

# Enable c11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}")
endif()

set(USE_CCACHE ON CACHE BOOL "Use ccache to speed up incremental compilation.")

if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)

    if(CCACHE_PROGRAM)
        message(STATUS "Found ccache: ${CCACHE_PROGRAM}")

        if(MSVC)
            file(COPY_FILE ${CCACHE_PROGRAM} ${CMAKE_BINARY_DIR}/cl.exe ONLY_IF_DIFFERENT)

            # By default Visual Studio generators will use /Zi which is not compatible
            # with ccache, so tell Visual Studio to use /Z7 instead.
            if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
                message(STATUS "Setting MSVC debug information format to 'Embedded'")
                set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
            endif()

            set(CMAKE_VS_GLOBALS
                "CLToolExe=cl.exe"
                "CLToolPath=${CMAKE_BINARY_DIR}"
                "UseMultiToolTask=true"
                "DebugInformationFormat=OldStyle")
        else()
            set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        endif()
    endif()
endif()

# Set default build type.
set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug, Release, RelWithDebInfo")

# Set target architecture for optimizations
set(ARCH native CACHE STRING "Set target architecture, options are: native, default")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if(NOT ARCH STREQUAL "default")
        add_compile_options(-march=${ARCH})
    endif()
endif()

# This section is for settings found in the slow-hash routine(s) that may benefit some systems (mostly ARM)
set(FORCE_USE_HEAP ON CACHE BOOL "Force the use of heap memory allocation")
set(NO_AES OFF CACHE BOOL "Turn off Hardware AES instructions?")
set(NO_OPTIMIZED_MULTIPLY_ON_ARM OFF CACHE BOOL "Turn off Optimized Multiplication on ARM?")

if(FORCE_USE_HEAP)
    add_compile_definitions(FORCE_USE_HEAP)
    message(STATUS "FORCE_USE_HEAP: ENABLED")
else()
    message(STATUS "FORCE_USE_HEAP: DISABLED")
endif()

if(NO_AES)
    add_compile_definitions(NO_AES)
    message(STATUS "HW AES: DISABLED")
else()
    message(STATUS "HW AES: ENABLED")

    check_c_compiler_flag(-maes HAS_AES_NI_COMPILER_FLAG)

    if(HAS_AES_NI_COMPILER_FLAG)
        string(APPEND CMAKE_C_FLAGS " -maes")
    endif()
endif()

if(NO_OPTIMIZED_MULTIPLY_ON_ARM)
    add_compile_definitions(NO_OPTIMIZED_MULTIPLY_ON_ARM)
    message(STATUS "OPTIMIZED_ARM_MULTIPLICATION: DISABLED")
else()
    message(STATUS "OPTIMIZED_ARM_MULTIPLICATION: ENABLED")
endif()

set(SET_PACKAGE_OUTPUT_SUFFIX "${CPACK_SYSTEM_NAME}" CACHE STRING "Set the CPack package file name suffix")

# This section helps us tag our builds with the git commit information
set(SET_COMMIT_ID_IN_VERSION ON CACHE BOOL "Include commit ID in version")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/version")

if(NOT SET_COMMIT_ID_IN_VERSION)
    unset(BUILD_COMMIT_ID)
elseif(NOT DEFINED BUILD_COMMIT_ID)
    if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
        find_package(Git QUIET)

        if(Git_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                RESULT_VARIABLE BUILD_COMMIT_ID_RESULT
                OUTPUT_VARIABLE BUILD_COMMIT_ID
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
    endif()
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/config/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version/version.h")
add_custom_target(version ALL)

include_directories("${CMAKE_CURRENT_BINARY_DIR}/version" "external" "include" "src")

# Platform specific code base information is applied here
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
    add_compile_options(/bigobj /MP /W3 /GS-)
    add_link_options(/STACK:10485760)

    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/msc)
elseif(MINGW)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
    add_link_options(-static)

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mingw)
elseif(APPLE)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/osx)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/posix)
else()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/posix)
endif()

if(NOT MSVC)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        # This option has no effect in glibc version less than 2.20.
        # Since glibc 2.20 _BSD_SOURCE is deprecated, this macro is recomended instead
        add_compile_definitions(_DEFAULT_SOURCE _GNU_SOURCE)
    endif()

    # # These options generate all those nice warnings we see while building
    set(WARNINGS "-Wall -Wextra -Wpointer-arith -Wvla -Wwrite-strings -Wno-error=extra -Wno-error=unused-function -Wno-error=sign-compare -Wno-error=strict-aliasing -Wno-error=type-limits -Wno-unused-parameter -Wno-error=unused-variable -Wno-error=undef -Wno-error=unused-result")

    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(WARNINGS "${WARNINGS} -Wno-error=mismatched-tags -Wno-error=null-conversion -Wno-overloaded-shift-op-parentheses -Wno-error=shift-count-overflow -Wno-error=tautological-constant-out-of-range-compare -Wno-error=unused-private-field -Wno-error=unneeded-internal-declaration -Wno-error=unused-function -Wno-error=missing-braces -Wno-error=unused-command-line-argument -Wno-error=integer-overflow")

        check_cxx_compiler_flag(-Wno-nan-infinity-disabled HAS_NO_NAN_INFINITY_DISABLED)

        if(HAS_NO_NAN_INFINITY_DISABLED)
            set(WARNINGS "${WARNINGS} -Wno-nan-infinity-disabled")
        endif()
    else()
        set(WARNINGS "${WARNINGS} -Wno-error=unused-but-set-variable -Wno-error=odr")

        if(NOT APPLE)
            set(WARNINGS "${WARNINGS} -Wlogical-op -Wno-error=maybe-uninitialized -Wno-error=clobbered")
        endif()
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNINGS} -Waggregate-return -Wnested-externs -Wold-style-definition -Wstrict-prototypes -Wno-implicit-fallthrough")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNINGS} -Wno-reorder -Wno-missing-field-initializers")

    check_cxx_compiler_flag(-Wno-error=inconsistent-missing-override HAS_INCONSISTENT_MISSING_OVERRIDE)

    if(HAS_INCONSISTENT_MISSING_OVERRIDE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=inconsistent-missing-override")
    endif()

    if(APPLE)
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            # Need to build against libc++ instead of libstc++ on apple
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdinc++ -nodefaultlibs -isystem /usr/local/opt/llvm/include/c++/v1 -L /usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib -lc++ -lc++abi -lm -lc -lgcc_s -lgcc")
        elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            # Need to link against the llvm libc++ library, default is too old for std::filesystem
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -I/usr/local/opt/llvm/include/c++/v1")
        endif()
    endif()

    if(NOT APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    endif()

    # # Setting up DEBUG flags
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(DEBUG_FLAGS "-g3 -Og -gdwarf-4 -fvar-tracking -fvar-tracking-assignments -fno-inline -fno-omit-frame-pointer")
    else()
        set(DEBUG_FLAGS "-g3 -O0 -fno-omit-frame-pointer")
    endif()

    # # Setting up RELEASE flags
    set(RELEASE_FLAGS "-O3 -DNDEBUG")

    # # Set up the normal CMake flags as we've built them
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEBUG_FLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${RELEASE_FLAGS}")

    # # Statically link our binaries
    if(NOT APPLE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif()
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS serialization)
find_package(httplib CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(miniupnpc CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)

# So we can use std::string with rapidjson
add_compile_definitions(RAPIDJSON_HAS_STDSTRING)

add_subdirectory(src)
