# U-Boot Recipe 结构

当前 U-Boot 配方按「版本层 + 公共层 + 角色层 + 板级层」拆分，后续升级主线版本时尽量只增量改动：

- `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot-source-<ver>.inc`：只放版本号、tarball 地址、sha256、源码目录
- `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot-alientek-common.inc`：放跨版本共用逻辑，例如公共依赖、构建目录、release tarball 补 `.git`
- `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot-target-alientek.inc`：放目标 U-Boot 的公共配置
- `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot-tools-alientek.inc`：放 `u-boot-tools` 共用兼容逻辑
- `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot_<ver>.bb`、`u-boot-tools_<ver>.bb`：仅负责组合上述层
- `meta-alientek/recipes-bsp/bootloader/u-boot/u-boot_%.bbappend`：只负责阿尔法板 `defconfig`、DTS 和默认 `fdt_file` 注入

## 升级主线版本

```bash
# 1. 新增版本层
meta-alientek/recipes-bsp/bootloader/u-boot/u-boot-source-2027.xx.inc

# 2. 新增薄入口
meta-alientek/recipes-bsp/bootloader/u-boot/u-boot_2027.xx.bb
meta-alientek/recipes-bsp/bootloader/u-boot/u-boot-tools_2027.xx.bb

# 3. 更新版本选择
kas/alientek-alpha.yml
```

若上游行为有变化，优先修改 `u-boot-target-alientek.inc` 或 `u-boot-tools-alientek.inc`，尽量不要把兼容逻辑重新散回各个版本文件。
