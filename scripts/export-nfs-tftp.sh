#!/usr/bin/env bash
# 把 deploy 里的 zImage、dtb、rootfs tar 拷到 TFTP 与 NFS 导出目录
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
deploy="${root}/build/tmp/deploy/images/imx6ull-alientek-alpha"
tftp_dir="/srv/tftp"
nfs_dir="/srv/nfs/nfs_rootfs"

usage() {
  echo "用法: $0 [--deploy-dir DIR] [--tftp-dir DIR] [--nfs-dir DIR]" >&2
  echo "  默认 deploy: ${root}/build/tmp/deploy/images/imx6ull-alientek-alpha" >&2
  echo "  默认 tftp:   /srv/tftp" >&2
  echo "  默认 nfs:    /srv/nfs/nfs_rootfs" >&2
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --deploy-dir) deploy="$2"; shift 2 ;;
    --tftp-dir) tftp_dir="$2"; shift 2 ;;
    --nfs-dir) nfs_dir="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "ERROR: 未知参数 $1" >&2; usage ;;
  esac
done

[[ -d "$deploy" ]] || { echo "ERROR: deploy 目录不存在: $deploy" >&2; exit 1; }

zimage="$(find "$deploy" -maxdepth 1 -name 'zImage' -print -quit)"
dtb="$(find "$deploy" -maxdepth 1 -name 'imx6ull-alientek-alpha.dtb' -print -quit)"
# 兼容无时间戳 symlink 与带时间戳的真实文件
rootfs="$(find "$deploy" -maxdepth 1 \( \
  -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs.tar.zst' -o \
  -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs.tar.bz2' -o \
  -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs.tar.gz' -o \
  -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs-*.tar.zst' -o \
  -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs-*.tar.bz2' -o \
  -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs-*.tar.gz' \
  \) -print | head -n 1)"

[[ -n "$zimage" ]] || { echo "ERROR: 未找到 zImage（是否已编译成功？）" >&2; exit 1; }
[[ -n "$dtb" ]] || { echo "ERROR: 未找到 imx6ull-alientek-alpha.dtb" >&2; exit 1; }
if [[ -z "$rootfs" ]]; then
  echo "ERROR: 未找到 rootfs tar（需要 *.rootfs.tar.zst/.gz/.bz2）" >&2
  echo "       当前 deploy 若只有 .wic.gz，说明未开 IMAGE_FSTYPES 的 tar。" >&2
  echo "       机器配置已含 tar.zst 时请重新: ./scripts/build.sh" >&2
  exit 1
fi

mkdir -p "$tftp_dir" "$nfs_dir"
cp -f "$zimage" "$tftp_dir/zImage"
cp -f "$dtb" "$tftp_dir/imx6ull-alientek-alpha.dtb"

if [[ -n "$(ls -A "$nfs_dir" 2>/dev/null || true)" ]]; then
  echo "ERROR: NFS 目录非空，拒绝覆盖: $nfs_dir" >&2
  echo "       请换目录或先清空后再跑。" >&2
  exit 1
fi

tar --auto-compress -xf "$rootfs" -C "$nfs_dir"
echo "已导出 TFTP=$tftp_dir NFS=$nfs_dir"
echo "请在 /etc/exports 加入: $nfs_dir *(rw,sync,no_root_squash,no_subtree_check)"
