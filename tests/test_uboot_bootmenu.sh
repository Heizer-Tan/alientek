#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
defconfig="${repo_root}/meta-alientek/recipes-bsp/u-boot/u-boot/mx6ull_aes_defconfig"
bootcmd="${repo_root}/meta-alientek/recipes-bsp/u-boot/u-boot/boot.cmd"

grep -Fqx 'CONFIG_CMD_BOOTMENU=y' "${defconfig}"
grep -Fqx 'CONFIG_ENV_SIZE=0x4000' "${defconfig}"

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

grep -q 'Boot from TF (mmc)=run boot_tf' "${bootcmd}"
grep -q 'Boot from NFS=run boot_nfs' "${bootcmd}"

tail_cmd="$(tail -n 1 "${bootcmd}" | tr -d '\r')"
case "${tail_cmd}" in
  bootmenu|run\ bootcmd) ;;
  *)
    echo "expected boot.cmd to end with bootmenu or run bootcmd, got: ${tail_cmd}" >&2
    exit 1
    ;;
esac

grep -q 'test -z "${bootmenu_default}"' "${bootcmd}"
grep -q 'test -z "${boot_mode}"' "${bootcmd}"

# NFS must use complete static ip= with eth0 (avoids IP-Config: Incomplete)
grep -q 'ip=192.168.5.201:192.168.5.27:192.168.5.1:255.255.255.0::eth0:off' "${bootcmd}"
grep -q 'setenv ethact eth0' "${bootcmd}"
grep -q 'setenv ethprime eth0' "${bootcmd}"
grep -q 'echo bootargs=' "${bootcmd}"
grep -q 'ping 192.168.5.27' "${bootcmd}"

# Menu handlers must boot inline after saveenv (not run netboot/mmcboot which may be corrupted)
grep -q "setenv boot_nfs 'setenv boot_mode nfs; setenv bootmenu_default 1; saveenv;" "${bootcmd}"
grep -q "setenv boot_tf 'setenv boot_mode mmc; setenv bootmenu_default 0; saveenv;" "${bootcmd}"
grep -q 'bootz 0x80800000 - 0x83000000' "${bootcmd}"

echo "uboot bootmenu script constraints ok"
