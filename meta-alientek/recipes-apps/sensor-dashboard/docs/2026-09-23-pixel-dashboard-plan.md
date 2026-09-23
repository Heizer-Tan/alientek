# Pixel Dashboard UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `sensor-dashboard` 主页/详情页皮肤改成规格中的复古掌机风（点阵底 + 双线 HUD + ▌ 色标），并确保无虚线焦点框。

**Architecture:** 纯 QSS + 现有 `QWidget`/`QPushButton`/`QLabel` 布局；全局 `NoFocusStyle` 干掉 Qt 焦点虚线；主页顶栏换成双线状态条，时钟挂在现有 `homeTimer_`（1s）上刷新。不改传感器/LED/OTA 业务函数。

**Tech Stack:** Qt6 Widgets、linuxfb、Yocto recipe `sensor-dashboard_1.0.bb`、CMake。

**Spec:** `meta-alientek/recipes-apps/sensor-dashboard/docs/2026-09-23-pixel-dashboard-design.md`

## Global Constraints

- 色板色值必须与 spec 表一致（`#1A1C2C` 等）。
- `border-radius: 0`；不引入图片/新字体包。
- 点阵优先 QSS `radial-gradient` 点阵；若编译期无法验证显示，保留 `█░` 字符条兜底（状态条第二行必有）。
- 不改页面枚举、导航、刷新周期语义（主页 1s / 详情 0.5s）。
- 中文注释；函数保持短小；已有 `NoFocusStyle` 必须保留并进入 recipe。

## File Map

| 文件 | 职责 |
|------|------|
| `files/src/nofocus_style.hpp` | `QProxyStyle` 屏蔽焦点框（已有未提交改动） |
| `files/src/main.cpp` | 安装 Fusion + `NoFocusStyle` |
| `files/src/dashboard.hpp` | 增加 `homeHudClock_` 成员 |
| `files/src/dashboard.cpp` | 样式助手、主页 HUD、卡片/详情皮肤 |
| `sensor-dashboard_1.0.bb` | `SRC_URI` 含 `nofocus_style.hpp` |

---

### Task 1: 落地 NoFocusStyle（消除虚线焦点）

**Files:**
- Create: `meta-alientek/recipes-apps/sensor-dashboard/files/src/nofocus_style.hpp`（若工作区已有则核对内容）
- Modify: `meta-alientek/recipes-apps/sensor-dashboard/files/src/main.cpp`
- Modify: `meta-alientek/recipes-apps/sensor-dashboard/sensor-dashboard_1.0.bb`（`SRC_URI` 增加头文件）

**Interfaces:**
- Consumes: Qt `QProxyStyle`, `QStyleFactory`
- Produces: 进程级 `app.setStyle(new NoFocusStyle(...))`；后续 Task 依赖此行为

- [ ] **Step 1: 确认 `nofocus_style.hpp` 内容**

文件必须包含（可微调，但语义不变）：

```cpp
/* SPDX-License-Identifier: MIT */
#pragma once

#include <QProxyStyle>
#include <QStyleOption>

class NoFocusStyle final : public QProxyStyle {
public:
	using QProxyStyle::QProxyStyle;

	void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
			   QPainter *painter, const QWidget *widget) const override
	{
		if (element == PE_FrameFocusRect)
			return;
		QProxyStyle::drawPrimitive(element, option, painter, widget);
	}

	void drawControl(ControlElement element, const QStyleOption *option,
			 QPainter *painter, const QWidget *widget) const override
	{
		if (element == CE_FocusFrame)
			return;
		if (element == CE_PushButton || element == CE_PushButtonBevel) {
			if (const auto *btn =
				    qstyleoption_cast<const QStyleOptionButton *>(
					    option)) {
				QStyleOptionButton opt(*btn);
				opt.state &= ~QStyle::State_HasFocus;
				QProxyStyle::drawControl(element, &opt, painter,
							 widget);
				return;
			}
		}
		QProxyStyle::drawControl(element, option, painter, widget);
	}
};
```

