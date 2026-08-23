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
