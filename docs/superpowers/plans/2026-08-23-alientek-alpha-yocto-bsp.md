# 阿尔法 Yocto BSP 骨架 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在本仓库用 kas + Yocto 6.0（Wrynose）+ `meta-freescale` 主线 BSP + 自有 `meta-alientek`，为正点原子 i.MX6ULL 阿尔法（512MB）产出可烧写的 TF 卡镜像，并支持串口登录、双 LAN8720 网口、NFS 根文件系统启动。

**Architecture:** 本仓库只存配方：`kas/alientek-alpha.yml` 锁定上游层；板级差异全部进 `meta-alientek`（MACHINE、linux-fslc bbappend、u-boot-fslc bbappend、镜像）。内核与 U-Boot 源码不 vendoring。WSL2 只支持 `kas-container`，不支持宿主机原生 bitbake。

**Tech Stack:** kas、Yocto 6.0 Wrynose、poky、meta-openembedded（meta-oe）、meta-freescale、linux-fslc、u-boot-fslc、wic/`imx-uboot-bootpart.wks.in`

## Global Constraints

- Yocto 发行版默认 `wrynose`（6.0 LTS）；三层无法 checkout 时才允许整组回退到 `scarthgap`，并在 README 写明原因
- `IMX_DEFAULT_BSP = "mainline"`；禁止 `linux-imx`、`u-boot-imx`、`meta-imx`
- MACHINE 名：`imx6ull-alientek-alpha`；镜像名：`alientek-image-base`
- 控制台：`ttymxc0`，115200；内存设备树：`0x80000000` / `0x20000000`
- PHY：两路 FEC + LAN8720 RMII；USDHC1=TF 启动；USDHC2=eMMC 设备树启用但不作第一期根分区
- NFS 根启动是第一期必验，不是可选项
- 第一期不做 LCD、触摸、音频、Wi-Fi、图形栈
- 上游层只读；补丁与 dts 只放 `meta-alientek`
- 骨架校验入口：`scripts/check-bsp-skeleton.sh`（每完成一块就跑，退出码 0 才算该任务过关）

---

## File structure

| 路径 | 责任 |
| --- | --- |
| `.gitignore` | 忽略 build/、downloads、sstate、kas 检出目录 |
| `README.md` | 编译、烧写、mmcboot、netboot |
| `kas/alientek-alpha.yml` | 锁定层、MACHINE、镜像、主线 BSP |
| `meta-alientek/conf/layer.conf` | 层声明，兼容 wrynose |
| `meta-alientek/conf/machine/imx6ull-alientek-alpha.conf` | 板级 MACHINE |
| `meta-alientek/recipes-core/images/alientek-image-base.bb` | 第一期镜像 |
| `meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend` | 投放 dts、Makefile 补丁、nfs.cfg |
| `meta-alientek/recipes-kernel/linux/linux-fslc/` | dts、nfs.cfg、补丁 |
| `meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend` | defconfig 补丁、boot.cmd |
| `meta-alientek/recipes-bsp/u-boot/u-boot-fslc/` | U-Boot 补丁与 boot.cmd |
| `scripts/check-bsp-skeleton.sh` | 配方静态检查 |
| `scripts/build.sh` | kas-container 封装 |
| `scripts/export-nfs-tftp.sh` | 把 deploy 产物导出到 TFTP/NFS 目录 |

---

### Task 1: 仓库骨架、kas 与静态检查脚本

**Files:**
- Create: `.gitignore`
- Create: `kas/alientek-alpha.yml`
- Create: `meta-alientek/conf/layer.conf`
- Create: `meta-alientek/README`
- Create: `scripts/check-bsp-skeleton.sh`
- Create: `README.md`（本任务只写「如何跑检查」；完整烧写说明在 Task 6 补齐）

**Interfaces:**
- Consumes: 无
- Produces: `kas/alientek-alpha.yml` 中 `machine: imx6ull-alientek-alpha`、`target: alientek-image-base`、`IMX_DEFAULT_BSP = "mainline"`；`scripts/check-bsp-skeleton.sh` 退出码 0/1

- [ ] **Step 1: 写会失败的检查脚本**

创建 `scripts/check-bsp-skeleton.sh`：

```bash
#!/usr/bin/env bash
# 校验本仓库 BSP 骨架文件是否齐全、关键字符串是否存在
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
fail() { echo "FAIL: $*" >&2; exit 1; }
need() { [[ -f "$root/$1" ]] || fail "缺少文件 $1"; }

need "kas/alientek-alpha.yml"
need "meta-alientek/conf/layer.conf"
need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
need "meta-alientek/recipes-core/images/alientek-image-base.bb"
need "meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
need "meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend"
need "scripts/build.sh"
need "README.md"

grep -q "machine: imx6ull-alientek-alpha" "$root/kas/alientek-alpha.yml" \
  || fail "kas 未设置 machine"
grep -q "alientek-image-base" "$root/kas/alientek-alpha.yml" \
  || fail "kas 未设置镜像目标"
grep -q 'IMX_DEFAULT_BSP = "mainline"' "$root/kas/alientek-alpha.yml" \
  || fail "kas 未锁定主线 BSP"
grep -q "branch: wrynose" "$root/kas/alientek-alpha.yml" \
  || fail "kas 未锁定 wrynose"
grep -q "LAYERSERIES_COMPAT_alientek" "$root/meta-alientek/conf/layer.conf" \
  || fail "layer.conf 缺少兼容声明"
grep -q "wrynose" "$root/meta-alientek/conf/layer.conf" \
  || fail "layer.conf 未声明 wrynose"

echo "PASS: BSP 骨架静态检查通过"
```

