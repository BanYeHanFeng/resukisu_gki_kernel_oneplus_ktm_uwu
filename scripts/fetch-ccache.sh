#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

CCACHE_VERSION=${CCACHE_VERSION:-4.14.3}
DEST=${1:-$HOME/toolchains}

log()  { printf '\033[32m[ccache]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

case "${TOOLCHAIN_ARCH:-$(uname -m)}" in
	aarch64|arm64) PKG_ARCH=aarch64 ;;
	x86_64|amd64)  PKG_ARCH=x86_64 ;;
	*) die "不支持的宿主架构：${TOOLCHAIN_ARCH:-$(uname -m)}" ;;
esac

PKG="ccache-${CCACHE_VERSION}-linux-${PKG_ARCH}-musl-static"
URL="https://github.com/ccache/ccache/releases/download/v${CCACHE_VERSION}/${PKG}.tar.xz"
TARBALL="$DEST/${PKG}.tar.xz"
SRC_DIR="$DEST/$PKG"

log "宿主架构：$(uname -m)，ccache 包：${PKG_ARCH}"
mkdir -p "$DEST"

if [ ! -x "$SRC_DIR/ccache" ]; then
	log "下载 ccache ${CCACHE_VERSION}"
	curl -fL --retry 3 -o "$TARBALL.part" "$URL" || die "下载失败：$URL"
	mv -f "$TARBALL.part" "$TARBALL"
	rm -rf "$SRC_DIR"
	tar -xJf "$TARBALL" -C "$DEST"
	[ -x "$SRC_DIR/ccache" ] || die "解压后没有找到 $SRC_DIR/ccache"
	rm -f "$TARBALL"
fi

log "ccache：$("$SRC_DIR/ccache" --version | head -1)"
echo "$SRC_DIR"
