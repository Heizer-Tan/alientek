# Qt6 传感器仪表盘设计（替换 LVGL）

日期：2026-09-23  
状态：已实现（待板上验证）  
相关：`sensor-dashboard`、`packagegroup-alientek-demo`、`kas/alientek-alpha.yml`

## 目标

用 **Qt6 Widgets + linuxfb** 重写 LCD 传感器仪表盘，功能与现 LVGL 版对等，并从镜像与板级层中 **彻底移除 LVGL**。

## 已确认需求

| 项 | 选择 |
|---|---|
| 范围 | 替换仪表盘应用（非仅加库） |
| 显示后端 | `linuxfb`（`/dev/fb0`） |
| 功能 | 对等：主页两卡片 → 详情 → 返回；中文；开机自启 |
| LVGL | 彻底移除（应用 + `lvgl` bbappend） |
| UI 框架 | Qt Widgets（方案 A，不用 QML） |

## 非目标

- QML / Qt Quick
- Wayland / X11 / eglfs
- 历史曲线、Web UI 改造
- 改动 AP3216C / ICM20608 内核驱动
- 改包名或 init 脚本路径（仍为 `sensor-dashboard`）

## 方案概览

**meta-qt6（scarthgap）+ 精简 qtbase（linuxfb/evdev）+ C++ Widgets 重写 `sensor-dashboard`，删除 LVGL。**

### 1. Yocto / kas

- `kas/alientek-alpha.yml` 增加 `meta-qt6` 仓库，`branch: scarthgap`，layers 含 `meta-qt6`
- `qtbase` PACKAGECONFIG：启用 linuxfb、fonts、相关输入；关闭 X11/Wayland（以 meta-qt6 scarthgap 实际可用项为准，板级用 `qtbase_%.bbappend` 收紧）
- `sensor-dashboard_1.0.bb`：
  - `DEPENDS` / `RDEPENDS`：`qtbase`（及构建所需 `qtbase-native` 等）替代 `lvgl`
  - `RDEPENDS` 增加 CJK 字体包（优先选用层内已有 recipe，如 `ttf-wqy-microhei` / `ttf-droid`；以 kas 层内可解析名为准）
- 删除整个 `meta-alientek/recipes-graphics/lvgl/`
- `packagegroup-alientek-demo` 继续安装 `sensor-dashboard`，不单独拉 lvgl

### 2. 应用结构与 UI

- 语言：C++17 + Qt Widgets
- 页面：`QStackedWidget`
  - 主页：光感 / 六轴两张可点卡片，显示关键实时值
  - AP3216C 详情：ir / als / ps +「返回」
  - ICM20608 详情：加速度 / 角速度 / 温度（含换算值）+「返回」
- 刷新：主页默认 1000 ms，详情 500 ms（与现 `/etc/default/sensor-dashboard` 一致）
- 读数：设备节点默认 `/dev/ap3216c`、`/dev/icm20608`；解析行为与现 `sensors.c` 对齐（迁到 C++）
- 启动环境（init / default）：
  - `QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0`（可用 `SENSOR_DASHBOARD_FB` 覆盖 fb 路径）
  - 触摸：Qt `evdevtouch`；`SENSOR_DASHBOARD_TOUCH_DEV` 可指定；空则扫 Goodix
  - 如需：`QT_QPA_FONTDIR` 指向字体目录
  - 保留 vtconsole 解绑与清屏逻辑，避免控制台叠在 GUI 上
- 安装路径不变：`/usr/bin/sensor-dashboard`、`/etc/init.d/sensor-dashboard`、`/etc/default/sensor-dashboard`
- 删除 LVGL 专用源：`main.c`、`ui.c`、`lv_font_dashboard.c` 及对应头文件中的 LVGL 依赖

### 3. 验证

- 构建：`bitbake sensor-dashboard` / 完整镜像成功；rootfs **无** `liblvgl` / lvgl 包
- 板上：开机自启 → 中文主页 → 触摸进出详情 → 遮光/倾斜数值合理
- 回归：`ap3216c-read`、`icm20608-read`、Web `:8080` 行为不变

### 4. 文档

- 更新 `README.md`「LCD 传感器仪表盘」：LVGL → Qt6 linuxfb
- 本设计文档作为实现依据

## 风险与对策

| 风险 | 对策 |
|------|------|
| Qt6 首次编译久、镜像变大 | 只开 linuxfb 相关配置，不加 qtdeclarative |
| WSL 宿主 pseudo / 大小写问题 | 优先 `kas-container`；宿主仅调试 |
| 触摸设备/坐标不对 | 保留 `SENSOR_DASHBOARD_TOUCH_DEV`；默认 Goodix 扫描 |
| 中文字体缺失 | recipe 明确字体 `RDEPENDS`；必要时设 `QT_QPA_FONTDIR` |
| meta-qt6 层依赖未进 kas | 与现有 scarthgap 分支对齐，首次构建用 `--full` |

## 实现顺序（摘要）

1. kas 引入 meta-qt6 + qtbase bbappend  
2. 重写 `sensor-dashboard`（Widgets + 传感器读取）并改 recipe/init/default  
3. 删除 LVGL 板级文件与依赖  
4. 更新 README；本地/板上验证清单打勾  
