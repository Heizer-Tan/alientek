#!/bin/sh
set -eu

rootfs="$1"

# 仅移除非必要服务链接，保留 sshd
for service in avahi-daemon rpcbind alsa-state; do
    rm -f "${rootfs}/etc/rcS.d/"*"$service" \
          "${rootfs}/etc/rc0.d/"*"$service" \
          "${rootfs}/etc/rc1.d/"*"$service" \
          "${rootfs}/etc/rc2.d/"*"$service" \
          "${rootfs}/etc/rc3.d/"*"$service" \
          "${rootfs}/etc/rc4.d/"*"$service" \
          "${rootfs}/etc/rc5.d/"*"$service" \
          "${rootfs}/etc/rc6.d/"*"$service"
done
