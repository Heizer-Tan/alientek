# Qt6 Sensor Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace LVGL `sensor-dashboard` with Qt6 Widgets on linuxfb, feature-parity UI, and remove LVGL from the BSP.

**Architecture:** Add meta-qt6 (scarthgap); slim `qtbase` for linuxfb/evdev; rewrite dashboard as C++ Widgets + stacked pages; keep init/default paths; delete `recipes-graphics/lvgl`.

**Tech Stack:** Qt6 Widgets, linuxfb, evdev touch, Yocto/kas scarthgap, C++17

**Spec:** `.superpowers/specs/2026-09-23-qt6-sensor-dashboard-design.md`

## Global Constraints

- Display: `QT_QPA_PLATFORM=linuxfb` on `/dev/fb0` only (no X11/Wayland/eglfs/QML).
- Feature parity: home cards → AP/ICM detail → back; CJK text; boot autostart.
- Package/paths unchanged: `/usr/bin/sensor-dashboard`, `/etc/init.d/sensor-dashboard`, `/etc/default/sensor-dashboard`.
- Remove LVGL entirely from meta-alientek and image.
- Chinese comments; functions with clear I/O; prefer small focused files.
- Do not commit unless the user explicitly asks.

## File map

| Path | Role |
|------|------|
| `kas/alientek-alpha.yml` | Add meta-qt6 repo + optional DISTRO_FEATURES trim notes via local_conf |
| `meta-alientek/recipes-qt/qt6/qtbase_%.bbappend` | linuxfb-focused PACKAGECONFIG |
| `meta-alientek/recipes-apps/sensor-dashboard/*` | Qt rewrite + recipe/init/default |
| `meta-alientek/recipes-graphics/lvgl/` | Delete entire tree |
| `tests/test_sensor_dashboard_parse.sh` | Host parse unit smoke |
| `README.md` | LVGL → Qt6 docs |

---

### Task 1: kas 引入 meta-qt6 + qtbase bbappend

**Files:**
- Modify: `kas/alientek-alpha.yml`
- Create: `meta-alientek/recipes-qt/qt6/qtbase_%.bbappend`
- Modify: `scripts/check-bsp-skeleton.sh`（若检查层列表，补 meta-qt6 存在性；无则跳过）

**Interfaces:**
- Produces: cas 能解析 `qtbase`；板级 bbappend 强制 linuxfb

- [ ] **Step 1: 在 kas 增加 meta-qt6**

在 `repos:` 中、`meta-swupdate` 之后插入：

```yaml
  meta-qt6:
    url: https://code.qt.io/yocto/meta-qt6.git
    branch: 6.8
    layers:
      meta-qt6:
```