- [ ] **Step 2: 确认 `main.cpp` 安装 Style**

`main` 在 `QApplication app(...)` 之后、`setupCjkFont` 之前：

```cpp
#include "nofocus_style.hpp"
#include <QStyleFactory>
// ...
if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
	app.setStyle(new NoFocusStyle(fusion));
else
	app.setStyle(new NoFocusStyle);
```

- [ ] **Step 3: 确认 recipe `SRC_URI`**

`sensor-dashboard_1.0.bb` 的 `SRC_URI` 含：

```bitbake
file://src/nofocus_style.hpp \
```

紧挨 `file://src/main.cpp \` 之后。

- [ ] **Step 4: 编译验证**

```bash
cd /home/tan/code/alientek && . poky/oe-init-build-env build >/dev/null
bitbake sensor-dashboard -c compile
```

Expected: `do_compile: Succeeded`

- [ ] **Step 5: Commit**

```bash
git add meta-alientek/recipes-apps/sensor-dashboard/files/src/nofocus_style.hpp \
        meta-alientek/recipes-apps/sensor-dashboard/files/src/main.cpp \
        meta-alientek/recipes-apps/sensor-dashboard/sensor-dashboard_1.0.bb
git commit -m "$(cat <<'EOF'
fix: 用 NoFocusStyle 去掉按钮虚线焦点框

Fusion 代理样式屏蔽 PE_FrameFocusRect，触摸屏不再出现虚线框。
EOF
)"
```

---

### Task 2: 重写像素样式助手（点阵底 / 双线框 / 卡片边）

**Files:**
- Modify: `meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.cpp`（匿名命名空间样式函数 + `applyDarkStyle` + `makeHomeCard`）

**Interfaces:**
- Consumes: 色板常量 `kBg`…`kInk`（保持现有值，已与 spec 一致）
- Produces: `pixelBtnStyle` / `pixelActionStyle` / `monoStyle` / `applyDarkStyle` / `makeHomeCard` 新外观；供 Task 3–4 使用

- [ ] **Step 1: 替换 `pixelBtnStyle` 与 `pixelActionStyle`**

主页卡片与动作按钮改为米白实线边 + 按下反色（去掉旧斜切高光，贴近双线/LCD 感）：

```cpp
QString pixelBtnStyle(const char *face)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2;"
		"  border: 3px solid %3; border-radius: 0px; outline: none;"
		"  text-align: left; padding: 12px 14px; font-weight: bold;"
		"}"
		"QPushButton:focus { outline: none; border: 3px solid %3; }"
		"QPushButton:pressed {"
		"  background-color: %4; border: 3px solid %5;"
		"  padding-top: 14px; padding-left: 16px;"
		"  padding-bottom: 10px; padding-right: 12px;"
		"}")
		.arg(QLatin1String(face), QLatin1String(kText),
		     QLatin1String(kText), QLatin1String(kPanelHi),
		     QLatin1String(kMuted));
}