`chmod +x scripts/check-bsp-skeleton.sh`

- [ ] **Step 2: 跑检查，确认失败**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `FAIL: 缺少文件 kas/alientek-alpha.yml`（或 layer.conf），退出码非 0

- [ ] **Step 3: 写入骨架文件**

`.gitignore`：

```gitignore
/build/
/build-*/
/downloads/
/sstate-cache/
/layers/
/.kas*
*.pyc
__pycache__/
```

`kas/alientek-alpha.yml`：

```yaml
header:
  version: 14

machine: imx6ull-alientek-alpha
distro: poky
target:
  - alientek-image-base

repos:
  poky:
    url: https://git.yoctoproject.org/git/poky
    branch: wrynose
    layers:
      meta:
      meta-poky:
  meta-openembedded:
    url: https://git.openembedded.org/meta-openembedded
    branch: wrynose
    layers:
      meta-oe:
  meta-freescale:
    url: https://git.yoctoproject.org/git/meta-freescale
    branch: wrynose
  meta-alientek:
    path: ../meta-alientek

local_conf_header:
  mainline-bsp: |
    IMX_DEFAULT_BSP = "mainline"
    PACKAGE_CLASSES = "package_rpm"
    EXTRA_IMAGE_FEATURES += "debug-tweaks ssh-server-openssh"
```

`meta-alientek/conf/layer.conf`：

```bitbake
BBPATH .= ":${LAYERDIR}"

BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "alientek"
BBFILE_PATTERN_alientek = "^${LAYERDIR}/"
BBFILE_PRIORITY_alientek = "8"
LAYERDEPENDS_alientek = "core freescale-layer"
LAYERSERIES_COMPAT_alientek = "wrynose"
```

`meta-alientek/README`：

```text
正点原子 i.MX6ULL 阿尔法板级层。只放 MACHINE、补丁、dts、镜像，不改 poky/meta-freescale。
```

`README.md`（最小）：

```markdown
# alientek

正点原子 i.MX6ULL 阿尔法 Yocto BSP（第一期骨架）。

## 静态检查

```bash
./scripts/check-bsp-skeleton.sh
```
```

本步骤先把检查脚本里「尚未创建」的 machine/image/bbappend/build.sh 用空文件占位会让后续任务的失败信息变差。不要创建空占位。本任务允许检查脚本在后续文件补齐前失败——因此 **把 `check-bsp-skeleton.sh` 改成分段检查**：缺文件时打印所有缺失项，但 Task 1 只强制 kas + layer.conf + README。

把脚本改成：

```bash
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
err=0
fail() { echo "FAIL: $*" >&2; err=1; }
need() { [[ -f "$root/$1" ]] || fail "缺少文件 $1"; }

need "kas/alientek-alpha.yml"
need "meta-alientek/conf/layer.conf"
need "README.md"

if [[ -f "$root/kas/alientek-alpha.yml" ]]; then
  grep -q "machine: imx6ull-alientek-alpha" "$root/kas/alientek-alpha.yml" \
    || fail "kas 未设置 machine"
  grep -q "alientek-image-base" "$root/kas/alientek-alpha.yml" \
    || fail "kas 未设置镜像目标"
  grep -q 'IMX_DEFAULT_BSP = "mainline"' "$root/kas/alientek-alpha.yml" \
    || fail "kas 未锁定主线 BSP"
  grep -q "branch: wrynose" "$root/kas/alientek-alpha.yml" \
    || fail "kas 未锁定 wrynose"
fi

if [[ -f "$root/meta-alientek/conf/layer.conf" ]]; then
  grep -q "LAYERSERIES_COMPAT_alientek" "$root/meta-alientek/conf/layer.conf" \
    || fail "layer.conf 缺少兼容声明"
  grep -q "wrynose" "$root/meta-alientek/conf/layer.conf" \
    || fail "layer.conf 未声明 wrynose"
fi

# 后续任务会启用完整清单；设置 FULL=1 才检查全部
if [[ "${FULL:-0}" == "1" ]]; then
  need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
  need "meta-alientek/recipes-core/images/alientek-image-base.bb"
  need "meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
  need "meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend"
  need "scripts/build.sh"
fi

[[ "$err" -eq 0 ]] || exit 1
echo "PASS: BSP 骨架静态检查通过"
```

- [ ] **Step 4: 再跑检查**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `PASS: BSP 骨架静态检查通过`

- [ ] **Step 5: Commit**

```bash
git add .gitignore kas/alientek-alpha.yml meta-alientek/conf/layer.conf meta-alientek/README scripts/check-bsp-skeleton.sh README.md
git commit -m "feat: 加入 kas 与 meta-alientek 层骨架"
```

---

### Task 2: MACHINE `imx6ull-alientek-alpha`

**Files:**
- Create: `meta-alientek/conf/machine/imx6ull-alientek-alpha.conf`
- Modify: `scripts/check-bsp-skeleton.sh`（FULL 检查仍留给 Task 6；本任务加 MACHINE 专项检查）

**Interfaces:**
- Consumes: `imx-base.inc`、`tune-cortexa7.inc`（来自 meta-freescale / poky）
- Produces: MACHINE 变量 `KERNEL_DEVICETREE`、`UBOOT_CONFIG[sd]`、`SERIAL_CONSOLES`、`UBOOT_ENV`、`IMAGE_BOOT_FILES`

