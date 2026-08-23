# 正点原子 i.MX6ULL 阿尔法 Yocto BSP

第一期：主线 `linux-fslc` + `u-boot-fslc`，TF 卡启动，双 LAN8720，NFS 根文件系统。

## 依赖

- Docker 或 Podman
- [kas](https://kas.readthedocs.io/)（提供 `kas-container`）
- 不要在 WSL2 里原生跑 bitbake

## 静态检查

```bash
./scripts/check-bsp-skeleton.sh
```

## 编译

```bash
./scripts/build.sh
```

产物在 kas 工作区的 `build/tmp/deploy/images/imx6ull-alientek-alpha/`：`u-boot.imx`、`zImage`、`imx6ull-alientek-alpha.dtb`、`alientek-image-base-imx6ull-alientek-alpha.rootfs.wic`。

### wrynose 层无法 checkout 时

把 `kas/alientek-alpha.yml` 里 poky、meta-openembedded、meta-freescale 的 `branch` 全部改成 `scarthgap`，并把 `meta-alientek/conf/layer.conf` 的 `LAYERSERIES_COMPAT_alientek` 改成 `"scarthgap wrynose"`。三层必须一起改，不要混用。

## 烧写 TF 卡（mmcboot）

确认设备节点后：

```bash
sudo bmaptool copy alientek-image-base-imx6ull-alientek-alpha.rootfs.wic.gz /dev/sdX
# 或：sudo dd if=alientek-image-base-imx6ull-alientek-alpha.rootfs.wic of=/dev/sdX bs=4M conv=fsync
```

拨码选择 SD 启动，串口 115200。U-Boot 执行 `run mmcboot`。若找不到系统分区，在 U-Boot 里 `mmc list` 后 `setenv mmcdev 0` 再 `run mmcboot`。

## NFS 启动（netboot，第一期必验）

1. 编译完成后导出：

```bash
sudo ./scripts/export-nfs-tftp.sh \
  --deploy-dir build/tmp/deploy/images/imx6ull-alientek-alpha \
  --tftp-dir /tftpboot \
  --nfs-dir /srv/nfs/alientek
```

2. `/etc/exports`：

```
/srv/nfs/alientek *(rw,sync,no_root_squash,no_subtree_check)
```

`sudo exportfs -ra`，并启动 tftpd 与 nfs-server。

3. 板端与 PC 同一网段。U-Boot：

```
setenv serverip 192.168.1.10
setenv nfsroot /srv/nfs/alientek
run netboot
```

## 故障分段

1. U-Boot 无输出：串口设备、波特率、拨码、`dd`/`bmaptool` 是否写对盘
2. 停在 U-Boot：DRAM 512MB、mmc 设备号
3. 内核 panic 无根：先 mmc 根，再 nfs 根，对比 `printenv bootargs`
4. 单网口：LAN8720 reset（GPIO5_IO7/8）、MDIO 地址 0/1
5. NFS：板端 ping `serverip` → TFTP 能否取 zImage → export 与 `no_root_squash` → `nfsvers=3`
