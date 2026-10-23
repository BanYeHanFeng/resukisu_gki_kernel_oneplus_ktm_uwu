#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

LLVM_VERSION=${LLVM_VERSION:-23.1.4}
DEST=${1:-$HOME/toolchains}

log()  { printf '\033[32m[toolchain]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

case "${TOOLCHAIN_ARCH:-$(uname -m)}" in
	aarch64|arm64) PKG_ARCH=aarch64 ;;
	x86_64|amd64)  PKG_ARCH=x86_64 ;;
	*) die "不支持的宿主架构：${TOOLCHAIN_ARCH:-$(uname -m)}" ;;
esac

URL="https://mirrors.edge.kernel.org/pub/tools/llvm/files/llvm-${LLVM_VERSION}-${PKG_ARCH}.tar.xz"
TARBALL="$DEST/llvm-${LLVM_VERSION}-${PKG_ARCH}.tar.xz"
SRC_DIR="$DEST/llvm-${LLVM_VERSION}-${PKG_ARCH}"

log "宿主架构：$(uname -m)，工具链包：${PKG_ARCH}"
mkdir -p "$DEST"

if [ ! -x "$SRC_DIR/bin/clang" ]; then
	log "下载 LLVM ${LLVM_VERSION}"
	if [ ! -f "$TARBALL" ]; then
		curl -fL --retry 3 -o "$TARBALL.part" "$URL" || die "下载失败：$URL"
		mv -f "$TARBALL.part" "$TARBALL"
	fi
	log "解压到 $SRC_DIR"
	rm -rf "$SRC_DIR"
	tar -xJf "$TARBALL" -C "$DEST"
	[ -x "$SRC_DIR/bin/clang" ] || die "解压后没有找到 $SRC_DIR/bin/clang"
	rm -f "$TARBALL"
fi

log "clang：$("$SRC_DIR/bin/clang" --version | head -1)"
echo "$SRC_DIR/bin"