- [ ] **Step 1: 扩展检查脚本**

在 `scripts/check-bsp-skeleton.sh` 末尾、`FULL` 块之前加入：

```bash
need "meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
mc="$root/meta-alientek/conf/machine/imx6ull-alientek-alpha.conf"
if [[ -f "$mc" ]]; then
  grep -q 'MACHINEOVERRIDES =. "mx6ull:"' "$mc" || fail "MACHINE 未声明 mx6ull"
  grep -q "imx-base.inc" "$mc" || fail "MACHINE 未 include imx-base.inc"
  grep -q "imx6ull-alientek-alpha.dtb" "$mc" || fail "MACHINE 未设置阿尔法 dtb"
  grep -q "mx6ull_alientek_alpha_config" "$mc" || fail "MACHINE 未设置阿尔法 U-Boot config"
  grep -q "115200;ttymxc0" "$mc" || fail "MACHINE 串口不是 ttymxc0 115200"
  grep -q 'IMX_DEFAULT_BSP' "$mc" && fail "MACHINE 不要覆盖 IMX_DEFAULT_BSP（由 kas 锁定）"
fi
```

- [ ] **Step 2: 跑检查，确认失败**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `FAIL: 缺少文件 meta-alientek/conf/machine/imx6ull-alientek-alpha.conf`

- [ ] **Step 3: 写 MACHINE**

`meta-alientek/conf/machine/imx6ull-alientek-alpha.conf`：

```bitbake
#@TYPE: Machine
#@NAME: 正点原子 i.MX6ULL 阿尔法
#@SOC: i.MX6ULL
#@DESCRIPTION: 512MB DDR，LAN8720 双网口，TF 卡启动（USDHC1），eMMC 在设备树中启用

MACHINEOVERRIDES =. "mx6ull:"

include conf/machine/include/imx-base.inc
include conf/machine/include/arm/armv7a/tune-cortexa7.inc

KERNEL_DEVICETREE = "nxp/imx/imx6ull-alientek-alpha.dtb"

UBOOT_MAKE_TARGET = "u-boot.imx"
UBOOT_SUFFIX = "imx"
UBOOT_CONFIG ??= "sd"
UBOOT_CONFIG[sd] = "mx6ull_alientek_alpha_config"

UBOOT_ENV = "boot"
UBOOT_ENV_SUFFIX = "scr"

SERIAL_CONSOLES = "115200;ttymxc0"

MACHINE_FEATURES = "usbgadget usbhost vfat ext2 alsa"

IMAGE_BOOT_FILES = " \
    zImage \
    imx6ull-alientek-alpha.dtb \
    boot.scr \
"

WKS_FILE = "imx-uboot-bootpart.wks.in"
```

不要在此文件设置 `PREFERRED_PROVIDER_virtual/kernel`；主线由 `IMX_DEFAULT_BSP = "mainline"` 选 `linux-fslc` / `u-boot-fslc`。

- [ ] **Step 4: 再跑检查**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `PASS: BSP 骨架静态检查通过`

- [ ] **Step 5: Commit**

```bash
git add meta-alientek/conf/machine/imx6ull-alientek-alpha.conf scripts/check-bsp-skeleton.sh
git commit -m "feat: 加入阿尔法 MACHINE 配置"
```

---

### Task 3: 镜像 `alientek-image-base`

**Files:**
- Create: `meta-alientek/recipes-core/images/alientek-image-base.bb`
- Modify: `scripts/check-bsp-skeleton.sh`

**Interfaces:**
- Consumes: `core-image-base`
- Produces: 镜像配方包含 `openssh`、`ethtool`、`iproute2`、`iputils`

- [ ] **Step 1: 扩展检查**

```bash
need "meta-alientek/recipes-core/images/alientek-image-base.bb"
img="$root/meta-alientek/recipes-core/images/alientek-image-base.bb"
if [[ -f "$img" ]]; then
  grep -q "core-image-base" "$img" || fail "镜像未继承 core-image-base"
  grep -q "openssh" "$img" || fail "镜像缺少 openssh"
  grep -q "ethtool" "$img" || fail "镜像缺少 ethtool"
  grep -q "iproute2" "$img" || fail "镜像缺少 iproute2"
  grep -q "iputils" "$img" || fail "镜像缺少 iputils"
fi
```

- [ ] **Step 2: 跑检查，确认失败**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `FAIL: 缺少文件 meta-alientek/recipes-core/images/alientek-image-base.bb`

- [ ] **Step 3: 写镜像配方**

```bitbake
DESCRIPTION = "阿尔法第一期基础镜像：串口登录、SSH、双网口工具"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-openssh"

CORE_IMAGE_EXTRA_INSTALL += " \
    ethtool \
    iproute2 \
    iputils \
"

# NFS 根启动由内核 fragment 提供；用户态不强制 nfs-utils
```

`core-image-base` 是镜像名不是 class。正确写法是：

```bitbake
DESCRIPTION = "阿尔法第一期基础镜像：串口登录、SSH、双网口工具"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb

IMAGE_FEATURES += "ssh-server-openssh"

CORE_IMAGE_EXTRA_INSTALL += " \
    ethtool \
    iproute2 \
    iputils \
"
```

检查脚本里的 `core-image-base` 与此一致。

