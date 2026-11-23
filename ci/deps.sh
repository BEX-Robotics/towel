#!/usr/bin/env bash
# Builds Towel's two non-Qt dependencies as static libraries into a prefix:
#   ci/deps.sh <prefix>
# libgit2 (reads repositories; no network code, Towel shells out to git for that)
# and KDE's syntax-highlighting (with extra-cmake-modules, which it needs to build).
# Works on Linux, macOS and Windows (Git Bash inside an MSVC environment).
set -euo pipefail
PREFIX=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
WORK=${WORK:-$PWD/_deps}
mkdir -p "$PREFIX" "$WORK"

build() {
  local name=$1 url=$2 tag=$3
  shift 3
  [ -d "$WORK/$name" ] || git clone --quiet --depth 1 -b "$tag" "$url" "$WORK/$name"
  cmake -S "$WORK/$name" -B "$WORK/$name-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON "$@"
  cmake --build "$WORK/$name-build"
  cmake --install "$WORK/$name-build"
}

build libgit2 https://github.com/libgit2/libgit2.git v1.9.1 \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_CLI=OFF -DBUILD_EXAMPLES=OFF \
  -DUSE_SSH=OFF -DUSE_HTTPS=OFF -DUSE_NTLMCLIENT=OFF -DUSE_GSSAPI=OFF \
  -DREGEX_BACKEND=builtin -DUSE_BUNDLED_ZLIB=ON -DSTATIC_CRT=OFF

build ecm https://github.com/KDE/extra-cmake-modules.git v6.24.0 \
  -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF

build syntax-highlighting https://github.com/KDE/syntax-highlighting.git v6.24.0 \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBUILD_QCH=OFF -DQRC_SYNTAX=ON
