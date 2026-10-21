#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DIST_DIR=${DIST_DIR:-$REPO_ROOT/dist}
OUT_ZIP=
WITH_DTBO=0
TAG=

AK3_REPO=${AK3_REPO:-https://github.com/osm0sis/AnyKernel3}
AK3_REF=${AK3_REF:-020dfeccf9d7e962a48400fc94d3e451df92eead}

ARCH_TOOLS="magiskboot busybox fec httools_static lptools_static snapshotupdater_static magiskpolicy"
ARCH_TOOLS_URL=${ARCH_TOOLS_URL:-https://raw.githubusercontent.com/Pzqqt/AnyKernel3/e57aad541b78db55663bd011c6a1d8e730df4065/tools}

log()  { printf '\033[32m[ak3]\033[0m %s\n' "$*"; }
warn() { printf '\033[33m[warn]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case "$1" in
		--dist) DIST_DIR=$2; shift 2 ;;
		--out) OUT_ZIP=$2; shift 2 ;;
		--tag) TAG=$2; shift 2 ;;
		--with-dtbo) WITH_DTBO=1; shift ;;
		-h|--help) echo "用法: scripts/pack_ak3.sh [--dist dist] [--out 包名.zip] [--with-dtbo] [--tag 后缀]"; exit 0 ;;
		*) die "未知参数：$1" ;;
	esac
done

[[ -f "$DIST_DIR/Image" ]] || die "找不到 $DIST_DIR/Image，请先运行 scripts/build.sh"

if [[ -z "$OUT_ZIP" ]]; then
	date_tag=$(date -u +%Y%m%d)
	sha_tag=$(git -C "$REPO_ROOT" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
	OUT_ZIP=$REPO_ROOT/dist/uwuAOSP-KTM-${date_tag}-${sha_tag}${TAG:+-$TAG}.zip
fi

WORK_DIR=${WORK_DIR:-$REPO_ROOT/out/ak3-work}
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

fetch_ak3() {
	local dst=$1
	log "取 AnyKernel3 框架（${AK3_REF:0:12}）"
	git init -q "$dst"
	git -C "$dst" remote add origin "$AK3_REPO"
	git -C "$dst" fetch -q --depth 1 origin "$AK3_REF"
	git -C "$dst" checkout -q FETCH_HEAD
	rm -rf "$dst/.git" "$dst/.github" "$dst/README.md" "$dst/LICENSE"
	rm -rf "$dst/patch" "$dst/ramdisk" "$dst/modules"
}

fetch_arch_tools() {
	local dst=$1 tool
	mkdir -p "$dst"
	for tool in $ARCH_TOOLS; do
		if curl -fsSL -o "$dst/$tool" "$ARCH_TOOLS_URL/$tool"; then
			chmod +x "$dst/$tool"
		else
			warn "下载 aarch64 工具失败：$tool（模板里的 32 位版本在 64 位-only 设备上跑不起来）"
			return 1
		fi
	done
}

main() {
	local stage=$WORK_DIR/stage
	fetch_ak3 "$stage"
	fetch_arch_tools "$stage/tools" || die "换成 aarch64 版安装工具失败"

	log "放入内核 Image"
	cp -f "$DIST_DIR/Image" "$stage/Image"

	log "放入安装脚本"
	cp -f "$REPO_ROOT/anykernel/anykernel.sh" "$stage/anykernel.sh"

	if [[ "$WITH_DTBO" == "1" ]]; then
		[[ -f "$DIST_DIR/dtbo.img" ]] || die "--with-dtbo 但 $DIST_DIR/dtbo.img 不存在"
		log "放入 dtbo.img（安装时会刷写 dtbo 分区）"
		cp -f "$DIST_DIR/dtbo.img" "$stage/dtbo.img"
	fi

	[[ -f "$DIST_DIR/kernel-info.txt" ]] && cp -f "$DIST_DIR/kernel-info.txt" "$stage/kernel-info.txt"

	log "打包：${OUT_ZIP#$REPO_ROOT/}"
	mkdir -p "$(dirname "$OUT_ZIP")"
	(cd "$stage" && zip -rq9 "$OUT_ZIP" . -x '.*')
	log "完成：$(du -h "$OUT_ZIP" | cut -f1)，只含内核$([[ "$WITH_DTBO" == "1" ]] && echo ' 与 dtbo.img')"
}

main "$@"
