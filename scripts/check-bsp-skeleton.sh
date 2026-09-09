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
  n_scarthgap=$(grep -c "branch: scarthgap" "$root/kas/alientek-alpha.yml" || true)
  n_wrynose=$(grep -c "branch: wrynose" "$root/kas/alientek-alpha.yml" || true)
  if [[ "$n_scarthgap" -ne 3 ]]; then
    fail "kas 必须三层均为 scarthgap（当前 scarthgap=$n_scarthgap）"
  fi
  if [[ "$n_wrynose" -ne 0 ]]; then
    fail "kas 不应再使用 wrynose（当前 wrynose=$n_wrynose）"
  fi
fi

if [[ -f "$root/meta-alientek/conf/layer.conf" ]]; then
  grep -q "LAYERSERIES_COMPAT_alientek" "$root/meta-alientek/conf/layer.conf" \
    || fail "layer.conf 缺少兼容声明"
  grep -q "scarthgap" "$root/meta-alientek/conf/layer.conf" \
    || fail "layer.conf 未声明 scarthgap"
fi

need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
mc="$root/meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
if [[ -f "$mc" ]]; then
  grep -q 'MACHINEOVERRIDES =. "mx6ull:"' "$mc" || fail "MACHINE 未声明 mx6ull"
  grep -q "imx-base.inc" "$mc" || fail "MACHINE 未 include imx-base.inc"
  grep -q 'IMX_DEFAULT_BOOTLOADER = "u-boot"' "$mc" || fail "MACHINE 未切到 u-boot"
  grep -q 'IMX_DEFAULT_KERNEL = "linux"' "$mc" || fail "MACHINE 未切到 linux"
  grep -q "imx6ull-alientek-alpha.dtb" "$mc" || fail "MACHINE 未设置 imx6ull-alientek-alpha.dtb"
  grep -q "mx6ull_aes_config" "$mc" || fail "MACHINE 未设置 mx6ull_aes_config"
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

need "meta-alientek/recipes-kernel/linux/linux_7.2.bb"
need "meta-alientek/recipes-bsp/device-tree/alientek-aes/imx6ull-alientek-alpha.dts"
need "meta-alientek/recipes-bsp/device-tree/alientek-aes/imx6ull-alientek-alpha.dtsi"
need "meta-alientek/recipes-bsp/u-boot/u-boot_2025.04.bb"
need "meta-alientek/recipes-bsp/u-boot/u-boot_%.bbappend"
need "meta-alientek/recipes-bsp/u-boot/u-boot/boot.cmd"
need "meta-alientek/recipes-bsp/u-boot/u-boot/mx6ull_aes_defconfig"
need "meta-alientek/recipes-kernel/linux/linux/nfs.cfg"

aes="$root/meta-alientek/recipes-bsp/device-tree/alientek-aes/imx6ull-alientek-alpha.dtsi"
if [[ -f "$aes" ]]; then
  grep -q 'iomuxc_snvs' "$aes" || fail "aes dtsi 未使用 iomuxc_snvs"
  grep -qi 'ethernet-phy-id0022.1560' "$aes" || fail "aes dtsi 缺少 KSZ PHY"
  grep -q 'pinctrl_beep' "$aes" || fail "aes dtsi 缺少蜂鸣器 pinctrl_beep"
  grep -q 'label = "beep"' "$aes" || fail "aes dtsi 缺少 gpio-leds beep 节点"
  # 主 iomuxc 活动行不得再写 SNVS/BOOT_MODE（忽略注释）
  if awk '
    /^&iomuxc \{/ {d=1; next}
    d && /^};$/ {d=0; next}
    d && $0 !~ /\/\*/ && $0 !~ /\*\// && $0 ~ /^[[:space:]]*MX6UL.*(SNVS_|BOOT_MODE)/ {exit 1}
  ' "$aes"; then
    :
  else
    fail "aes dtsi 主 iomuxc 仍含活动 SNVS/BOOT_MODE"
  fi
  grep -qE '&sim2|pinctrl_sim2' "$aes" && fail "aes dtsi 仍引用 sim2（U-Boot 无此标签）"
fi