- [ ] **Step 4: 再跑检查**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add meta-alientek/recipes-core/images/alientek-image-base.bb scripts/check-bsp-skeleton.sh
git commit -m "feat: 加入 alientek-image-base 镜像"
```

---

### Task 4: linux-fslc 设备树与 NFS fragment

**Files:**
- Create: `meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend`
- Create: `meta-alientek/recipes-kernel/linux/linux-fslc/nfs.cfg`
- Create: `meta-alientek/recipes-kernel/linux/linux-fslc/imx6ull-alientek-alpha.dts`
- Create: `meta-alientek/recipes-kernel/linux/linux-fslc/0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch`
- Modify: `scripts/check-bsp-skeleton.sh`

**Interfaces:**
- Consumes: 主线 `imx6ull-14x14-evk.dts`（内核树内 include）
- Produces: `KERNEL_DEVICETREE` 对应 `nxp/imx/imx6ull-alientek-alpha.dtb`；NFS 根所需 `CONFIG_ROOT_NFS=y`

设备树策略：include NXP 14x14 EVK，再覆盖 memory、compatible、两路 FEC 的 LAN8720（地址 0/1，reset GPIO5_IO7 / GPIO5_IO8，与正点原子阿尔法 eMMC 核心板常见接法一致）。LCD/音频节点保持 EVK 原样但第一期不依赖它们。

- [ ] **Step 1: 扩展检查**

```bash
need "meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
need "meta-alientek/recipes-kernel/linux/linux-fslc/nfs.cfg"
need "meta-alientek/recipes-kernel/linux/linux-fslc/imx6ull-alientek-alpha.dts"
need "meta-alientek/recipes-kernel/linux/linux-fslc/0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch"

append="$root/meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend"
dts="$root/meta-alientek/recipes-kernel/linux/linux-fslc/imx6ull-alientek-alpha.dts"
cfg="$root/meta-alientek/recipes-kernel/linux/linux-fslc/nfs.cfg"
mk="$root/meta-alientek/recipes-kernel/linux/linux-fslc/0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch"

if [[ -f "$append" ]]; then
  grep -q "nfs.cfg" "$append" || fail "bbappend 未引用 nfs.cfg"
  grep -q "imx6ull-alientek-alpha.dts" "$append" || fail "bbappend 未引用 dts"
fi
if [[ -f "$dts" ]]; then
  grep -q "imx6ull-14x14-evk.dts" "$dts" || fail "dts 应以 EVK 为基线 include"
  grep -q "0x20000000" "$dts" || fail "dts 内存不是 512MB"
  grep -qi "lan8720\\|smsc" "$dts" || fail "dts 未描述 LAN8720"
fi
if [[ -f "$cfg" ]]; then
  grep -q "CONFIG_ROOT_NFS=y" "$cfg" || fail "nfs.cfg 缺少 CONFIG_ROOT_NFS"
  grep -q "CONFIG_IP_PNP_DHCP=y" "$cfg" || fail "nfs.cfg 缺少 CONFIG_IP_PNP_DHCP"
fi
if [[ -f "$mk" ]]; then
  grep -q "imx6ull-alientek-alpha.dtb" "$mk" || fail "Makefile 补丁未加入 dtb"
fi
```

- [ ] **Step 2: 跑检查，确认失败**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `FAIL: 缺少文件 meta-alientek/recipes-kernel/linux/linux-fslc_%.bbappend`

- [ ] **Step 3: 写内核配方文件**

`nfs.cfg`：

```
CONFIG_NFS_FS=y
CONFIG_NFS_V3=y
CONFIG_ROOT_NFS=y
CONFIG_IP_PNP=y
CONFIG_IP_PNP_DHCP=y
CONFIG_IP_PNP_BOOTP=y
```

`imx6ull-alientek-alpha.dts`：

```dts
// SPDX-License-Identifier: GPL-2.0
/dts-v1/;

#include "imx6ull-14x14-evk.dts"

/ {
	model = "Alientek i.MX6ULL Alpha";
	compatible = "alientek,imx6ull-alpha", "fsl,imx6ull";

	memory@80000000 {
		device_type = "memory";
		reg = <0x80000000 0x20000000>;
	};
};

&fec1 {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_enet1>;
	phy-mode = "rmii";
	phy-handle = <&ethphy0>;
	phy-reset-gpios = <&gpio5 7 GPIO_ACTIVE_LOW>;
	phy-reset-duration = <200>;
	status = "okay";
};

&fec2 {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_enet2>;
	phy-mode = "rmii";
	phy-handle = <&ethphy1>;
	phy-reset-gpios = <&gpio5 8 GPIO_ACTIVE_LOW>;
	phy-reset-duration = <200>;
	status = "okay";
};

