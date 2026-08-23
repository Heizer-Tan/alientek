# 正点原子 i.MX6ULL 阿尔法 Yocto BSP 骨架设计

日期：2026-08-23  
状态：待用户审阅  
范围：第一期（可启动 BSP 骨架）

## 1. 背景与已拍板决策

在空仓库 `alientek` 上，为正点原子 i.MX6ULL 阿尔法开发板建立可复现的 Yocto 工程。

已确认：

| 项 | 选择 |
| --- | --- |
| 板型 | 正点原子 i.MX6ULL 阿尔法，DDR 512MB |
| 启动介质 | 核心板按 eMMC 设计，第一期默认从 TF 卡启动；eMMC 烧写作为后续目标，不阻塞第一期验收 |
| 内核 / U-Boot | 主线方向：`linux-fslc` + `u-boot-fslc`（Freescale 社区维护的主线分支，不是 NXP `linux-imx`） |
| 构建系统 | kas + Yocto 6.0（Wrynose LTS）+ `meta-freescale` 主线 BSP + 自有 `meta-alientek` |
| 第一期目标 | 串口登录 + 双网口 + **NFS 根文件系统必选** |
| 明确不做（第一期） | LCD、触摸、音频、Wi-Fi、对齐出厂完整体验 |

后续子项目（不在本文范围）：LCD/触摸 → 其余外设与出厂工具 → eMMC 量产烧写。

## 2. 目标与非目标

### 2.1 目标

1. 一条命令检出所有层并编译出可烧写的 TF 卡镜像。
2. 板上 U-Boot 能起来，内核能起来，串口能登录。
3. 两路以太网在 Linux 下可获取地址、可 ping。
4. 支持 NFS 根文件系统启动（TFTP 加载内核和设备树，NFS 挂根）。
5. 阿尔法相对 NXP EVK 的差异（LAN8720、512MB、USDHC 管脚）全部落在 `meta-alientek`，不直接改 poky / meta-freescale。

### 2.2 非目标

- 不引入 NXP `meta-imx`，不使用 `linux-imx` / `u-boot-imx`。
- 不在第一期做图形栈、Qt、 Weston。
- 不把内核、U-Boot 完整源码 vendoring 进本仓库。
- 不保证在 WSL2 宿主机上原生跑 bitbake；推荐 `kas-container`。

## 3. 仓库与分层结构

本仓库只保存「可复现配方」，不保存 Yocto 下载缓存和 `build/`。

```text
alientek/
├── kas/
│   └── alientek-alpha.yml      # 层锁定、MACHINE、镜像目标、主线 BSP 开关
├── meta-alientek/              # 唯一板级层
├── scripts/                    # 宿主机辅助：kas 封装、NFS/TFTP 导出说明用脚本
├── docs/
└── README.md                   # 编译、烧写、NFS 启动
```

`kas/alientek-alpha.yml` 锁定：

- `poky` 分支 `wrynose`
- `meta-openembedded`（至少 `meta-oe`，按依赖再开 `meta-python` / `meta-networking`）
- `meta-freescale` 分支 `wrynose`
- 本仓库内 `meta-alientek`

kas 中固定：

```text
machine: imx6ull-alientek-alpha
distro: poky
target: alientek-image-base
IMX_DEFAULT_BSP = "mainline"
```

若 Wrynose 某层尚未同步导致无法 checkout，允许把三层统一回退到 Scarthgap 5.0 LTS，并在 README 写明原因。这是构建阻塞时的逃生口，不是默认方案。

## 4. 组件

每个单元只做一件事，板级差异只进 `meta-alientek`。

### 4.1 MACHINE：`imx6ull-alientek-alpha`