aes_dts="$root/meta-alientek/recipes-bsp/device-tree/alientek-aes/imx6ull-alientek-alpha.dts"
if [[ -f "$aes_dts" ]]; then
  grep -q 'sim2' "$aes_dts" && fail "aes dts 仍引用 sim2"
fi

cmd="$root/meta-alientek/recipes-bsp/u-boot/u-boot/boot.cmd"
if [[ -f "$cmd" ]]; then
  grep -q "nfsroot" "$cmd" || fail "boot.cmd 缺少 nfsroot"
  grep -q "ttymxc0" "$cmd" || fail "boot.cmd 控制台不是 ttymxc0"
  grep -q "mmcboot" "$cmd" || fail "boot.cmd 缺少 mmcboot"
  grep -q "netboot" "$cmd" || fail "boot.cmd 缺少 netboot"
  grep -q "imx6ull-alientek-alpha.dtb" "$cmd" || fail "boot.cmd 未加载 imx6ull-alientek-alpha.dtb"
  grep -q 'setenv fdt_file imx6ull-alientek-alpha.dtb' "$cmd" \
    || fail "boot.cmd 未同步 EVK 变量 fdt_file"
  grep -q '^run mmcboot$' "$cmd" || fail "boot.cmd 末尾未执行 run mmcboot"
fi

defcfg="$root/meta-alientek/recipes-bsp/u-boot/u-boot/mx6ull_aes_defconfig"
if [[ -f "$defcfg" ]]; then
  grep -q 'CONFIG_DEFAULT_DEVICE_TREE="imx6ull-alientek-alpha"' "$defcfg" \
    || fail "U-Boot defconfig 未锁定板级 DEFAULT_DEVICE_TREE"
  grep -q 'fdtfile imx6ull-alientek-alpha.dtb' "$defcfg" \
    || fail "U-Boot BOOTCOMMAND 未设置 fdtfile"
fi

cfg="$root/meta-alientek/recipes-kernel/linux/linux/nfs.cfg"
if [[ -f "$cfg" ]]; then
  grep -q "CONFIG_ROOT_NFS=y" "$cfg" || fail "nfs.cfg 缺少 CONFIG_ROOT_NFS"
  grep -q "CONFIG_IP_PNP=y" "$cfg" || fail "nfs.cfg 缺少 CONFIG_IP_PNP"
  grep -q "# CONFIG_IP_PNP_DHCP is not set" "$cfg" || fail "nfs.cfg 应关闭 CONFIG_IP_PNP_DHCP（强制静态 IP）"
fi

if [[ "${FULL:-1}" == "1" ]]; then
  need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
  need "meta-alientek/recipes-core/images/alientek-image-base.bb"
  need "meta-alientek/recipes-kernel/linux/linux_7.2.bb"
  need "meta-alientek/recipes-bsp/u-boot/u-boot_%.bbappend"
  need "scripts/build.sh"
  need "scripts/kas-env.sh"
  need "scripts/kernel-menuconfig.sh"
  need "scripts/build-dtb.sh"
  need "scripts/export-nfs-tftp.sh"
  grep -q "kas-container" "$root/scripts/build.sh" || fail "build.sh 未使用 kas-container"
  grep -q "kas-container shell" "$root/scripts/kernel-menuconfig.sh" \
    || fail "kernel-menuconfig.sh 未使用 kas-container shell"
  grep -q "bitbake virtual/kernel -c menuconfig" "$root/scripts/kernel-menuconfig.sh" \
    || fail "kernel-menuconfig.sh 未调用内核 menuconfig"
  grep -q "scarthgap" "$root/README.md" || fail "README 未说明 scarthgap"
  grep -q "imx6ull-alientek-alpha.dtb" "$root/scripts/export-nfs-tftp.sh" || fail "export-nfs-tftp.sh 未使用 imx6ull-alientek-alpha.dtb"
  grep -q "netboot" "$root/README.md" || fail "README 未写 netboot"
  grep -q "/srv/nfs/alientek" "$root/README.md" || fail "README 未写 NFS 导出路径"
fi

[[ "$err" -eq 0 ]] || exit 1
echo "PASS: BSP 骨架静态检查通过"