&mdio {
	ethphy0: ethernet-phy@0 {
		compatible = "ethernet-phy-id0007.e410", "ethernet-phy-ieee802.3-c22";
		reg = <0>;
		smsc,disable-energy-detect;
		clocks = <&clks IMX6UL_CLK_ENET_REF>;
		clock-names = "rmii-ref";
	};

	ethphy1: ethernet-phy@1 {
		compatible = "ethernet-phy-id0007.e410", "ethernet-phy-ieee802.3-c22";
		reg = <1>;
		smsc,disable-energy-detect;
		clocks = <&clks IMX6UL_CLK_ENET_REF>;
		clock-names = "rmii-ref";
	};
};
```

若 EVK dts 里 `&mdio` 已有 `ethphy0`@2（KSZ8081），编译会因标签重定义失败。处理方式：不要重复定义同名 label。把节点写成覆盖 EVK 的 phy 节点（改 `reg` 与 compatible），或在 dts 中 `/delete-node/ &ethphy0` 后再定义。实施时打开 `imx6ull-14x14-evk.dts` 确认节点名，按下述优先顺序改阿尔法 dts：

1. `/delete-node/` 掉 EVK 的 phy 子节点，再添加 `ethernet-phy@0` / `@1`
2. 若 EVK 用 `ethphy0: ethernet-phy@2`，则改为 `reg = <0>` 并增加 `ethphy1`

`0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch` 在第一次 `kas-container` 解出内核源码后生成。本任务先放占位补丁正文（路径以 linux-fslc 6.12 为准）：

```diff
From: Alientek BSP <bsp@local>
Subject: [PATCH] ARM: dts: imx: add imx6ull-alientek-alpha to Makefile

---
 arch/arm/boot/dts/nxp/imx/Makefile | 1 +
 1 file changed, 1 insertion(+)

diff --git a/arch/arm/boot/dts/nxp/imx/Makefile b/arch/arm/boot/dts/nxp/imx/Makefile
index 1111111..2222222 100644
--- a/arch/arm/boot/dts/nxp/imx/Makefile
+++ b/arch/arm/boot/dts/nxp/imx/Makefile
@@ -200,6 +200,7 @@ dtb-$(CONFIG_SOC_IMX6UL) += \
 	imx6ull-14x14-evk.dtb \
+	imx6ull-alientek-alpha.dtb \
 	imx6ull-colibri-emmc-eval-v3.dtb \
```

占位 `index` 行在 Task 7 用真实内核树重新 `git format-patch`。检查脚本只要求文件里出现 `imx6ull-alientek-alpha.dtb`。

`linux-fslc_%.bbappend`（Yocto 6.0 解包目录为 `UNPACKDIR`）：

```bitbake
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += " \
    file://imx6ull-alientek-alpha.dts \
    file://nfs.cfg \
    file://0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch \
"

do_configure:prepend() {
    install -D -m 0644 ${UNPACKDIR}/imx6ull-alientek-alpha.dts \
        ${S}/arch/arm/boot/dts/nxp/imx/imx6ull-alientek-alpha.dts
}
```

若 `linux-fslc` 不把 `file://*.cfg` 自动合并进 `.config`，在 bbappend 追加：

```bitbake
do_configure:append() {
    if [ -f ${UNPACKDIR}/nfs.cfg ]; then
        ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${UNPACKDIR}/nfs.cfg
    fi
}
```

`do_configure:append` 必须在 defconfig 已展开之后。若 `merge_config.sh` 在 `do_configure` 末尾太早，改放到 `do_compile:prepend` 并 `oe_runmake olddefconfig`。Task 7 若 `.config` 里没有 `CONFIG_ROOT_NFS=y`，就把合并挪到 `do_compile:prepend`。

- [ ] **Step 4: 再跑检查**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add meta-alientek/recipes-kernel/linux scripts/check-bsp-skeleton.sh
git commit -m "feat: 加入阿尔法设备树与 NFS 内核 fragment"
```

---

### Task 5: u-boot-fslc defconfig 与 boot.cmd（mmc + nfs）

**Files:**
- Create: `meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend`
- Create: `meta-alientek/recipes-bsp/u-boot/u-boot-fslc/boot.cmd`
- Create: `meta-alientek/recipes-bsp/u-boot/u-boot-fslc/0001-configs-add-mx6ull_alientek_alpha_defconfig.patch`
- Modify: `scripts/check-bsp-skeleton.sh`

**Interfaces:**
- Consumes: 主线 `mx6ull_14x14_evk_defconfig`（补丁从它复制）
- Produces: `UBOOT_CONFIG[sd] = "mx6ull_alientek_alpha_config"` 能编出 `u-boot.imx`；`boot.scr` 提供 `mmcboot` 与 `netboot`

- [ ] **Step 1: 扩展检查**

```bash
need "meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend"
need "meta-alientek/recipes-bsp/u-boot/u-boot-fslc/boot.cmd"
need "meta-alientek/recipes-bsp/u-boot/u-boot-fslc/0001-configs-add-mx6ull_alientek_alpha_defconfig.patch"

ub="$root/meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend"
cmd="$root/meta-alientek/recipes-bsp/u-boot/u-boot-fslc/boot.cmd"
defp="$root/meta-alientek/recipes-bsp/u-boot/u-boot-fslc/0001-configs-add-mx6ull_alientek_alpha_defconfig.patch"

if [[ -f "$ub" ]]; then
  grep -q "boot.cmd" "$ub" || fail "u-boot bbappend 未引用 boot.cmd"
fi
if [[ -f "$cmd" ]]; then
  grep -q "nfsroot" "$cmd" || fail "boot.cmd 缺少 nfsroot"
  grep -q "ttymxc0" "$cmd" || fail "boot.cmd 控制台不是 ttymxc0"
  grep -q "mmcboot" "$cmd" || fail "boot.cmd 缺少 mmcboot"
  grep -q "netboot" "$cmd" || fail "boot.cmd 缺少 netboot"
  grep -q "imx6ull-alientek-alpha.dtb" "$cmd" || fail "boot.cmd 未加载阿尔法 dtb"