- 以 `meta-freescale` 的 `imx6ullevk` 为参考，不复制 NXP 专有 firmware 依赖。
- `UBOOT_MACHINE` / defconfig：`mx6ull_alientek_alpha_defconfig`（从 `mx6ull_14x14_evk` 派生）。
- `KERNEL_DEVICETREE`：`imx6ull-alientek-alpha.dtb`（主线路径以实际 `linux-fslc` 树为准，一般为 `nxp/imx/` 或 `arch/arm/boot/dts/`）。
- 内存：设备树 `memory` 节点 `0x80000000 + 0x20000000`（512MB）。
- 串口：UART1，115200。
- 存储：USDHC1 = TF 卡（第一期启动）；USDHC2 = eMMC（设备树启用，第一期不作为根分区）。
- 网口：FEC1 + FEC2，PHY 为 LAN8720，RMII。
- `WKS_FILE`：沿用 meta-freescale 主线 i.MX6ULL 的 U-Boot 镜像布局（`u-boot.imx` 写在 TF 卡 1KB 偏移，或该层对 `u-boot-fslc` 规定的 SPL 布局）。以实际 `u-boot-fslc` + `imx6ullevk` 在 Wrynose 上的产物为准，在 MACHINE 里显式写死，避免默默换成 NXP 布局。

### 4.2 发行版

使用 `poky`。不新建 distro。主线开关只放在 kas 的 `local_conf_header`：`IMX_DEFAULT_BSP = "mainline"`。

### 4.3 U-Boot：`u-boot-fslc` + bbappend

`meta-alientek/recipes-bsp/u-boot/`：

- `u-boot-fslc_%.bbappend` 只追加阿尔法补丁 / defconfig / 设备树片段。
- 补丁内容限于：LAN8720、512MB DRAM 若与 EVK 不一致、USDHC 启动、默认 `bootcmd`（MMC 与 NFS 两套）。
- 默认环境变量必须同时支持：
  - `mmcboot`：TF 卡上的 zImage + dtb + 根文件系统
  - `netboot`：DHCP 或静态 IP，TFTP 加载 zImage/dtb，NFS 挂根
- 控制台：`console=ttymxc0,115200`（与内核 `stdout-path` 一致）。

不 fork 一份完整 U-Boot 进仓库。

### 4.4 内核：`linux-fslc` + bbappend

`meta-alientek/recipes-kernel/linux/`：

- 设备树以主线 `imx6ull-14x14-evk` 为基线，改成阿尔法硬件：LAN8720、eMMC、按键/LED 若影响启动则一并改，LCD/音频保持 disabled。
- 用 `bbappend` 的 `SRC_URI` 投放 dts/dtso，或使用 `KERNEL_DEVICETREE` + 补丁。优先补丁或独立 dts 文件，避免大段复制 evk dtsi。
- 内核必须打开 NFS 根启动所需选项：`CONFIG_NFS_FS`、`CONFIG_ROOT_NFS`、`CONFIG_IP_PNP`、`CONFIG_IP_PNP_DHCP`。用 fragment 文件 `nfs.cfg` 保证可审查。
- defconfig 以 `linux-fslc` 的 i.MX 主线配置为底，不从正点原子 4.1.15 配置往前搬。

### 4.5 镜像：`alientek-image-base`

继承 `core-image-base`，额外包含：

- `packagegroup-core-ssh-openssh`
- `ethtool`、`iproute2`、`iputils`（ping）
- NFS 客户端能力（内核已支持；用户态按 poky 默认即可）

第一期不做自定义 packagegroup 拆分，除非镜像配方超过可读长度。

### 4.6 宿主机脚本

`scripts/` 只做薄封装，不代替 kas：

- `build.sh`：调用 `kas build kas/alientek-alpha.yml`（或 `kas-container`）。
- NFS/TFTP：文档 + 可选脚本，把 `tmp/deploy/images/...` 里的 zImage、dtb、rootfs tar 导出到 `/tftpboot` 和 NFS export。脚本必须检测目录失败并给出明确错误，不假设发行版一定是 Ubuntu。

## 5. 编译与启动数据流

```text
kas-container / kas
    → 检出 poky + meta-openembedded + meta-freescale + meta-alientek
    → bitbake alientek-image-base
    → deploy/
         u-boot*.imx（或 SPL + u-boot.img）
         zImage
         imx6ull-alientek-alpha.dtb
         alientek-image-base-*.wic / .rootfs.tar
```

两条板上路径，都是第一期必验：

