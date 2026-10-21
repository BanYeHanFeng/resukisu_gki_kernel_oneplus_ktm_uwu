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

ui_print " ";
ui_print "*******************************";
ui_print " uwuAOSP Kernel · OnePlus Ace6 (KTM)";
ui_print "*******************************";
ui_print "- 只换内核：vendor_dlkm / vendor_boot 里的模块保持原样";

ui_print " ";
ui_print "- 刷写 boot 内核";
split_boot;
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
