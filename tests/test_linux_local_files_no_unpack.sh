#!/bin/sh
# 板级 nfs.cfg / DTS 不得进 SRC_URI，否则改本地文件会触发整包 do_unpack
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
recipe="${repo_root}/meta-alientek/recipes-kernel/linux/linux_7.2.bb"

grep -q 'SRC_URI = "${KERNELORG_MIRROR}/linux/kernel/v7.x/linux-${PV}.tar.xz"' "${recipe}"
! grep -q 'file://nfs.cfg' "${recipe}"
! grep -q 'file://imx6ull-alientek-alpha.dts' "${recipe}"
! grep -q 'file://imx6ull-alientek-alpha.dtsi' "${recipe}"

grep -q 'ALIENTK_NFS_CFG' "${recipe}"
grep -q 'do_configure\[file-checksums\]' "${recipe}"
grep -q '\${ALIENTK_NFS_CFG}' "${recipe}"
grep -q '\${ALIENTK_DTS}' "${recipe}"
grep -q '\${ALIENTK_DTSI}' "${recipe}"

echo "linux recipe local-file unpack isolation ok"