说明：scarthgap 对应 Qt 6.8.x 时 meta-qt6 常用分支名为 `6.8`（若 clone 后无此分支，改为与 [meta-qt6 scarthgap 文档](https://code.qt.io/cgit/yocto/meta-qt6.git) 一致的 `6.8.3`/`scarthgap` 实际存在分支）。`url` 若 TLS 慢可改 `https://github.com/qt/meta-qt6.git`。

在 `local_conf_header` 增加：

```yaml
  qt6-linuxfb: |
    # 仪表盘只用 linuxfb；避免拉 X11/Wayland Qt 插件
    DISTRO_FEATURES:remove = "x11 wayland"
```

注意：若后续发现某 recipe 强依赖 `x11` DISTRO_FEATURE，改为仅在 `qtbase_%.bbappend` 里 `PACKAGECONFIG:remove`，撤回这条 DISTRO_FEATURES:remove。

- [ ] **Step 2: 写 qtbase bbappend**

创建 `meta-alientek/recipes-qt/qt6/qtbase_%.bbappend`：

```bitbake
# 阿尔法：无 GPU 加速需求，Qt 走 linuxfb + libinput/udev
PACKAGECONFIG:append = " linuxfb libinput fontconfig widgets"
PACKAGECONFIG:remove = " xcb gl gles2 eglfs kms gbm vulkan"
```

- [ ] **Step 3: 验证 kas 配置可解析（不要求编完 Qt）**

```bash
# 有网络时
KAS_USE_HOST=1 ./scripts/build.sh --fetch-only
# 或容器：去掉错误 127.0.0.1 代理后
./scripts/build.sh --fetch-only
```

Expected: meta-qt6 检出成功；无 YAML/layer 解析错误。

- [ ] **Step 4: 提交（仅当用户要求时）**

```bash
git add kas/alientek-alpha.yml meta-alientek/recipes-qt/qt6/qtbase_%.bbappend
git commit -m "$(cat <<'EOF'
feat: 引入 meta-qt6 并为阿尔法启用 linuxfb

EOF
)"
```

---

### Task 2: 传感器解析库（可宿主单测，无 Qt）

**Files:**
- Create: `meta-alientek/recipes-apps/sensor-dashboard/files/src/sensors.hpp`
- Create: `meta-alientek/recipes-apps/sensor-dashboard/files/src/sensors.cpp`
- Create: `tests/test_sensor_dashboard_parse.sh`
- Delete later (Task 4): 旧 `sensors.c` / `sensors.h`

**Interfaces:**
- Produces:
  - `struct ApSample { unsigned ir, als, ps; bool valid; }`
  - `struct IcmSample { int ax,ay,az,gx,gy,gz,temp_raw; double ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c; bool valid; }`
  - `bool parseApSample(const char *line, ApSample *out);`
  - `bool parseIcmSample(const char *line, IcmSample *out);`
  - `bool readApSample(const char *devPath, ApSample *out);`
  - `bool readIcmSample(const char *devPath, IcmSample *out);`

- [ ] **Step 1: 写失败单测脚本**

创建 `tests/test_sensor_dashboard_parse.sh`：

```bash
#!/usr/bin/env bash
# 宿主编译 sensors.cpp（TEST_PARSE）并断言解析
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/meta-alientek/recipes-apps/sensor-dashboard/files/src"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/test_parse.cpp" <<'EOF'
#define SENSOR_DASHBOARD_TEST_PARSE 1
#include "sensors.cpp"
#include <cstdio>
#include <cstdlib>

static void fail(const char *m) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(1); }

int main() {
  ApSample a{};
  if (!parseApSample("ir=1 als=2 ps=3", &a) || a.ir != 1 || a.als != 2 || a.ps != 3 || !a.valid)
    fail("ap ok");
  if (parseApSample("bad", &a)) fail("ap bad");
  IcmSample i{};
  const char *line =
    "ax=1 ay=2 az=3 gx=4 gy=5 gz=6 temp_raw=7 "
    "ax_g=0.1 ay_g=0.2 az_g=0.9 gx_dps=0.0 gy_dps=0.0 gz_dps=0.0 temp_c=25.0";
  if (!parseIcmSample(line, &i) || i.ax != 1 || i.az_g < 0.89 || !i.valid)
    fail("icm ok");
  std::puts("PASS");
  return 0;
}
EOF

g++ -std=c++17 -Wall -Wextra -I"$src" -o "$tmp/t" "$tmp/test_parse.cpp"
"$tmp/t"
```

- [ ] **Step 2: 跑测，确认失败（文件尚不存在）**

```bash
chmod +x tests/test_sensor_dashboard_parse.sh
./tests/test_sensor_dashboard_parse.sh
```

Expected: FAIL（找不到 sensors.cpp / 编译错误）

- [ ] **Step 3: 实现 sensors.hpp / sensors.cpp**

`sensors.hpp`：

```cpp
/* SPDX-License-Identifier: MIT */
#pragma once

struct ApSample {
	unsigned ir = 0;
	unsigned als = 0;
	unsigned ps = 0;
	bool valid = false;
};

struct IcmSample {
	int ax = 0, ay = 0, az = 0;
	int gx = 0, gy = 0, gz = 0;
	int temp_raw = 0;
	double ax_g = 0, ay_g = 0, az_g = 0;
	double gx_dps = 0, gy_dps = 0, gz_dps = 0;
	double temp_c = 0;
	bool valid = false;
};

/* 解析设备文本行；成功 true */
bool parseApSample(const char *line, ApSample *out);
bool parseIcmSample(const char *line, IcmSample *out);

#ifndef SENSOR_DASHBOARD_TEST_PARSE
bool readApSample(const char *devPath, ApSample *out);
bool readIcmSample(const char *devPath, IcmSample *out);
#endif
```

`sensors.cpp`：从现有 `sensors.c` 逻辑迁移；`sscanf` 格式字符串保持不变；返回值改为 `bool`；`readDevLine` 保留。

- [ ] **Step 4: 跑测通过**

```bash
./tests/test_sensor_dashboard_parse.sh
```

Expected: 打印 `PASS`

---

### Task 3: Qt Widgets UI + main + init/default + recipe

**Files:**
- Create: `files/src/main.cpp`, `dashboard.hpp`, `dashboard.cpp`, `CMakeLists.txt`（或 Makefile 用 qmake/`pkg-config Qt6Widgets`）
- Modify: `sensor-dashboard_1.0.bb`, `sensor-dashboard.init`, `sensor-dashboard.default`
- Delete: `main.c`, `ui.c`, `ui.h`, `lv_font_dashboard.c`, 旧 `Makefile`

**Interfaces:**
- Consumes: Task 2 的 `parse*` / `read*`
- Produces: 可执行文件 `sensor-dashboard`；环境变量见 default

- [ ] **Step 1: 实现 Dashboard UI（结构）**

`dashboard.hpp`：

```cpp
#pragma once
#include "sensors.hpp"
#include <QWidget>

class Dashboard final : public QWidget {
	Q_OBJECT
public:
	explicit Dashboard(const QString &apDev, const QString &icmDev,
			   int homeMs, int detailMs, QWidget *parent = nullptr);

private slots:
	void onHomeTick();
	void onDetailTick();
	void openAp();
	void openIcm();
	void backHome();

private:
	void rebuildHomeLabels();
	void rebuildApLabels();
	void rebuildIcmLabels();

	QString apDev_;
	QString icmDev_;
	class QStackedWidget *stack_ = nullptr;
	class QLabel *homeApSummary_ = nullptr;
	class QLabel *homeIcmSummary_ = nullptr;
	class QLabel *apDetail_ = nullptr;
	class QLabel *icmDetail_ = nullptr;
	class QTimer *homeTimer_ = nullptr;
	class QTimer *detailTimer_ = nullptr;
};
```

`dashboard.cpp` 要点：
- 页 0：两按钮/卡片「光感 AP3216C」「六轴 ICM20608」+ summary QLabel
- 页 1/2：详情 QLabel（等宽多行）+「返回」`QPushButton`
- 定时器按页启停；中文用 `QString::fromUtf8(...)`
- 读失败时显示「读取失败」

- [ ] **Step 2: main.cpp**

```cpp
#include "dashboard.hpp"
#include <QApplication>
#include <QFont>
#include <cstdlib>

static const char *envOr(const char *k, const char *d) {
	const char *v = std::getenv(k);
	return (v && *v) ? v : d;
}

int main(int argc, char **argv) {
	qputenv("QT_QPA_PLATFORM",
		qgetenv("QT_QPA_PLATFORM").isEmpty()
			? QByteArray("linuxfb:fb=") + envOr("SENSOR_DASHBOARD_FB", "/dev/fb0")
			: qgetenv("QT_QPA_PLATFORM"));
	QApplication app(argc, argv);
	QFont font(QString::fromUtf8("WenQuanYi Micro Hei"));
	if (!font.exactMatch())
		font = QFont(QString::fromUtf8("Noto Sans CJK SC"));
	font.setPointSize(16);
	app.setFont(font);
	Dashboard w(QString::fromUtf8(envOr("SENSOR_DASHBOARD_AP_DEV", "/dev/ap3216c")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_ICM_DEV", "/dev/icm20608")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_HOME_MS", "1000")).toInt(),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_DETAIL_MS", "500")).toInt());
	w.showFullScreen();
	return app.exec();
}
```

触摸：依赖 Qt `evdevtouch`；若设置了 `SENSOR_DASHBOARD_TOUCH_DEV`，main 里：

```cpp
if (const char *t = std::getenv("SENSOR_DASHBOARD_TOUCH_DEV"); t && *t)
	qputenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", t);
```

- [ ] **Step 3: 构建文件**

优先 `CMakeLists.txt` + recipe `inherit cmake_qt6`（meta-qt6 提供）。若 inherit 名不同，用：

```bitbake
DEPENDS = "qtbase"
inherit cmake pkgconfig
```

CMakeLists.txt：

```cmake
cmake_minimum_required(VERSION 3.16)
project(sensor-dashboard LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(sensor-dashboard main.cpp dashboard.cpp sensors.cpp)
target_link_libraries(sensor-dashboard PRIVATE Qt6::Widgets)
install(TARGETS sensor-dashboard RUNTIME DESTINATION bin)
```

- [ ] **Step 4: 改 recipe**

`sensor-dashboard_1.0.bb` 关键内容：

```bitbake
SUMMARY = "LCD 传感器仪表盘（Qt6 linuxfb）"
DESCRIPTION = "主页卡片进入 AP3216C / ICM20608 详情，触摸操作"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://main.cpp;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "qtbase"
RDEPENDS:${PN} = "qtbase-plugins ap3216c-module icm20608-module ttf-wqy-microhei"

SRC_URI = " \
    file://src/main.cpp \
    file://src/dashboard.cpp \
    file://src/dashboard.hpp \
    file://src/sensors.cpp \
    file://src/sensors.hpp \
    file://src/CMakeLists.txt \
    file://sensor-dashboard.init \
    file://sensor-dashboard.default \
"
S = "${WORKDIR}/src"

inherit cmake_qt6 update-rc.d

INITSCRIPT_NAME = "sensor-dashboard"
INITSCRIPT_PARAMS = "defaults 90"

FILES:${PN} += "${sysconfdir}/init.d/sensor-dashboard ${sysconfdir}/default/sensor-dashboard"

do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/sensor-dashboard.init ${D}${sysconfdir}/init.d/sensor-dashboard
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/sensor-dashboard.default ${D}${sysconfdir}/default/sensor-dashboard
}
```

若 `ttf-wqy-microhei` 在层中不存在：改为 `ttf-wqy-zenhei` 或 `ttf-droid`（`bitbake -s | grep ttf-wqy` 确认后改 RDEPENDS 与 main 字体族名）。若 `cmake_qt6` 不存在：`inherit qt6-cmake cmake`（以 meta-qt6 文档为准）。

更新 `LIC_FILES_CHKSUM`：对 `main.cpp` 第一行 MIT 注释算 md5。

- [ ] **Step 5: init / default**

`sensor-dashboard.default`：

```bash
# sensor-dashboard（Qt6 linuxfb）
# SENSOR_DASHBOARD_FB=/dev/fb0
# SENSOR_DASHBOARD_TOUCH_DEV=
# SENSOR_DASHBOARD_AP_DEV=/dev/ap3216c
# SENSOR_DASHBOARD_ICM_DEV=/dev/icm20608
# SENSOR_DASHBOARD_HOME_MS=1000
# SENSOR_DASHBOARD_DETAIL_MS=500
# QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
# QT_QPA_FONTDIR=/usr/share/fonts
```

`sensor-dashboard.init`：Short-Description 改为 Qt6；`do_start` 在 `loadDefaults` 后确保：

```sh
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-linuxfb:fb=${SENSOR_DASHBOARD_FB:-/dev/fb0}}"
```

保留现有 vtconsole 解绑与清屏逻辑。

- [ ] **Step 6: 交叉编译冒烟（有 sstate/容器时）**

```bash
./scripts/build.sh --full sensor-dashboard
# 或
./scripts/kas-shell.sh -c 'bitbake sensor-dashboard'
```

Expected: 任务成功；`tmp/work/.../sensor-dashboard/.../image/usr/bin/sensor-dashboard` 存在。

---

### Task 4: 删除 LVGL + 文档

**Files:**
- Delete: `meta-alientek/recipes-graphics/lvgl/` 下全部文件
- Delete: 旧 LVGL 源 `main.c` `ui.c` `ui.h` `lv_font_dashboard.c` `Makefile` `sensors.c` `sensors.h`（若 Task 3 未删）
- Modify: `README.md` LCD 小节
- Modify: `scripts/check-bsp-skeleton.sh`（去掉对 lvgl 路径的假设，如有）

- [ ] **Step 1: 删除 LVGL 板级文件**

```bash
rm -rf meta-alientek/recipes-graphics/lvgl
rm -f meta-alientek/recipes-apps/sensor-dashboard/files/src/main.c \
      meta-alientek/recipes-apps/sensor-dashboard/files/src/ui.c \
      meta-alientek/recipes-apps/sensor-dashboard/files/src/ui.h \
      meta-alientek/recipes-apps/sensor-dashboard/files/src/lv_font_dashboard.c \
      meta-alientek/recipes-apps/sensor-dashboard/files/src/Makefile \
      meta-alientek/recipes-apps/sensor-dashboard/files/src/sensors.c \
      meta-alientek/recipes-apps/sensor-dashboard/files/src/sensors.h
```

- [ ] **Step 2: 更新 README**

将「LCD 传感器仪表盘」中 LVGL 表述改为：

- 镜像含 `sensor-dashboard`（**Qt6 Widgets + linuxfb** `/dev/fb0` + evdev/Goodix 触摸）
- 环境变量说明补充 `QT_QPA_PLATFORM`

- [ ] **Step 3: 确认无残留引用**

```bash
rg -n 'lvgl|LVGL|lv_font_dashboard' meta-alientek kas README.md scripts tests || true
```

Expected: 无业务引用（本 plan/spec 历史文档除外可保留）。

- [ ] **Step 4: 骨架检查**

```bash
./scripts/check-bsp-skeleton.sh
```

Expected: 通过（按脚本现有规则）。

---

### Task 5: 板上验证清单（人工）

- [ ] 烧录/NFS 启动含新 dashboard 的镜像
- [ ] 开机自启，主页中文两卡片可见
- [ ] 触摸进入光感/六轴详情并返回
- [ ] 遮光/倾斜时数值变化合理
- [ ] `lsmod` / `ap3216c-read` / Web `:8080` 正常
- [ ] `opkg list-installed | grep -i lvgl` 无输出（或对应包管理命令）

---

## Spec coverage（自检）

| Spec 项 | Task |
|---------|------|
| meta-qt6 + linuxfb qtbase | 1 |
| Widgets UI 对等 | 3 |
| 传感器路径/解析 | 2 |
| init/default/包名不变 | 3 |
| 删除 LVGL | 4 |
| CJK 字体 | 3（RDEPENDS + QFont） |
| README | 4 |
| 验证 | 5 + Task 2/3 冒烟 |

无 TBD 占位；字体包名以实现时 `bitbake -s` 校准为唯一可变点（已写明回退名）。