fi
if [[ -f "$defp" ]]; then
  grep -q "mx6ull_alientek_alpha_defconfig" "$defp" || fail "补丁未加入阿尔法 defconfig"
  grep -q "imx6ull-alientek-alpha" "$defp" || fail "defconfig 未指向阿尔法设备树"
fi
```

- [ ] **Step 2: 跑检查，确认失败**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `FAIL: 缺少文件 meta-alientek/recipes-bsp/u-boot/u-boot-fslc_%.bbappend`

- [ ] **Step 3: 写 U-Boot 文件**

`boot.cmd`：

```
setenv fdtfile imx6ull-alientek-alpha.dtb
setenv console ttymxc0,115200
setenv mmcdev 1
setenv mmcpart 1
setenv nfsroot /srv/nfs/alientek

setenv mmcargs "setenv bootargs console=${console} root=/dev/mmcblk1p2 rootwait rw"
setenv mmcboot "echo Booting from MMC...; run mmcargs; fatload mmc ${mmcdev}:${mmcpart} ${loadaddr} zImage; fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

setenv netargs "setenv bootargs console=${console} root=/dev/nfs nfsroot=${serverip}:${nfsroot},nfsvers=3,tcp ip=dhcp"
setenv netboot "echo Booting from NFS...; dhcp; run netargs; tftp ${loadaddr} zImage; tftp ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

setenv bootcmd "run mmcboot"
```

阿尔法从 TF 卡启动时，U-Boot 里 TF 卡常见为 `mmc 0` 或 `mmc 1`。`mmcdev` 在板上用 `mmc list` 确认后改默认值。第一期 README 写明若 mmcboot 找不到分区，把 `mmcdev` 改成 `0`。

`0001-configs-add-mx6ull_alientek_alpha_defconfig.patch`：从 `mx6ull_14x14_evk_defconfig` 复制为 `mx6ull_alientek_alpha_defconfig`，并保证含有：

```
CONFIG_TARGET_MX6ULL_14X14_EVK=y
CONFIG_DEFAULT_DEVICE_TREE="imx6ull-alientek-alpha"
CONFIG_CMD_DHCP=y
CONFIG_CMD_TFTPBOOT=y
CONFIG_CMD_NFS=y
CONFIG_CMD_PING=y
CONFIG_CMD_MMC=y
CONFIG_CMD_FAT=y
CONFIG_CMD_EXT4=y
CONFIG_CMD_BOOTZ=y
```

U-Boot 设备树文件名必须与 `CONFIG_DEFAULT_DEVICE_TREE` 一致。若 U-Boot 2025.01 仍用 `arch/arm/dts/` 而不是内核的 `nxp/imx/` 路径，补丁还需把内核 dts 精简拷一份到 U-Boot `arch/arm/dts/imx6ull-alientek-alpha.dts`（至少 memory + uart1 + usdhc1 + fec），并改 U-Boot `arch/arm/dts/Makefile`。实施顺序：

1. 先只改 defconfig 的 `CONFIG_DEFAULT_DEVICE_TREE` 为已有的 `imx6ull-14x14-evk`，确认能编过
2. 再增加阿尔法 dts（至少改 memory 512MB 与 LAN8720）

检查脚本要求补丁字符串含 `imx6ull-alientek-alpha`，因此第一期补丁就必须带阿尔法 dts，不能只停在 EVK 设备树上。

`u-boot-fslc_%.bbappend`：

```bitbake
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += " \
    file://0001-configs-add-mx6ull_alientek_alpha_defconfig.patch \
    file://boot.cmd \
"
```

poky 的 `u-boot.inc` 在 `UBOOT_ENV = "boot"` 且 `SRC_URI` 含 `boot.cmd` 时会生成 `boot.scr`。若配方不自动编译 `boot.cmd`，在 bbappend 增加：

```bitbake
DEPENDS += "u-boot-mkimage-native"

do_compile:append() {
    mkimage -A arm -O linux -T script -C none -n "boot" \
        -d ${UNPACKDIR}/boot.cmd ${B}/boot.scr
}

do_install:append() {
    install -D -m 0644 ${B}/boot.scr ${D}/boot/boot.scr
}

do_deploy:append() {
    install -D -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}
```

- [ ] **Step 4: 再跑检查**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add meta-alientek/recipes-bsp/u-boot scripts/check-bsp-skeleton.sh
git commit -m "feat: 加入阿尔法 U-Boot defconfig 与 mmc/nfs boot.cmd"
```

---

### Task 6: 构建脚本、NFS/TFTP 导出、README

**Files:**
- Create: `scripts/build.sh`
- Create: `scripts/export-nfs-tftp.sh`
- Modify: `README.md`
- Modify: `scripts/check-bsp-skeleton.sh`（`FULL=1` 变为默认）

**Interfaces:**
- Consumes: `kas/alientek-alpha.yml`、deploy 目录产物
- Produces: `scripts/build.sh` 调用 `kas-container`；`scripts/export-nfs-tftp.sh` 参数 `--deploy-dir` `--tftp-dir` `--nfs-dir`

- [ ] **Step 1: 让完整清单成为默认检查**

把脚本里 `FULL` 默认改为 `1`（`FULL="${FULL:-1}"`），并增加：

