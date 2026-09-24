# recipes-apps 目录整理设计

日期：2026-09-25  
范围：仅重组 `meta-alientek/recipes-apps` 与 `conf/layer.conf` 的 BBFILES；不改业务逻辑。

## 目标

- AP3216C / ICM20608：各合并为**一目录、一 recipe、两包**（方案 A）
- 按功能域分类（方案 C），示例目录名为 **`example`**
- **包名保持不变**，避免改 packagegroup / 镜像依赖

## 目标树

```
recipes-apps/
  common/                         # iio-icm.*、input-device.*（路径不变）
  sensors/
    ap3216c/
      ap3216c_1.0.bb              # → 包 ap3216c-read + ap3216c-logger
      files/...
    icm20608/
      icm20608_1.0.bb             # → 包 icm20608-read + icm20608-logger
      files/...
  example/
    key-monitor/
    touch-monitor/
  board-ui/
    sensor-dashboard/
    webserver/
```

删除旧的顶层：`ap3216c-read/`、`ap3216c-logger/`、`icm20608-read/`、`icm20608-logger/`，以及迁走后的旧路径。

## 多包 recipe 约定

以 `ap3216c_1.0.bb` 为例（`icm20608` 同理）：

- `PN` = `ap3216c`
- `PACKAGES` 含 `ap3216c-read`、`ap3216c-logger`（及 dbg/dev 等默认包）
- `ALLOW_EMPTY:${PN} = "1"` 或把主包置空，避免默认 `${PN}` 抢文件
- `FILES:ap3216c-read` / `FILES:ap3216c-logger` 分开装二进制与 init/default
- `RDEPENDS:ap3216c-logger` 保留 sqlite3
- `inherit update-rc.d`：`INITSCRIPT_PACKAGES = "ap3216c-logger"`，`INITSCRIPT_NAME:ap3216c-logger = "ap3216c-logger"`
- 二进制与 init 脚本**文件名不变**

`icm20608`：`FILESEXTRAPATHS` 指向 `../../common`（相对 `sensors/icm20608/`）。

`example/key-monitor`、`example/touch-monitor`：`FILESEXTRAPATHS` → `../../common`。

`board-ui/sensor-dashboard`：若引用 `../common`，改为 `../../common`。

## layer.conf

现有只匹配两级，需增加一级：

```
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend \
            ${LAYERDIR}/recipes-*/*/*/*.bbappend"
```

## 依赖与引用

- `packagegroup-alientek-core` / `demo`：包名不变，可不改
- 文档/注释里旧路径可顺手更新（非必须）
- 内核模块仍在 `recipes-kernel/`，本次不动

## 验收

- [ ] `bitbake-layers show-recipes | grep -E 'ap3216c|icm20608|key-monitor|sensor-dashboard'` 能解析到新路径
- [ ] `bitbake ap3216c-read ap3216c-logger icm20608-read icm20608-logger` 成功
- [ ] `bitbake key-monitor touch-monitor sensor-dashboard webserver` 成功
- [ ] 旧目录已删除，无残留 recipe

## 非目标

- 不改 C/C++ 业务代码行为
- 不合并 read/logger 为单一安装包
- 不移动 `recipes-kernel` 下的 `*-module`
