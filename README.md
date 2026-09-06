# 正点原子 i.MX6ULL 阿尔法 Yocto BSP

当前板级默认：主线 **U-Boot 2025.04** + **Linux 7.1.4**，设备树 `imx6ull-alientek-alpha.dtb`（`PREFERRED_PROVIDER` 为 `u-boot` / `linux`）。


第一期：主线 `linux (7.1.4)` + `u-boot (2025.04)`，TF 卡启动，双网口（AES DT：KSZ8081），NFS 根文件系统。

## 目录说明

| 路径 | 含义 |
|------|------|
| `meta-alientek/` | 板级层（机器、U-Boot/Linux、AES 设备树、镜像） |
| `kas/` | kas 配置（层分支、provider、local.conf 片段） |
| `scripts/` | 构建 / NFS 导出 / 骨架检查 |
| `docs/` | 设计与计划文档（含历史归档） |
| `poky/`、`meta-freescale/`、`meta-openembedded/` | **kas 检出的上游层**，勿当板级资产提交 |

构建入口：`./scripts/build.sh` → `kas-container` + `kas/alientek-alpha.yml`。

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
# 只检出层并下载源码，不编译（源码进 downloads/）
./scripts/build.sh --fetch-only

# 编译完整镜像（未改动的包走 sstate-cache/，源码包走 downloads/）
./scripts/build.sh

# 只改了设备树时：自动检测并只编 DTB（不编 linux/u-boot），有 /tftp 会顺带拷过去
./scripts/build.sh
# 强制完整构建：./scripts/build.sh --full
# 指定 TFTP：./scripts/build.sh --tftp /tftp

# 只编某个 recipe（例如改了 key-monitor 后）
./scripts/build.sh key-monitor
# 或：./scripts/build.sh --target key-monitor
```

`downloads/` 在仓库根（与 `build/` 分离）；`sstate-cache` 在 `build/sstate-cache/`。若曾把 `DL_DIR` 指到空目录导致重下失败，把旧的 `build/downloads` 迁到仓库根 `downloads/` 即可。
国内访问 `ghcr.io` 常 TLS 超时。`build.sh` 会改拉南大 `ghcr.nju.edu.cn/siemens/kas/kas:5.5`，成功后打成官方标签。

**不要用 `ghcr.1ms.run` 拉 kas:5.5**：该源曾把最大一层（约 232MB）下完但校验失败（`unexpected commit digest`）。若已经踩过，先清坏层再换源：

```bash
docker builder prune -af
docker image rm -f ghcr.1ms.run/siemens/kas/kas:5.5 2>/dev/null || true
echo '210.28.130.20 ghcr.nju.edu.cn' | sudo tee -a /etc/hosts
docker pull ghcr.nju.edu.cn/siemens/kas/kas:5.5
docker tag ghcr.nju.edu.cn/siemens/kas/kas:5.5 ghcr.io/siemens/kas/kas:5.5
./scripts/build.sh
```

若南大也报 `unexpected commit digest sha256:cd581334…`：这是本地 containerd 里第一次从毫秒源写入的坏层，`docker builder prune` 清不掉。先清缓存再拉：

```bash
sudo ./scripts/purge-kas-docker-cache.sh
docker pull ghcr.nju.edu.cn/siemens/kas/kas:5.5
docker tag ghcr.nju.edu.cn/siemens/kas/kas:5.5 ghcr.io/siemens/kas/kas:5.5
./scripts/build.sh
```

仍失败可暂时绕过镜像（WSL 上有 pseudo 风险）：

```bash
KAS_USE_HOST=1 ./scripts/build.sh
```

产物在 kas 工作区的 `build/tmp/deploy/images/imx6ull-alientek-alpha/`：`u-boot.imx`、`zImage`、`imx6ull-alientek-alpha.dtb`、`alientek-image-base-imx6ull-alientek-alpha.rootfs.wic`。

上游层固定 **scarthgap**（见 `kas/alientek-alpha.yml`）；三层分支须一致，不要混用。

## 烧写 TF 卡（mmcboot）

确认设备节点后：

```bash
sudo bmaptool copy alientek-image-base-imx6ull-alientek-alpha.rootfs.wic.gz /dev/sdX
# 或：sudo dd if=alientek-image-base-imx6ull-alientek-alpha.rootfs.wic of=/dev/sdX bs=4M conv=fsync
```

拨码选择 SD 启动，串口 115200。U-Boot 执行 `run mmcboot`。若找不到系统分区，在 U-Boot 里 `mmc list` 后 `setenv mmcdev 0` 再 `run mmcboot`。

## 按键演示（key-monitor）

串口登录后：`key-monitor`（自动找 `gpio-keys`），按 KEY0 会打印 `type=1 code=28 value=1/0`。可选自启：`update-rc.d key-monitor defaults && /etc/init.d/key-monitor start`（写 syslog）。

## AP3216C 演示

设备树节点挂在 `i2c1@0x1e`，镜像内包含 `i2c-tools`、`ap3216c-module` 与 `ap3216c-read`。可先用 `i2cdetect` / `i2cget` / `i2cdump` 排查总线，再读取驱动导出的数据：

```bash
i2cdetect -y 0
ap3216c-read
ap3216c-read -w
# 示例：ir=12 als=345 ps=28
```

若手靠近传感器，`ps` 应上升；遮光/打光时，`als` 应变化。

## 传感器入库与 Web 查询

- 采集：`ap3216c-logger` 每 5 分钟写入 `/var/lib/ap3216c/ap3216c.db`，保留 7 天
- 查询：浏览器打开 `http://<板子IP>:8080/`
- 启停：`/etc/init.d/ap3216c-logger start|stop`；`/etc/init.d/board-web start|stop`
- 手动读数仍可用：`ap3216c-read`

## NFS 启动（netboot，默认方案）

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

3. 板端与 PC 同一网段。当前 `boot.cmd` 默认 `run netboot`（静态 IP + eth1），示例：

```
setenv serverip 192.168.5.27
setenv ipaddr 192.168.5.201
setenv gatewayip 192.168.5.1
setenv netmask 255.255.255.0
setenv nfsroot /srv/nfs/nfs_rootfs
run netboot
```

未更新 `boot.scr` 时，可在 U-Boot 里手动执行上面变量后 `run netboot`。TF 卡可只烧 U-Boot；内核/DTB 走 TFTP，根走 NFS。

## 故障分段

1. U-Boot 无输出：串口设备、波特率、拨码、`dd`/`bmaptool` 是否写对盘
2. 停在 U-Boot：DRAM 512MB、mmc 设备号
3. 内核 panic 无根：先 mmc 根，再 nfs 根，对比 `printenv bootargs`
4. 单网口：KSZ8081 reset（GPIO5_IO7/8）、MDIO 地址 2/1
5. NFS：板端 ping `serverip` → TFTP 能否取 zImage → export 与 `no_root_squash` → `nfsvers=3`


在此感谢 [imx-forge](https://github.com/Awesome-Embedded-Learning-Studio/imx-forge) 项目为本项目设备树设计提供的宝贵参考。