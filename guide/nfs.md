# NFS 启动（netboot，调试入口）

1. 编译完成后导出（需有 `*.rootfs.tar.zst`；仅有 `wic.gz` 时先完整编一次镜像）：

```bash
sudo ./scripts/export-nfs-tftp.sh \
  --tftp-dir /tftp \
  --nfs-dir /srv/nfs/nfs_rootfs
```

（`--deploy-dir` 默认为仓库下 `build/tmp/deploy/images/imx6ull-alientek-alpha`，一般可省略。）

2. `/etc/exports`：

```
/srv/nfs/nfs_rootfs *(rw,sync,no_root_squash,no_subtree_check)
```

`sudo exportfs -ra`，并启动 tftpd 与 nfs-server。

3. 板端与 PC 同一网段，网线插 ENET2（`ethernet@20b4000`）。当前 `boot.cmd` 保留 `run netboot` 作为调试入口（静态 IP + eth0，禁止 DHCP），示例：

```
setenv serverip 192.168.5.27
setenv ipaddr 192.168.5.201
setenv gatewayip 192.168.5.1
setenv netmask 255.255.255.0
setenv nfsroot /srv/nfs/nfs_rootfs
run netboot
```

未更新 `boot.scr` 时，可在 U-Boot 里手动执行上面变量后 `run netboot`。TF 卡可只烧 U-Boot；内核/DTB 走 TFTP，根走 NFS。

板载静态网络默认见 [network-ntp.md](./network-ntp.md)；排障见 [troubleshooting.md](./troubleshooting.md)。
