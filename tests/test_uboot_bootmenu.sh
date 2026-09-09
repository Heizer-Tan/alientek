#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
defconfig="${repo_root}/meta-alientek/recipes-bsp/u-boot/u-boot/mx6ull_aes_defconfig"
bootcmd="${repo_root}/meta-alientek/recipes-bsp/u-boot/u-boot/boot.cmd"

grep -Fqx 'CONFIG_CMD_BOOTMENU=y' "${defconfig}"

grep -q 'bootmenu_delay 5\|bootmenu_delay=5' "${bootcmd}"
grep -q 'setenv bootmenu_0 ' "${bootcmd}"
grep -q 'setenv bootmenu_1 ' "${bootcmd}"
grep -q 'Boot from TF (mmc)' "${bootcmd}"
grep -q 'Boot from NFS' "${bootcmd}"
grep -q 'setenv boot_mode mmc' "${bootcmd}"
grep -q 'setenv boot_mode nfs' "${bootcmd}"
grep -q 'setenv bootmenu_default 0' "${bootcmd}"
grep -q 'setenv bootmenu_default 1' "${bootcmd}"
grep -q 'saveenv' "${bootcmd}"
grep -Eq 'setenv bootcmd .*bootmenu|setenv bootcmd bootmenu' "${bootcmd}"

# 菜单项格式：标题=命令
grep -q 'Boot from TF (mmc)=run boot_tf' "${bootcmd}"
grep -q 'Boot from NFS=run boot_nfs' "${bootcmd}"

# 末尾入口必须是 bootmenu，不能再直接 run mmcboot
tail_cmd="$(tail -n 1 "${bootcmd}" | tr -d '\r')"
case "${tail_cmd}" in
  bootmenu|run\ bootcmd) ;;
  *)
    echo "expected boot.cmd to end with bootmenu or run bootcmd, got: ${tail_cmd}" >&2
    exit 1
    ;;
esac

# 首次默认：仅在空时设置，避免冲掉记忆
grep -q 'test -z "${bootmenu_default}"' "${bootcmd}"
grep -q 'test -z "${boot_mode}"' "${bootcmd}"

grep -q 'setenv ethact eth0' "${bootcmd}"
grep -q 'setenv ethprime eth0' "${bootcmd}"
grep -q 'ethaddr' "${bootcmd}"

echo "uboot bootmenu script constraints ok"
