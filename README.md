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

# 编译镜像
./scripts/build.sh
```

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
4. 单网口：KSZ8081 reset（GPIO5_IO7/8）、MDIO 地址 2/1
5. NFS：板端 ping `serverip` → TFTP 能否取 zImage → export 与 `no_root_squash` → `nfsvers=3`
