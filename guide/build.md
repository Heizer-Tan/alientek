# 编译说明

构建入口：`./scripts/build.sh` → `kas-container` + `kas/alientek-alpha.yml`。  
升级功能额外引入 `meta-swupdate`。

进入交互式 bitbake 环境：

```bash
./scripts/kas-shell.sh
# 或单次命令：
./scripts/kas-shell.sh -c 'bitbake -c cleansstate python3'
```

## 常用命令

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

上游层固定 **scarthgap**（见 `kas/alientek-alpha.yml`）；三层分支须一致，不要混用。

## 默认产物

默认目标为 `alientek-image-update`（`kas/alientek-alpha.yml` 与 `./scripts/build.sh` 一致）：产物在 `build/tmp/deploy/images/imx6ull-alientek-alpha/`，含：

- `u-boot.imx`、`zImage`、`imx6ull-alientek-alpha.dtb`
- `alientek-image-base-*.rootfs.wic`（update 依赖 base）
- 单文件升级包 `*.swu`

仅要 rootfs/wic 时可显式指定：`./scripts/build.sh alientek-image-base`。

## kas / Docker 镜像排障

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
