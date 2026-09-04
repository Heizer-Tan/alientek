# key-monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `meta-alientek` 中提供 C 程序 `key-monitor`，读取 `gpio-keys` 的 `EV_KEY` 并打印 `type/code/value`，可选 SysV 自启（默认关闭），并装入 `alientek-image-base`。

**Architecture:** 扫描 `/dev/input/event*`（`EVIOCGNAME` 匹配 `gpio-keys`）或 `-d` 指定设备；阻塞读 `struct input_event`；仅输出 `EV_KEY` 行到 stdout 或 syslog。Init 脚本以前台方式启动 `key-monitor --syslog`。

**Tech Stack:** C99、libc、`linux/input.h`、Yocto scarthgap recipe、SysV init（poky）

## Global Constraints

- 输出格式固定：`type=<t> code=<c> value=<v>`（十进制）
- 仅处理 `EV_KEY`；不引入 libevdev / systemd
- 默认不开机自启；源码注释中文；函数尽量 ≤30 行
- 设计文档：`docs/superpowers/specs/2026-09-05-key-monitor-design.md`

---

### Task 1: C 源码与 Makefile

**Files:**
- Create: `meta-alientek/recipes-apps/key-monitor/files/key-monitor.c`
- Create: `meta-alientek/recipes-apps/key-monitor/files/Makefile`

**Interfaces:**
- Produces: 可执行文件 `key-monitor`（CLI：`-d`、`-n`、`--syslog`）

- [x] **Step 1: 编写 key-monitor.c**（设备发现、事件循环、syslog/stdout、信号处理）
- [x] **Step 2: 编写 Makefile**（`CC`/`CFLAGS`/`DESTDIR`/`PREFIX` 可覆盖，支持 `make install`）
- [x] **Step 3: 宿主机 sanity** — `make -C .../files` 应无错误编译出二进制
- [ ] **Step 4: Commit** — `feat: 增加 key-monitor 源码`（待用户确认后再提交）

### Task 2: Recipe、init、镜像与 README

**Files:**
- Create: `meta-alientek/recipes-apps/key-monitor/key-monitor_1.0.bb`
- Create: `meta-alientek/recipes-apps/key-monitor/files/key-monitor.init`
- Modify: `meta-alientek/recipes-core/images/alientek-image-base.bb`
- Modify: `README.md`

**Interfaces:**
- Consumes: Task 1 源码
- Produces: 包 `key-monitor` 含 `/usr/bin/key-monitor` 与 `/etc/init.d/key-monitor`（未 enable）

- [x] **Step 1: key-monitor.init** — start/stop/status；start 为 `start-stop-daemon` 或后台 `&` 启动 `key-monitor --syslog`（记录 pidfile）
- [x] **Step 2: key-monitor_1.0.bb** — `file://` SRC_URI；`do_compile`/`do_install`；不 inherit `update-rc.d`（避免默认 enable）
- [x] **Step 3: 镜像加入 `key-monitor`；README 两行用法**
- [ ] **Step 4: Commit** — `feat: 打包 key-monitor 并装入基础镜像`（待用户确认后再提交）

### Task 3: 构建验证

- [x] **Step 1:** 宿主机 `make` 通过；Yocto `bitbake key-monitor` 留给整镜像构建时验证
- [ ] **Step 2:** 板上验收留给用户（按 KEY0 见 `type=1 code=28 value=1/0`）