QString pixelActionStyle(const char * /*face*/)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2;"
		"  border: 3px solid %2; border-radius: 0px; outline: none;"
		"  text-align: center; padding: 10px; font-weight: bold;"
		"  font-size: 16px;"
		"}"
		"QPushButton:focus { outline: none; }"
		"QPushButton:pressed {"
		"  background-color: %3; color: %4; border: 3px solid %4;"
		"}")
		.arg(QLatin1String(kPanel), QLatin1String(kText),
		     QLatin1String(kText), QLatin1String(kInk));
}
```

- [ ] **Step 2: 替换 `monoStyle` 为双线数据框**

```cpp
QString monoStyle()
{
	return QStringLiteral(
		"font-size: 18px; font-family: monospace; color: %1;"
		" background-color: %2; border: 4px double %3; padding: 12px;")
		.arg(QLatin1String(kText), QLatin1String(kInk),
		     QLatin1String(kMuted));
}
```

- [ ] **Step 3: 更新 `applyDarkStyle`（点阵底 + 纯色兜底）**

```cpp
void Dashboard::applyDarkStyle(QWidget *w)
{
	w->setStyleSheet(QStringLiteral(
		"QWidget {"
		"  background-color: %1; color: %2;"
		"  background-image: radial-gradient(%3 1px, transparent 1px);"
		"  background-size: 4px 4px;"
		"}"
		"QLabel { background: transparent; background-image: none; }"
		"QStackedWidget { background-color: %1;"
		"  background-image: radial-gradient(%3 1px, transparent 1px);"
		"  background-size: 4px 4px; }"
		"QPushButton, QPushButton:focus { outline: none; }"
		"*:focus { outline: none; }")
		.arg(QLatin1String(kBg), QLatin1String(kText),
		     QLatin1String(kPanelHi)));
	w->setFocusPolicy(Qt::NoFocus);
}
```

说明：linuxfb 上若点阵不可见，纯色底仍可读；HUD 字符条由 Task 3 保证像素感。

- [ ] **Step 4: 更新 `makeHomeCard` 标签为 `▌`**

把 `■ ` 改成 `▌`，保留 accent 色；hint 可改为 `> OPEN` 或保持 `> ENTER`（任选其一，全站统一）。

```cpp
auto *tag = new QLabel(QString::fromUtf8("▌") + title);
```

- [ ] **Step 5: 编译**

```bash
bitbake sensor-dashboard -c compile
```

Expected: Succeeded

- [ ] **Step 6: Commit**

```bash
git add meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.cpp
git commit -m "$(cat <<'EOF'
style: 像素助手改为点阵底与双线数据框

卡片米白描边、详情 double border，贴近掌机 LCD 规格。
EOF
)"
```

---

### Task 3: 主页游戏状态条 + 时钟

**Files:**
- Modify: `meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.hpp`
- Modify: `meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.cpp`（`buildHomePage`、`onHomeTick`）

**Interfaces:**
- Consumes: `homeTimer_` 1s 回调 `onHomeTick`
- Produces: `QLabel *homeHudClock_`；状态条两行（品牌/`[OK]`/时钟 + `█░` 装饰条）

- [ ] **Step 1: 在 `dashboard.hpp` 增加成员**

在 `homeOtaSummary_` 附近：

```cpp
QLabel *homeHudClock_ = nullptr;
QLabel *homeHudBar_ = nullptr;
```

- [ ] **Step 2: 重写 `buildHomePage` 顶栏**

删除现有 `brand` / `title` / `bar`（居中 ALIENTEK 三段）。替换为：

```cpp
auto *hud = new QLabel;
hud->setObjectName(QStringLiteral("hud"));
hud->setStyleSheet(QStringLiteral(
	"QLabel#hud {"
	"  background-color: %1; color: %2;"
	"  border: 4px double %3; padding: 8px 10px;"
	"  font-family: monospace; font-weight: 900; font-size: 14px;"
	"  background-image: none;"
	"}")
	.arg(QLatin1String(kInk), QLatin1String(kText),
	     QLatin1String(kMuted)));

auto *hudLay = new QVBoxLayout(hud);
hudLay->setContentsMargins(4, 2, 4, 2);
hudLay->setSpacing(2);

auto *row = new QHBoxLayout;
auto *brand = new QLabel(QString::fromUtf8("▓ ALIENTEK"));
brand->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
			     .arg(QLatin1String(kCyan)));
auto *ok = new QLabel(QString::fromUtf8("[OK]"));
ok->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
			  .arg(QLatin1String(kGreen)));
ok->setAlignment(Qt::AlignCenter);
homeHudClock_ = new QLabel(QString::fromUtf8("--:--:--"));
homeHudClock_->setStyleSheet(
	QStringLiteral("color: %1; background: transparent;")
		.arg(QLatin1String(kYellow)));
homeHudClock_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
row->addWidget(brand);
row->addWidget(ok, 1);
row->addWidget(homeHudClock_);

