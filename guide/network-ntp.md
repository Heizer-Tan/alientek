# 时间同步与板载网络

## 时间同步（NTP）

开机顺序：`busybox-hwclock` 先从板载电池保持的 **SNVS RTC** 恢复时间，再由 `board-ntpdate`（BusyBox `ntpd -q`）对时；关机时 `hwclock` 会把系统时间写回 RTC。默认 NTP 服务器 `ntp.aliyun.com`、`ntp.tencent.com`，在 `ap3216c-logger` 之前执行。

```bash
ls -l /dev/rtc0
hwclock -r
date -u
/etc/init.d/board-ntpdate start   # 手动再对一次
hwclock -w                        # 首次校时后写回 RTC
# 自定义服务器：NTP_SERVERS="cn.pool.ntp.org" /etc/init.d/board-ntpdate start
```

需板子能访问外网 NTP；失败时启动不中断，仍可依赖 RTC 电池保持大致正确时间。

## 本地启动网络

本地 MMC 启动场景下，镜像默认安装 `board-network`，在开机时为 `eth0`（ENET2 / `fec2@20b4000`）配置静态地址 `192.168.5.201/24`、默认网关 `192.168.5.1`，并写入 DNS `223.5.5.5`、`119.29.29.29`。U-Boot TFTP/NFS 与 Linux 共用该口，实验室网线只插 ENET2。现场可修改 `etc/default/board-network` 后重启或执行 `/etc/init.d/board-network restart` 生效。

NFS / TFTP 调试启动见 [nfs.md](./nfs.md)。
