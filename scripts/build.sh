#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
KERNEL_DIR="$REPO_ROOT/kernel"
DTS_SRC="$REPO_ROOT/sm8750-devicetrees"
DTS_LINK="$KERNEL_DIR/arch/arm64/boot/dts/vendor"

OUT_DIR=${OUT_DIR:-$REPO_ROOT/out}
DIST_DIR=${DIST_DIR:-$REPO_ROOT/dist}
JOBS=${JOBS:-$(nproc)}
BASE_CONFIG=${BASE_CONFIG:-$REPO_ROOT/configs/ktm-baseline.config}
USE_CCACHE=${USE_CCACHE:-0}

log()  { printf '\033[32m[build]\033[0m %s\n' "$*"; }
warn() { printf '\033[33m[warn]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

check_env() {
	[[ -d "$KERNEL_DIR" ]] || die "找不到内核源码目录：$KERNEL_DIR"
	[[ -f "$BASE_CONFIG" ]] || die "找不到基线配置：$BASE_CONFIG"

	if [[ -n "${CLANG_BIN_DIR:-}" ]]; then
		[[ -x "$CLANG_BIN_DIR/clang" ]] || die "CLANG_BIN_DIR 下没有 clang：$CLANG_BIN_DIR"
		export PATH="$CLANG_BIN_DIR:$PATH"
	fi

	local missing=() cmd
	for cmd in make clang ld.lld python3 bc bison flex pahole rsync; do
		command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
	done
	[[ ${#missing[@]} -eq 0 ]] || die "缺少依赖命令：${missing[*]}"

	log "clang: $(clang --version | head -1)"
	log "pahole: $(pahole --version)"
}

prepare_tree() {
	if [[ ! -e "$DTS_LINK" ]]; then
		[[ -d "$DTS_SRC" ]] || die "找不到设备树目录：$DTS_SRC"
		log "链接设备树：$DTS_LINK -> ../../../../../sm8750-devicetrees"
		ln -sfn ../../../../../sm8750-devicetrees "$DTS_LINK"
	fi
	[[ -f "$DTS_LINK/Makefile" ]] || die "设备树目录摆放异常：$DTS_LINK/Makefile 不存在"

	mkdir -p "$OUT_DIR" "$DIST_DIR"
}

CC_VAR=clang
if [[ "$USE_CCACHE" == "1" ]]; then
	command -v ccache >/dev/null 2>&1 || die "USE_CCACHE=1 但未安装 ccache"
	CC_VAR="ccache clang"
	log "ccache: $(ccache --version | head -1)"
fi

make_args=(
	-C "$KERNEL_DIR"
	O="$OUT_DIR"
	ARCH=arm64
	LLVM=1
	CC="$CC_VAR"
	LD=ld.lld
	HOSTLD=ld.lld
	PAHOLE=pahole
	-j"$JOBS"
	CONFIG_KTM_DTB=y
)


configure() {
	if [[ -f "$OUT_DIR/.config" && "${RECONFIG:-0}" != "1" ]]; then
		log "复用已有 .config（要按基线重新生成请设 RECONFIG=1）"
		make "${make_args[@]}" olddefconfig
		[[ -f "$OUT_DIR/include/config/kernel.release" ]] && \
			log "内核版本：$(cat "$OUT_DIR/include/config/kernel.release")"
		return 0
	fi
	log "生成 .config（基线：${BASE_CONFIG#$REPO_ROOT/}）"
	cp "$BASE_CONFIG" "$OUT_DIR/.config"

	if grep -q '^CONFIG_LOCALVERSION_AUTO=y' "$OUT_DIR/.config"; then
		sed -i 's/^CONFIG_LOCALVERSION_AUTO=y/# CONFIG_LOCALVERSION_AUTO is not set/' "$OUT_DIR/.config"
	fi
	if [[ -n "${KERNEL_LOCALVERSION:-}" ]]; then
		log "覆盖 localversion：$KERNEL_LOCALVERSION"
		sed -i "s|^CONFIG_LOCALVERSION=.*|CONFIG_LOCALVERSION=\"$KERNEL_LOCALVERSION\"|" "$OUT_DIR/.config"
	fi

	make "${make_args[@]}" olddefconfig
	[[ -f "$OUT_DIR/include/config/kernel.release" ]] && \
		log "内核版本：$(cat "$OUT_DIR/include/config/kernel.release")"
	return 0
}

DS_REQUIRED=(
	CONFIG_SYSVIPC CONFIG_POSIX_MQUEUE CONFIG_IPC_NS CONFIG_PID_NS
	CONFIG_DEVTMPFS CONFIG_NETFILTER_XT_MATCH_ADDRTYPE
)
DS_RECOMMENDED=(
	CONFIG_USER_NS CONFIG_MACVLAN CONFIG_IP6_NF_NAT CONFIG_IP6_NF_TARGET_MASQUERADE
	CONFIG_NETFILTER_XT_TARGET_LOG CONFIG_NETFILTER_XT_MATCH_RECENT
	CONFIG_IP_SET CONFIG_IP_SET_HASH_IP CONFIG_IP_SET_HASH_NET CONFIG_NETFILTER_XT_SET
	CONFIG_TMPFS_POSIX_ACL CONFIG_TMPFS_XATTR
)

check_droidspaces() {
	local opt missing=() absent=()
	for opt in "${DS_REQUIRED[@]}"; do
		grep -qx "$opt=y" "$OUT_DIR/.config" || missing+=("$opt")
	done
	for opt in "${DS_RECOMMENDED[@]}"; do
		grep -qx "$opt=y" "$OUT_DIR/.config" || absent+=("$opt")
	done

	log "Droidspaces：必需项 ${#DS_REQUIRED[@]} 项全部生效，推荐项生效 $(( ${#DS_RECOMMENDED[@]} - ${#absent[@]} ))/${#DS_RECOMMENDED[@]}"
	[[ ${#absent[@]} -eq 0 ]] || warn "Droidspaces 推荐选项未生效：${absent[*]}"
	[[ ${#missing[@]} -eq 0 ]] || die "Droidspaces 必需选项未生效：${missing[*]}（检查 $OUT_DIR/.config，本地复用了旧配置时用 RECONFIG=1 重新生成）"
	if grep -qx 'CONFIG_CFS_BANDWIDTH=y' "$OUT_DIR/.config"; then
		warn "CONFIG_CFS_BANDWIDTH=y：会破坏 kABI，原厂 vendor 模块可能加载失败"
	fi
	if grep -qx 'CONFIG_CGROUP_PIDS=y' "$OUT_DIR/.config"; then
		warn "CONFIG_CGROUP_PIDS=y：会破坏 kABI，原厂 vendor 模块可能加载失败"
	fi
}

build_kernel() {
	log "编译 Image 与设备树（-j$JOBS）"
	make "${make_args[@]}" Image dtbs
	[[ -f "$OUT_DIR/arch/arm64/boot/Image" ]] || die "内核 Image 未生成"
}

make_dtbo_img() {
	local dtbo_files=() f
	while IFS= read -r f; do dtbo_files+=("$f"); done < <(find "$OUT_DIR/arch/arm64/boot/dts/vendor" -name '*.dtbo' -type f | sort)
	[[ ${#dtbo_files[@]} -gt 0 ]] || { warn "没有编译出 .dtbo，跳过 dtbo.img"; return 0; }
	[[ ${#dtbo_files[@]} -le 1 ]] && warn "只找到 ${#dtbo_files[@]} 个 .dtbo，dtbo.img 可能不完整"

	local mkdtboimg="$OUT_DIR/tools/mkdtboimg.py"
	if [[ ! -f "$mkdtboimg" ]]; then
		mkdir -p "$OUT_DIR/tools"
		curl -fsSL "https://android.googlesource.com/platform/system/libufdt/+/refs/heads/main/utils/src/mkdtboimg.py?format=TEXT" \
			| base64 -d >"$mkdtboimg" || die "下载 mkdtboimg.py 失败"
	fi
	python3 "$mkdtboimg" create "$DIST_DIR/dtbo.img" --page_size=4096 "${dtbo_files[@]}"
	log "dtbo.img：$(du -h "$DIST_DIR/dtbo.img" | cut -f1)，包含 ${#dtbo_files[@]} 个 dtbo"
}

collect_dist() {
	log "收集产物到 ${DIST_DIR#$REPO_ROOT/}/"
	rm -rf "${DIST_DIR:?}/dtbs" "${DIST_DIR:?}/modules"
	mkdir -p "$DIST_DIR/dtbs"

	cp -f "$OUT_DIR/arch/arm64/boot/Image" "$DIST_DIR/Image"

	find "$OUT_DIR/arch/arm64/boot/dts/vendor" -name '*.dtb' -type f -exec cp -f {} "$DIST_DIR/dtbs/" \;
	find "$OUT_DIR/arch/arm64/boot/dts/vendor" -name '*.dtbo' -type f -exec cp -f {} "$DIST_DIR/dtbs/" \;

	{
		echo "kernel_release: $(cat "$OUT_DIR/include/config/kernel.release")"
		echo "source_commit: $(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
		echo "source_branch: $(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
		echo "clang: $(clang --version | head -1)"
		echo "build_date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
		echo "modules: none（只换内核，不编译模块）"
	} >"$DIST_DIR/kernel-info.txt"

	log "Image：$(du -h "$DIST_DIR/Image" | cut -f1)"
	log "设备树：$(find "$DIST_DIR/dtbs" -type f | wc -l) 个文件，共 $(du -sh "$DIST_DIR/dtbs" | cut -f1)"
}

main() {
	check_env
	prepare_tree
	configure
	check_droidspaces
	build_kernel
	collect_dist
	make_dtbo_img
	log "构建完成，产物在 ${DIST_DIR#$REPO_ROOT/}/"
}

main "$@"
