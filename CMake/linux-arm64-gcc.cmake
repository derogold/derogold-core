set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

    # Debian and Ubuntu install foreign-architecture OpenSSL libraries in the
    # multiarch directory. FindOpenSSL otherwise consults the host pkg-config
    # metadata and can silently select x86_64 libraries for an ARM64 link.
    set(OPENSSL_INCLUDE_DIR /usr/include CACHE PATH "ARM64 OpenSSL headers")
    set(OPENSSL_SSL_LIBRARY /usr/lib/aarch64-linux-gnu/libssl.so CACHE FILEPATH "ARM64 OpenSSL SSL library")
    set(OPENSSL_CRYPTO_LIBRARY /usr/lib/aarch64-linux-gnu/libcrypto.so CACHE FILEPATH "ARM64 OpenSSL crypto library")

    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()