```bash
need "scripts/export-nfs-tftp.sh"
grep -q "kas-container" "$root/scripts/build.sh" || fail "build.sh 未使用 kas-container"
grep -q "scarthgap" "$root/README.md" || fail "README 未写 wrynose 失败时的 scarthgap 回退"
grep -q "netboot" "$root/README.md" || fail "README 未写 netboot"
grep -q "/srv/nfs/alientek" "$root/README.md" || fail "README 未写 NFS 导出路径"
```

- [ ] **Step 2: 跑检查，确认失败**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: `FAIL: 缺少文件 scripts/build.sh`

- [ ] **Step 3: 写脚本与 README**

`scripts/build.sh`：

```bash
#!/usr/bin/env bash
# 用 kas-container 编译阿尔法镜像；不在 WSL2 宿主机原生跑 bitbake
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if ! command -v kas-container >/dev/null 2>&1; then
  echo "ERROR: 未找到 kas-container。请先安装 kas，并使用容器构建。" >&2
  echo "  pipx install kas   # 或发行版软件包" >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1 && ! command -v podman >/dev/null 2>&1; then
  echo "ERROR: 需要 docker 或 podman。" >&2
  exit 1
fi

exec kas-container build "$root/kas/alientek-alpha.yml" "$@"
```

`scripts/export-nfs-tftp.sh`：

```bash
#!/usr/bin/env bash
# 把 deploy 里的 zImage、dtb、rootfs tar 拷到 TFTP 与 NFS 导出目录
set -euo pipefail

deploy=""
tftp_dir="/tftpboot"
nfs_dir="/srv/nfs/alientek"

usage() {
  echo "用法: $0 --deploy-dir DIR [--tftp-dir DIR] [--nfs-dir DIR]" >&2
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --deploy-dir) deploy="$2"; shift 2 ;;
    --tftp-dir) tftp_dir="$2"; shift 2 ;;
    --nfs-dir) nfs_dir="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "ERROR: 未知参数 $1" >&2; usage ;;
  esac
done

[[ -n "$deploy" ]] || { echo "ERROR: 必须指定 --deploy-dir" >&2; exit 1; }
[[ -d "$deploy" ]] || { echo "ERROR: deploy 目录不存在: $deploy" >&2; exit 1; }

zimage="$(find "$deploy" -maxdepth 1 -name 'zImage' -print -quit)"
dtb="$(find "$deploy" -maxdepth 1 -name 'imx6ull-alientek-alpha.dtb' -print -quit)"
rootfs="$(find "$deploy" -maxdepth 1 -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs.tar.zst' -o -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs.tar.bz2' -o -name 'alientek-image-base-imx6ull-alientek-alpha.rootfs.tar.gz' | head -n 1)"

[[ -n "$zimage" ]] || { echo "ERROR: 未找到 zImage（是否已编译成功？）" >&2; exit 1; }
[[ -n "$dtb" ]] || { echo "ERROR: 未找到 imx6ull-alientek-alpha.dtb" >&2; exit 1; }
[[ -n "$rootfs" ]] || { echo "ERROR: 未找到 rootfs tar" >&2; exit 1; }

mkdir -p "$tftp_dir" "$nfs_dir"
cp -f "$zimage" "$tftp_dir/zImage"
cp -f "$dtb" "$tftp_dir/imx6ull-alientek-alpha.dtb"

if [[ -n "$(ls -A "$nfs_dir" 2>/dev/null || true)" ]]; then
  echo "ERROR: NFS 目录非空，拒绝覆盖: $nfs_dir" >&2
  echo "       请换目录或先清空后再跑。" >&2
  exit 1
fi

tar --auto-compress -xf "$rootfs" -C "$nfs_dir"
echo "已导出 TFTP=$tftp_dir NFS=$nfs_dir"
echo "请在 /etc/exports 加入: $nfs_dir *(rw,sync,no_root_squash,no_subtree_check)"
```

`chmod +x scripts/build.sh scripts/export-nfs-tftp.sh`

`README.md` 完整内容：

```markdown
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
```

- [ ] **Step 4: 再跑检查**