homeHudBar_ = new QLabel(QString::fromUtf8("██████████████████░░░░░░"));
homeHudBar_->setStyleSheet(
	QStringLiteral("color: %1; font-size: 10px; letter-spacing: 1px;"
		       " background: transparent;")
		.arg(QLatin1String(kPanelHi)));

hudLay->addLayout(row);
hudLay->addWidget(homeHudBar_);
lay->addWidget(hud);
```

需要 `#include <QHBoxLayout>` 与 `#include <QTime>`（或 `QDateTime`）。

保留下方 2×3 `QGridLayout` 与六个 `makeHomeCard` 连接不变。

- [ ] **Step 3: 在 `onHomeTick` 更新时钟**

在现有摘要刷新开头或末尾：

```cpp
if (homeHudClock_)
	homeHudClock_->setText(
		QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
```

- [ ] **Step 4: 编译**

```bash
bitbake sensor-dashboard -c compile
```

Expected: Succeeded

- [ ] **Step 5: Commit**

```bash
git add meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.hpp \
        meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.cpp
git commit -m "$(cat <<'EOF'
feat: 主页改为双线 HUD 状态条与秒级时钟

品牌/[OK]/时间 + █░ 装饰条，符合掌机状态栏规格。
EOF
)"
```

---

### Task 4: 详情页与灯控页皮肤对齐 + 验收编译

**Files:**
- Modify: `meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.cpp`（各 `build*Page`、按键大字样式）

**Interfaces:**
- Consumes: Task 2 的 `monoStyle` / `pixelActionStyle` / `makeBackBtn` / `titleStyle`
- Produces: 全页皮肤一致；功能槽函数不变

- [ ] **Step 1: 确认详情页使用 `monoStyle()`**

`buildApPage` / `buildIcmPage` / `buildSysPage` / `buildOtaPage` / `buildLedsPage` 的 detail `QLabel` 已调用 `monoStyle()` 则无需改逻辑；标题保持 `■ 模块名` + `titleStyle(accent)`。

- [ ] **Step 2: 对齐按键页大框为双线**

`buildKeysPage` 中 `keyDetail_` 样式改为：

```cpp
keyDetail_->setStyleSheet(QStringLiteral(
	"font-size: 48px; font-weight: 900; color: %1;"
	" background-color: %2; border: 4px double %3; padding: 24px;"
	" background-image: none;")
	.arg(QLatin1String(kYellow), QLatin1String(kInk),
	     QLatin1String(kMuted)));
```

- [ ] **Step 3: 灯控动作按钮统一 `pixelActionStyle`**

五个按钮继续 `pixelActionStyle(...)` + `noFocus`；`makeBackBtn` 已走新样式。无需改 `onLed*` / `onBeep*`。

- [ ] **Step 4: 全量编译**

```bash
bitbake sensor-dashboard -c compile
```

Expected: Succeeded

- [ ] **Step 5: 对照 spec 验收清单（板端或自检）**

- [ ] 主页一眼是掌机 HUD  
- [ ] 详情/灯控皮肤一致  
- [ ] 无虚线焦点  
- [ ] 编译通过  
- [ ] 功能与改前一致（导航、摘要、LED）

- [ ] **Step 6: Commit**

```bash
git add meta-alientek/recipes-apps/sensor-dashboard/files/src/dashboard.cpp
git commit -m "$(cat <<'EOF'
style: 详情与按键页对齐双线像素框

按键大状态框与数据区统一 double border。
EOF
)"
```

---

## Spec Coverage Check

| Spec 项 | Task |
|---------|------|
| NoFocus / 无虚线 | Task 1 |
| 点阵底、双线、卡片米边、▌ 色标 | Task 2 |
| HUD 状态条 + █░ + 时钟 | Task 3 |
| 详情/灯控/返回一致 | Task 4 |
| 不改业务/导航周期 | 全任务约束 |
| bitbake 通过 | 每任务 compile 步 |

## Placeholder Scan

无 TBD；命令与代码块均为可执行内容。
