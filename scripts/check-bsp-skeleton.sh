#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
err=0
fail() { echo "FAIL: $*" >&2; err=1; }
need() { [[ -f "$root/$1" ]] || fail "缺少文件 $1"; }

need "kas/alientek-alpha.yml"
need "meta-alientek/conf/layer.conf"
need "README.md"

if [[ -f "$root/kas/alientek-alpha.yml" ]]; then
  grep -q "machine: imx6ull-alientek-alpha" "$root/kas/alientek-alpha.yml" \
    || fail "kas 未设置 machine"
  grep -q "alientek-image-base" "$root/kas/alientek-alpha.yml" \
    || fail "kas 未设置镜像目标"
  grep -q 'IMX_DEFAULT_BSP = "mainline"' "$root/kas/alientek-alpha.yml" \
    || fail "kas 未锁定主线 BSP"
  grep -q "branch: wrynose" "$root/kas/alientek-alpha.yml" \
    || fail "kas 未锁定 wrynose"
fi

if [[ -f "$root/meta-alientek/conf/layer.conf" ]]; then
  grep -q "LAYERSERIES_COMPAT_alientek" "$root/meta-alientek/conf/layer.conf" \
    || fail "layer.conf 缺少兼容声明"
  grep -q "wrynose" "$root/meta-alientek/conf/layer.conf" \
    || fail "layer.conf 未声明 wrynose"
fi

need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
mc="$root/meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
if [[ -f "$mc" ]]; then
  grep -q 'MACHINEOVERRIDES =. "mx6ull:"' "$mc" || fail "MACHINE 未声明 mx6ull"
  grep -q "imx-base.inc" "$mc" || fail "MACHINE 未 include imx-base.inc"
  grep -q "imx6ull-alientek-alpha.dtb" "$mc" || fail "MACHINE 未设置阿尔法 dtb"
  grep -q "mx6ull_alientek_alpha_config" "$mc" || fail "MACHINE 未设置阿尔法 U-Boot config"
  grep -q "115200;ttymxc0" "$mc" || fail "MACHINE 串口不是 ttymxc0 115200"
  grep -q 'IMX_DEFAULT_BSP' "$mc" && fail "MACHINE 不要覆盖 IMX_DEFAULT_BSP（由 kas 锁定）"
fi

need "meta-alientek/recipes-core/images/alientek-image-base.bb"
img="$root/meta-alientek/recipes-core/images/alientek-image-base.bb"
if [[ -f "$img" ]]; then
  grep -q "core-image-base" "$img" || fail "镜像未继承 core-image-base"
  grep -q "openssh" "$img" || fail "镜像缺少 openssh"
  grep -q "ethtool" "$img" || fail "镜像缺少 ethtool"
  grep -q "iproute2" "$img" || fail "镜像缺少 iproute2"
  grep -q "iputils" "$img" || fail "镜像缺少 iputils"
fi

need "meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
need "meta-alientek/recipes-kernel/linux/linux-fslc/nfs.cfg"
need "meta-alientek/recipes-kernel/linux/linux-fslc/imx6ull-alientek-alpha.dts"
need "meta-alientek/recipes-kernel/linux/linux-fslc/0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch"

append="$root/meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
dts="$root/meta-alientek/recipes-kernel/linux/linux-fslc/imx6ull-alientek-alpha.dts"
cfg="$root/meta-alientek/recipes-kernel/linux/linux-fslc/nfs.cfg"
mk="$root/meta-alientek/recipes-kernel/linux/linux-fslc/0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch"

if [[ -f "$append" ]]; then
  grep -q "nfs.cfg" "$append" || fail "bbappend 未引用 nfs.cfg"
  grep -q "imx6ull-alientek-alpha.dts" "$append" || fail "bbappend 未引用 dts"
fi
if [[ -f "$dts" ]]; then
  grep -q "imx6ull-14x14-evk.dts" "$dts" || fail "dts 应以 EVK 为基线 include"
  grep -q "0x20000000" "$dts" || fail "dts 内存不是 512MB"
  grep -qi "lan8720\\|smsc" "$dts" || fail "dts 未描述 LAN8720"
fi
if [[ -f "$cfg" ]]; then
  grep -q "CONFIG_ROOT_NFS=y" "$cfg" || fail "nfs.cfg 缺少 CONFIG_ROOT_NFS"
  grep -q "CONFIG_IP_PNP_DHCP=y" "$cfg" || fail "nfs.cfg 缺少 CONFIG_IP_PNP_DHCP"
fi
if [[ -f "$mk" ]]; then
  grep -q "imx6ull-alientek-alpha.dtb" "$mk" || fail "Makefile 补丁未加入 dtb"
fi

# 后续任务会启用完整清单；设置 FULL=1 才检查全部
if [[ "${FULL:-0}" == "1" ]]; then
  need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
  need "meta-alientek/recipes-core/images/alientek-image-base.bb"
  need "meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
  need "meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend"
  need "scripts/build.sh"
fi

[[ "$err" -eq 0 ]] || exit 1
echo "PASS: BSP 骨架静态检查通过"
