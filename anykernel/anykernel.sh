properties() { '
kernel.string=uwuAOSP Kernel for OnePlus Ace6 (KTM)
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=ktm
device.name2=OP6113L1
device.name3=PLQ110
device.name4=
device.name5=
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; }

BLOCK=boot;
IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;
NO_MAGISK_CHECK=true;

if [ -f "$0" ] && [ "$(dirname "$0")" != "." ]; then
  AKHOME=$(cd "$(dirname "$0")" 2>/dev/null && pwd) || AKHOME=$PWD;
else
  AKHOME=${AKHOME:-$PWD};
fi;
export AKHOME;

export PATH="$AKHOME/tools:$PATH";

. "$AKHOME/tools/ak3-core.sh";

ui_print() {
  if [ "$OUTFD" ] && [ -e "/proc/self/fd/$OUTFD" ]; then
    until [ ! "$1" ]; do
      echo "ui_print $1
      ui_print" >>"/proc/self/fd/$OUTFD";
      shift;
    done;
  else
    until [ ! "$1" ]; do
      [ "$1" = " " ] || echo "$1";
      shift;
    done;
  fi;
}
if [ -z "$OUTFD" ]; then
  echo "! 没有 OUTFD：检测到是在终端里直接执行脚本，输出改打到标准输出";
  echo "! 正常刷机请在 Recovery 或 KernelSU/Magisk App 里刷入整个 zip";
fi;

clean_boot_cmdline() {
  local hdr="$SPLITIMG/header" line old tok keep seen nstrip ndup rmlist duplist nog;

  if [ ! -f "$hdr" ]; then
    ui_print "! 没拿到 split_img/header，跳过 boot cmdline 检查";
    return 0;
  fi;

  while IFS= read -r line; do
    case "$line" in
      cmdline=*) old=${line#cmdline=}; break;;
    esac;
  done <"$hdr";

  if [ -z "$old" ]; then
    ui_print "- boot cmdline 为空，跳过检查";
    return 0;
  fi;

  case $- in
    *f*) nog=0;;
    *) nog=1; set -f;;
  esac;

  nstrip=0;
  ndup=0;
  keep=;
  seen=;
  rmlist=;
  duplist=;
  for tok in $old; do
    case "$tok" in
      ramoops.*)
        nstrip=$((nstrip + 1));
        rmlist="${rmlist:+$rmlist, }$tok";
        continue;;
    esac;
    case " $seen " in
      *" $tok "*)
        ndup=$((ndup + 1));
        duplist="${duplist:+$duplist, }$tok";
        continue;;
    esac;
    seen="$seen $tok";
    keep="${keep:+$keep }$tok";
  done;

  [ "$nog" = 1 ] && set +f;

  if [ "$nstrip" = 0 ] && [ "$ndup" = 0 ]; then
    ui_print "- boot cmdline 干净：没有残留 ramoops.*，也没有重复参数";
    return 0;
  fi;

  [ "$nstrip" != 0 ] && ui_print "! 清掉 $nstrip 个残留 ramoops.*：$rmlist";
  [ "$ndup" != 0 ] && ui_print "! 去掉 $ndup 个重复参数：$duplist";
  [ "$nstrip" != 0 ] && ui_print "- 残留 ramoops.* 会让内核内建的 ramoops 先抢注同名平台设备，" \
                            "vendor 的 qcom_dynamic_ramoops 随后再注册只会 -EEXIST，pmsg 通道一直是关的";

  while IFS= read -r line; do
    case "$line" in
      cmdline=*) printf 'cmdline=%s\n' "$keep";;
      *) printf '%s\n' "$line";;
    esac;
  done <"$hdr" >"$hdr.new" && mv -f "$hdr.new" "$hdr";

  ui_print "- boot header 的 cmdline 已清洗，用它重新打包 boot";
  return 0;
}

ui_print " ";
ui_print "*******************************";
ui_print " uwuAOSP Kernel · OnePlus Ace6 (KTM)";
ui_print "*******************************";
ui_print "- 只换内核：vendor_dlkm / vendor_boot 里的模块保持原样";

ui_print " ";
ui_print "- 刷写 boot 内核";
split_boot;
clean_boot_cmdline;
flash_boot;

rm -f "$AKHOME/Image";

if [ -f "$AKHOME/dtbo.img" ]; then
  ui_print " ";
  ui_print "- 刷写 dtbo 分区（包内带 dtbo.img）";
  flash_generic dtbo;
else
  ui_print "! 安装包里没有 dtbo.img：dtbo 分区保持原样";
fi;

sync 2>/dev/null;
ui_print " ";
ui_print "- 安装完成，重启设备生效";
