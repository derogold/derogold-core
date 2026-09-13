# syntax=docker/dockerfile:1

ARG UBUNTU_VERSION=22.04
ARG CCACHE_VERSION=4.10.2
ARG NVM_VERSION=0.40.1

##################################################
# Default Build Environment
##################################################

FROM --platform=${BUILDPLATFORM} ubuntu:${UBUNTU_VERSION} AS dev_env_default
ARG DEBIAN_FRONTEND=noninteractive

ARG BUILDPLATFORM

ARG UBUNTU_VERSION
ARG CCACHE_VERSION
ARG NVM_VERSION

ARG CMAKE_APT_PACKAGE="ca-certificates curl gpg"
ARG VCS_PACKAGE="git gpg"
ARG DEV_PACKAGE="cmake ninja-build"
# Nothing is downloaded during the build; these are for the CMake apt key
# fetch above and for pkg-config lookups.
ARG FETCH_PACKAGE="curl ca-certificates pkg-config"
ARG LIB_PACKAGE="libssl-dev"

ARG AMD64_GCC_PACKAGE="build-essential crossbuild-essential-arm64"
ARG ARM64_GCC_PACKAGE="build-essential crossbuild-essential-amd64"

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install --no-install-recommends --no-install-suggests -y ${CMAKE_APT_PACKAGE} && \
    curl -o- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - > /usr/share/keyrings/kitware-archive-keyring.gpg && \
    [ -s /etc/os-release ] && . /etc/os-release && \
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ ${UBUNTU_CODENAME} main" > /etc/apt/sources.list.d/kitware.list && \
    if [ "${BUILDPLATFORM}" = "linux/amd64" ]; then \
        apt-get update && apt-get install --no-install-recommends --no-install-suggests -y ${VCS_PACKAGE} ${DEV_PACKAGE} ${FETCH_PACKAGE} ${LIB_PACKAGE} ${AMD64_GCC_PACKAGE}; \
    elif [ "${BUILDPLATFORM}" = "linux/arm64" ]; then \
        apt-get update && apt-get install --no-install-recommends --no-install-suggests -y ${VCS_PACKAGE} ${DEV_PACKAGE} ${FETCH_PACKAGE} ${LIB_PACKAGE} ${ARM64_GCC_PACKAGE}; \
    fi

RUN git clone --branch v${CCACHE_VERSION} --depth 1 --recursive https://github.com/ccache/ccache.git /usr/local/src/ccache && \
    cd /usr/local/src/ccache && \
    cmake -D CMAKE_BUILD_TYPE=Release -S . -B build && cmake --build build -t install -j $(nproc) && \
    rm -r /usr/local/src/ccache

RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v${NVM_VERSION}/install.sh | bash && \
    export NVM_DIR="$HOME/.nvm" && [ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh" && \
    nvm install 22