Run: `bash scripts/check-bsp-skeleton.sh`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add scripts/build.sh scripts/export-nfs-tftp.sh scripts/check-bsp-skeleton.sh README.md
git commit -m "feat: 加入 kas-container 构建与 NFS/TFTP 导出说明"
```

---

### Task 7: kas checkout 与镜像编译

**Files:**
- Modify: `meta-alientek/recipes-kernel/linux/linux-fslc/0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch`（用真实内核树刷新）
- Modify: `meta-alientek/recipes-bsp/u-boot/u-boot-fslc/0001-configs-add-mx6ull_alientek_alpha_defconfig.patch`（用真实 U-Boot 树刷新）
- Modify: `imx6ull-alientek-alpha.dts`（按 EVK 实际 phy 节点做 `/delete-node/`）

**Interfaces:**
- Consumes: Tasks 1–6 全部配方
- Produces: `build/tmp/deploy/images/imx6ull-alientek-alpha/` 下的 `u-boot.imx`、`zImage`、`imx6ull-alientek-alpha.dtb`、wic

- [ ] **Step 1: 检出层**

Run:

```bash
kas-container checkout kas/alientek-alpha.yml
```

Expected: 成功检出 poky / meta-openembedded / meta-freescale。若 wrynose 分支 404，按 README 三层改为 scarthgap 后重试，并另开 commit 记录回退原因。

- [ ] **Step 2: 只解析配方，确认 MACHINE 与 provider**

Run:

```bash
kas-container shell kas/alientek-alpha.yml -c 'bitbake -e alientek-image-base | grep -E "^(MACHINE|PREFERRED_PROVIDER_virtual/kernel|PREFERRED_PROVIDER_virtual/bootloader|IMX_DEFAULT_BSP)="'
```

Expected:

```
MACHINE="imx6ull-alientek-alpha"
IMX_DEFAULT_BSP="mainline"
PREFERRED_PROVIDER_virtual/kernel="linux-fslc"
PREFERRED_PROVIDER_virtual/bootloader="u-boot-fslc"
```

若 kernel 是 `linux-fslc-imx` 或 `linux-imx`，停止，检查 kas 的 `IMX_DEFAULT_BSP` 是否生效（变量名必须与当前 `imx-base.inc` 一致；若上游改名为 `IMX_DEFAULT_BSP` 以外的写法，只改 kas，不改 MACHINE 去 force provider）。

- [ ] **Step 3: 先编 U-Boot，刷新 defconfig 补丁**

Run:

```bash
kas-container shell kas/alientek-alpha.yml -c 'bitbake u-boot-fslc'
```

Expected: `u-boot.imx` 出现在 deploy。第一次会因补丁 `index` 对不上而失败：进入 U-Boot 工作副本，从 `mx6ull_14x14_evk_defconfig` 复制出 `mx6ull_alientek_alpha_defconfig`，改 `CONFIG_DEFAULT_DEVICE_TREE`，补 NFS/TFTP 命令，用 `git format-patch` 替换配方里的补丁，再编。

- [ ] **Step 4: 编内核，刷新 dts Makefile 补丁并确认 NFS config**

Run:

```bash
kas-container shell kas/alientek-alpha.yml -c 'bitbake virtual/kernel'
```

Expected: deploy 中有 `zImage` 与 `imx6ull-alientek-alpha.dtb`。

再确认 NFS：

```bash
kas-container shell kas/alientek-alpha.yml -c 'bitbake -c compile virtual/kernel && grep -E "CONFIG_ROOT_NFS=y|CONFIG_IP_PNP_DHCP=y" tmp/work/*/linux-fslc/*/linux-*/.config'
```

Expected: 两行都是 `=y`。没有则按 Task 4 把 merge_config 挪到 `do_compile:prepend`。

dts 与 EVK phy 标签冲突时，按 Task 4 的 `/delete-node/` 规则改 dts 后重编。

- [ ] **Step 5: 编完整镜像**

Run:

```bash
./scripts/build.sh
```

Expected: deploy 目录含 wic（或 wic.gz）、zImage、dtb、u-boot.imx、rootfs tar。

- [ ] **Step 6: Commit 补丁刷新**

```bash
git add meta-alientek/recipes-kernel/linux meta-alientek/recipes-bsp/u-boot kas/alientek-alpha.yml meta-alientek/conf/layer.conf
git commit -m "fix: 按 Wrynose 源码刷新 U-Boot 与内核补丁"
```

（若无文件变化可省略。）

---

### Task 8: 板上验收（规格第 7 节）

**Files:**
- Modify: 仅当板上发现 `mmcdev`、PHY reset、拨码与文档不符时改 `boot.cmd` / dts / README

**Interfaces:**
- Consumes: Task 7 产物
- Produces: 规格 6 条验收全部通过；无板则不得宣称第一期完成

- [ ] **Step 1: TF 卡 mmcboot**

烧写 wic，串口 115200，确认 U-Boot 横幅、内核启动、`login:`，root 登录（`debug-tweaks` 允许空密码）。

Expected: 能登录。

- [ ] **Step 2: 双网口**

```bash
ip link
ethtool eth0
ethtool eth1
```

Expected: 两路均 link（或至少电缆插上的那路 link）；`udhcpc`/`dhclient` 后至少一路能 ping 通网关或 PC。

- [ ] **Step 3: NFS netboot**

PC 上跑 `export-nfs-tftp.sh`，板端 `run netboot`。

Expected: TFTP 取到 zImage/dtb，NFS 挂根，再次出现 `login:`。

失败按 README「故障分段」记录是 ping、TFTP 还是 export 问题，修配方或文档后再验。

- [ ] **Step 4: 若有板级修正则 Commit**

```bash
git add meta-alientek README.md
git commit -m "fix: 按阿尔法实板修正启动与网口"
```

---

## Self-review vs spec

| 规格条目 | 对应任务 |
| --- | --- |
| kas + wrynose + meta-freescale + meta-alientek | Task 1 |
| `IMX_DEFAULT_BSP = "mainline"`，不用 linux-imx | Task 1、7 |
| MACHINE、512MB、ttymxc0、USDHC、wic 布局 | Task 2 |
| `alientek-image-base` + ssh/ethtool/iproute2/iputils | Task 3 |
| LAN8720 dts、nfs.cfg、不 vendoring 内核 | Task 4 |
| u-boot-fslc、mmcboot+netboot | Task 5 |
| kas-container、NFS/TFTP 脚本、scarthgap 逃生口、故障分段 | Task 6 |
| 编译出 U-Boot/zImage/dtb/wic | Task 7 |
| 板上 6 条验收；无板不能宣称完成 | Task 8 |
| 不做 LCD/音频/Wi-Fi/图形 | 无对应配方，YAGNI |

占位符扫描：Makefile/defconfig 补丁的 `index` 哈希允许在 Task 7 用真实树替换；这是 Yocto 补丁的必要步骤，不是规格空洞。
