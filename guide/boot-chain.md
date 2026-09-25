# Linux 启动链路（U-Boot / DTB / 内核）

本文说明嵌入式 Linux 从上电到用户空间的接力过程，并对照本仓库阿尔法板（i.MX6ULL）的实际配置（`boot.cmd`、A/B 分区、NFS 调试）。

---

## 1. 总览

```
上电 / 复位
    │
    ▼
① BootROM（芯片固化）
    │  按拨码/eFuse 选启动介质（SD / eMMC / USB…）
    ▼
② U-Boot（第二级引导；部分平台前还有 SPL）
    │  读环境变量、选根分区、加载内核 + DTB（可有 initrd）
    │  设置 bootargs，执行 bootz / bootm / booti
    ▼
③ Linux 内核
    │  解压、解析 DTB、初始化驱动、挂根文件系统
    ▼
④ 用户空间（init）
    │  启动服务、登录、dashboard 等
```

| 部件 | 角色 | 大致类比 |
|------|------|----------|
| **BootROM** | 芯片里写死的「找引导程序」 | PC 固件找启动盘 |
| **U-Boot** | 可配置的引导加载器 | 更贴近嵌入式的 GRUB |
| **zImage / Image** | Linux 内核镜像 | 操作系统本体 |
| **DTB** | 设备树二进制：板级硬件描述 | 告诉内核有哪些外设、接在哪 |
| **rootfs** | 根文件系统 | `/`、`/bin`、`/etc`… |

内核主要靠 **DTB**（再配合驱动 `compatible`）认识这块板，而不是自己「扫遍整板」。

---

## 2. 各层在做什么

### BootROM（SoC）

- 读启动拨码（阿尔法板选 SD 时从 TF 启动）。
- 从介质固定偏移加载引导镜像（本 BSP 产物为 `u-boot.imx`）。
- 几乎不可改，只负责把控制权交给 U-Boot。

### U-Boot

- 初始化到「能读存储 / 能网启」的程度（串口、MMC、网口等）。
- 使用环境变量：`bootargs`、`active_slot`、`fdtfile` / `fdt_file` 等。
- 从介质把 **内核 + DTB** 读入内存，再 `bootz`（或 `bootm` / `booti`）跳转。
- **不跑** 你的应用；把 CPU 交给内核后使命结束。

本仓库 `boot.cmd` 中与启动直接相关的核心是：

```text
fatload … zImage                         → 0x80800000
fatload … imx6ull-alientek-alpha.dtb     → 0x83000000
bootz ${loadaddr} - ${fdt_addr_r}
```

中间的 `-` 表示 **没有 initrd**（根在磁盘或 NFS，不靠内存盘）。

内核与 DTB **必须分地址**：未设置 `fdt_addr_r` 时可能回退到 `loadaddr`，DTB 会盖住 `zImage`。本板固定：

- 内核：`0x80800000`
- DTB：`0x83000000`

### DTB（Device Tree Blob）

- 源码：`.dts` / `.dtsi`（板级如 `imx6ull-alientek-alpha.dts`，SoC 如 `imx6ul.dtsi`）。
- 编译成 `.dtb`，由 U-Boot 加载后交给内核。
- 描述 CPU、内存、总线、引脚、`compatible`、`reg`、`status` 等。
- 例：`i2c1` 上的 `ap3216c@1e`、`compatible = "fsl,imx6ul-i2c"`，决定驱动能否 `probe`。

DTB 错误或不匹配时：内核可能起来，但外设大面积不可用（无网、无屏、无 I2C…）。

### Linux 内核

1. 解压自身，建立基本运行环境。
2. **解析 DTB**，建成运行时设备树。
3. 按 DT 注册设备，匹配驱动并 `probe`。
4. 读取 `bootargs`（控制台、`root=`、`rootwait` 等）。
5. 挂载根文件系统，执行 init。

### 根文件系统（rootfs）

- TF 量产：`rootfsA` / `rootfsB`（A/B 升级）。
- NFS 调试：`root=/dev/nfs`。
- 之后才是 `board-network`、`dashboard`、可选内核模块等用户空间。

---

## 3. 本板 mmc 启动（量产路径）

```
拨码 SD 启动
    │
    ▼
i.MX6ULL BootROM
    │  读 TF 前部 u-boot.imx
    ▼
U-Boot（当前默认 2026.07）
    │  bootcmd → bootmenu → Boot from TF（boot_tf）
    │  按 active_slot 选根（优先 PARTUUID，否则 mmcblk0p2/p3）
    │  bootargs：console=ttymxc0,115200 root=… rootwait rw
    ▼
从 boot 分区（FAT）加载
    zImage                         @ 0x80800000
    imx6ull-alientek-alpha.dtb     @ 0x83000000
    │
    ▼
bootz → Linux（当前默认 7.2.4）
    │  解析 DTB → 挂 rootfsA 或 rootfsB
    ▼
用户空间（BusyBox init 等）
```

分区大致关系（见 `meta-alientek/wic/imx6ull-alientek-ab.wks.in`）：

| 区域 | 内容 |
|------|------|
| 卡前部 raw | `u-boot.imx` |
| `boot`（FAT） | `zImage`、`imx6ull-alientek-alpha.dtb`、`boot.scr` |
| `rootfsA` / `rootfsB` | 根文件系统（A/B） |

`boot.scr` 由 `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot/boot.cmd` 编译而来，是 U-Boot 实际执行的脚本。

---

## 4. 三条启动路径对比

| 路径 | 内核 / DTB 来源 | 根文件系统 |
|------|-----------------|------------|
| **TF mmc**（量产） | FAT `boot` 分区 | `rootfsA` / `rootfsB` |
| **NFS netboot**（调试） | TFTP | PC 上 NFS 导出目录 |
| **SWUpdate 升级后** | 仍可由 boot 分区提供 | 写入非活动槽，重启后依 `active_slot` 切换 |

升级不改变「BootROM → U-Boot → bootz」这条骨架，只更换镜像文件或切换根槽。详见 [ota-swupdate.md](./ota-swupdate.md)、[nfs.md](./nfs.md)。

---

## 5. 容易混淆的点

1. **DTB ≠ 内核**：内核是通用逻辑；DTB 是「这块板怎么接」。同一 `zImage` 可搭配不同 DTB。
2. **U-Boot DTS 与 Linux DTS**：两边都可以有设备树；**跑 Linux 时用的是传给 `bootz` 的那份 Linux DTB**。
3. **驱动与 DT**：如 `compatible = "alientek,ap3216c"` 只在内核驱动 + Linux DTB 匹配时 `probe`；U-Boot 通常不管这颗传感器。完整从设备链路见 `meta-alientek/recipes-kernel/modules/ap3216c/AP3216C-DEVICE-PATH.md`。
4. **load 地址**：内核与 DTB 必须分开，避免互相覆盖。

---

## 6. 关系示意

```text
         ┌─────────────┐
         │  硬件原理图  │
         └──────┬──────┘
                │ 写成
                ▼
    .dts/.dtsi ──编译──► .dtb ──U-Boot fatload/tftp──► 内存
                                                         │
    内核源码 ──编译──► zImage ──fatload/tftp─────────► 内存
                                                         │
                                              bootz 交给内核
                                                         │
                                                         ▼
                               解析 DTB → 匹配驱动 → 挂 root → init
```

## 相关文档

- [烧写 TF](./flash-tf.md)
- [NFS 调试启动](./nfs.md)
- [SWUpdate A/B](./ota-swupdate.md)
- [故障分段](./troubleshooting.md)
- [U-Boot 配方结构](./u-boot-recipe.md)
