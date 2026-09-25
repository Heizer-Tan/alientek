# 正点原子 i.MX6ULL 阿尔法 Yocto BSP

当前板级默认：主线 **U-Boot 2026.07** + **Linux 7.2.4**，设备树 `imx6ull-alientek-alpha.dtb`（`PREFERRED_PROVIDER` 为 `u-boot` / `linux`）。

第一期：主线 `linux (7.2.4)` + `u-boot (2026.07)`，TF 卡启动，双网口（AES DT：KSZ8081），NFS 根文件系统。

## 目录说明

| 路径 | 含义 |
|------|------|
| `meta-alientek/` | 板级层（机器、U-Boot/Linux、AES 设备树、镜像） |
| `kas/` | kas 配置（层分支、provider、local.conf 片段） |
| `scripts/` | 构建 / NFS 导出 / 骨架检查 |
| `guide/` | 入库的操作手册（编译、烧写、OTA、板端演示等） |
| `docs/` | 本地设计/计划文档（**gitignore，不入库**） |
| `poky/`、`meta-freescale/`、`meta-openembedded/` | **kas 检出的上游层**，勿当板级资产提交 |

## 依赖

- Docker 或 Podman
- [kas](https://kas.readthedocs.io/)（提供 `kas-container`）
- 不要在 WSL2 里原生跑 bitbake

## 静态检查

```bash
./scripts/check-bsp-skeleton.sh
```

## 快速编译

```bash
./scripts/build.sh --fetch-only   # 只下载
./scripts/build.sh                # 编完整镜像（默认 alientek-image-update）
./scripts/build.sh --full         # 强制完整构建
./scripts/build.sh key-monitor    # 只编某个 recipe
./scripts/kas-shell.sh            # 交互式 bitbake 环境
```

产物在 `build/tmp/deploy/images/imx6ull-alientek-alpha/`。详细说明、目录缓存与 **kas/Docker 镜像排障** 见 [guide/build.md](guide/build.md)。

## 文档索引

| 文档 | 内容 |
|------|------|
| [guide/build.md](guide/build.md) | 编译命令、产物、kas 镜像排障 |
| [guide/u-boot-recipe.md](guide/u-boot-recipe.md) | U-Boot 配方分层与升版 |
| [guide/flash-tf.md](guide/flash-tf.md) | 烧写 TF 卡 / mmcboot |
| [guide/ota-swupdate.md](guide/ota-swupdate.md) | SWUpdate A/B、Web 升级、回滚 |
| [guide/mqtt-ota.md](guide/mqtt-ota.md) | MQTT OTA 双进程与配置 |
| [guide/board-apps.md](guide/board-apps.md) | 按键/触摸/光感/六轴/dashboard/Web |
| [guide/network-ntp.md](guide/network-ntp.md) | NTP 与板载静态网络 |
| [guide/nfs.md](guide/nfs.md) | NFS/TFTP 调试启动 |
| [guide/troubleshooting.md](guide/troubleshooting.md) | 故障分段速查 |

MQTT 零基础验证：`tests/ota-agent/MQTT-VERIFICATION.md`。  
AP3216C 驱动链路：`meta-alientek/recipes-kernel/modules/ap3216c/AP3216C-DEVICE-PATH.md`。

---

在此感谢 [imx-forge](https://github.com/Awesome-Embedded-Learning-Studio/imx-forge) 项目为本项目设备树设计提供的宝贵参考。