1. **TF 卡本地根**：`wic` 写入 TF 卡 → DIP 选择 SD 启动 → U-Boot `mmcboot` → 本卡 rootfs → 串口登录。
2. **NFS 根**：板子 DHCP 或静态 IP → U-Boot `netboot` → TFTP 取 zImage/dtb → `root=/dev/nfs nfsroot=<server>:<export>,nfsvers=3,tcp ip=dhcp` → 串口登录。

NFS 不是可选项。U-Boot 环境、内核 fragment、README 里的 export 示例必须齐套。

开发默认仍建议先 TF 卡证明「板级能启动」，再用同一套镜像做 NFS，避免把 PHY/设备树问题和 NFS 服务问题搅在一起。

## 6. 错误处理

| 场景 | 处理 |
| --- | --- |
| kas checkout 失败 | 打印层名与 git 错误；README 给出 Wrynose→Scarthgap 回退步骤 |
| bitbake 失败 | 不改上游层；补丁和 bbappend 留在 meta-alientek；日志用 `bitbake -e` / `task log` 定位 |
| WSL2 原生 bitbake 失败 | 文档写明不受支持；改走 `kas-container` |
| U-Boot 无输出 | 查串口设备、波特率、烧写偏移、启动拨码 |
| 停在 U-Boot | 查 DRAM 大小、设备树、eMMC/SD 控制器 |
| 内核 panic 无根 | 先 mmc 根，再 nfs 根；对比 `bootargs` |
| 单网口失败 | 先确认 LAN8720 与 MDIO、时钟、reset GPIO；两路分开验证 |
| NFS 挂载失败 | 分段：板端 ping 服务器 → TFTP 能否取文件 → 服务器 export 与 `no_root_squash` → `nfsvers=3` |

错误信息面向操作者，写清「哪一步、缺什么、下一步查什么」，不用笼统 Exception。

## 7. 验收

在阿尔法板（512MB）上，第一期全部通过才算完成：

1. `kas checkout`（或 `kas-container` 等价命令）在干净目录可复现检出。
2. `alientek-image-base` 编译成功，`deploy/images/imx6ull-alientek-alpha/` 含 U-Boot、zImage、dtb、wic。
3. TF 卡启动后串口出现 login，能用 root（或文档中的账号）登录。
4. 两路网口均可 link，至少一路能 DHCP 并 ping 通网关或 PC。
5. NFS 启动：TFTP 成功加载内核与 dtb，NFS 挂根后同样能串口登录。
6. README 按文档操作即可完成编译、烧写、mmcboot、netboot。

不单独做「只检查宿主机、不烧板」的替代验收。没有板时可以编译，但不能宣称第一期完成。

## 8. 风险与约束

- 阿尔法与 NXP 14x14 EVK 的以太网 PHY、部分 GPIO、eMMC 接线不同；这是第一期主要移植工作。
- `linux-fslc` 仍带少量社区补丁，不是 kernel.org 纯树；这是 Yocto 上可维护的主线路径，规格接受这一点。
- 当前环境是 WSL2：Yocto 依赖伪文件系统与大小写敏感，原生构建不作为支持路径。
- Wrynose（6.0）在 2026-08 仍新，层分支可能短暂不一致；逃生口见第 3 节。

## 9. 实现时的文件责任（预览）

| 路径 | 责任 |
| --- | --- |
| `kas/alientek-alpha.yml` | 锁定层、机器、镜像、主线 BSP |
| `meta-alientek/conf/layer.conf` | 层声明 |
| `meta-alientek/conf/machine/imx6ull-alientek-alpha.conf` | MACHINE |
| `meta-alientek/recipes-bsp/u-boot/` | 阿尔法 U-Boot 补丁与环境 |
| `meta-alientek/recipes-kernel/linux/` | 阿尔法 dts 与 nfs.cfg |
| `meta-alientek/recipes-core/images/alientek-image-base.bb` | 第一期镜像 |
| `scripts/build.sh` | kas 入口 |
| `README.md` | 人读操作说明 |

补丁文件保持短小，一个硬件差异一份补丁，避免单文件同时改 PHY、LCD、音频。
