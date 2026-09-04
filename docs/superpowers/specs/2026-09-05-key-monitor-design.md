# key-monitor 按键读取应用设计

日期：2026-09-05  
状态：已评审（对话确认）  
范围：应用层只读按键事件，接入 `alientek-image-base`

## 1. 背景与目标

阿尔法板设备树已用主线 `gpio-keys` 将 KEY0（GPIO1_IO18）上报为 `KEY_ENTER`，板上可用 `/dev/input/event*` 验证。镜像中尚无用户态按键演示程序。

**目标**：提供一个 C 程序 `key-monitor`，读取 `EV_KEY` 事件并打印 `type`、`code`、`value` 三个字段；支持前台手动运行，以及可选 SysV 自启（默认关闭）。

**非目标**：LED 联动、外部脚本钩子、libevdev、自写内核按键驱动、systemd。

## 2. 架构

```text
gpio-keys (DT) → /dev/input/eventN → key-monitor → stdout 或 syslog
```

| 组件 | 路径 | 职责 |
|------|------|------|
| 源码 | `meta-alientek/recipes-apps/key-monitor/files/key-monitor.c` | 设备发现、事件循环、输出 |
| 构建 | `files/Makefile` + `key-monitor_1.0.bb` | 交叉编译并安装 |
| 可选服务 | `files/key-monitor.init` | SysV 脚本，启动 `key-monitor --syslog` |
| 镜像 | `alientek-image-base.bb` | `CORE_IMAGE_EXTRA_INSTALL` 加入 `key-monitor` |

依赖：仅 libc + Linux `linux/input.h` ioctl；不引入额外库。

## 3. CLI 与行为

```text
key-monitor                 # 按名称匹配 gpio-keys，前台打印
key-monitor -d /dev/input/eventN
key-monitor -n <substr>     # 默认 -n gpio-keys
key-monitor --syslog        # 写 syslog，服务模式使用
```

**设备发现**：扫描 `/dev/input/event*`，`EVIOCGNAME` 名称含子串则选用；`-d` 优先于自动扫描。

**输出（一行一事）**：打印 `struct input_event` 的三个字段（十进制），格式固定为：

```text
type=<type> code=<code> value=<value>
```

示例（KEY0 → `KEY_ENTER=28`，按下 `value=1`、松开 `value=0`）：

```text
type=1 code=28 value=1
type=1 code=28 value=0
```

仅处理 `type == EV_KEY`（`type=1`）；忽略同步与其它事件。  
不强制打印键名字符串；需要时可用 `code` 对照 `linux/input-event-codes.h`（`KEY_ENTER=28`）。

**信号处理**：`SIGINT` / `SIGTERM` 关闭 fd 后以退出码 `0` 退出。

**退出码**：

| 码 | 含义 |
|----|------|
| 0 | 正常退出（信号） |
| 1 | 找不到设备、open/read 失败、参数错误 |

进程不自行 daemonize；由 init 以前台方式拉起。

## 4. 可选自启（SysV）

发行版为 `poky`（默认 SysV，不用 systemd）。

- 安装 `/etc/init.d/key-monitor`，启动命令：`key-monitor --syslog`
- **默认不开机启动**（不执行 `update-rc.d` 启用，或等价于 disable）
- 用户需要时：

```bash
update-rc.d key-monitor defaults
/etc/init.d/key-monitor start
```

## 5. Yocto 打包

```text
meta-alientek/recipes-apps/key-monitor/
  key-monitor_1.0.bb
  files/
    key-monitor.c
    Makefile
    key-monitor.init
```

Recipe 要点：

- `SRC_URI` 使用 `file://` 本地源
- 安装二进制到 `${bindir}/key-monitor`
- 安装 init 脚本；不自动 enable
- `LICENSE` / `LIC_FILES_CHKSUM` 按层惯例（MIT，源文件 SPDX 头）
- `alientek-image-base` 安装该包
- README 补充前台用法与可选自启两行

## 6. 验收标准

1. rootfs 存在 `/usr/bin/key-monitor`
2. 串口执行 `key-monitor`，按 KEY0 出现 `type=1 code=28 value=1` 与 `type=1 code=28 value=0`
3. `key-monitor --syslog` 时系统日志可见同等内容
4. 未手动 `update-rc.d` 时开机不自动运行
5. 本机构建可 `bitbake key-monitor` 通过（不必强制整镜像 CI）

## 7. 实现顺序（概要）

1. 落地 C 源码与 Makefile  
2. 写 recipe 与 init 脚本  
3. 加入镜像与 README  
4. `bitbake key-monitor` 验证编译  

详细步骤见后续 implementation plan。
