#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

LLVM_VERSION=${LLVM_VERSION:-23.1.4}
DEST=${1:-$HOME/toolchains}
URL="https://mirrors.edge.kernel.org/pub/tools/llvm/files/llvm-${LLVM_VERSION}-x86_64.tar.xz"
TARBALL="$DEST/llvm-${LLVM_VERSION}-x86_64.tar.xz"
SRC_DIR="$DEST/llvm-${LLVM_VERSION}-x86_64"

log()  { printf '\033[32m[toolchain]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

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
