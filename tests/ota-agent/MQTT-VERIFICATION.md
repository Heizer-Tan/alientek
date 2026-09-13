# MQTT OTA 零基础验证指南（Alientek Alpha）

面向：不熟悉 MQTT、第一次验证本仓库 `ota-agent` 的同学。  
目标：先搞懂 MQTT 是什么，再按本仓库**今天真实能测的路径**做完验证。

---

## 0. 先说结论（很重要）

板端镜像里的 `ota-agent` 已通过 **Eclipse Paho MQTT C**（`paho-mqtt-c` / `-lpaho-mqtt3c`）对接真实 broker：

| 接口 | 行为 |
|---|---|
| `otaMqttConnect` | 连接 `tcp://HOST:PORT`，失败时保留客户端并后台重试 |
| `otaMqttSubscribeCommand` | 订阅命令 topic（默认 `device/ota/command`） |
| `otaMqttPublishStatus` | 向状态 topic 发布 JSON（默认 `device/ota/status`） |
| `otaMqttYield` + `otaMqttPopCommand` | daemon 主循环收包并执行升级流水线 |

因此：

| 验证方式 | 是否可用 |
|---|---|
| PC 装 Mosquitto，往板子发 MQTT 命令，板子自动升级 | **可以**（配置好 `OTA_MQTT_HOST`） |
| `--mqtt-command` 注入 JSON | **可以**（不依赖 broker，方便单测业务） |
| 宿主机单测（JSON / 状态机） | **可以**（编译用 `OTA_MQTT_BACKEND=stub`） |

宿主机默认 Makefile 后端是 **stub**（无 paho 依赖）；Yocto 配方强制 `OTA_MQTT_BACKEND=paho`。

---

## 1. MQTT 是什么（零基础）

### 1.1 一句话

MQTT 是一种**轻量消息协议**：设备之间不直接打电话，而是都连到一台叫 **Broker（代理/消息中间件）** 的服务器，通过 **Topic（主题）** 收发消息。

### 1.2 三个角色

```text
[发布者 Publisher] --发布到 topic--> [Broker] --推送给--> [订阅者 Subscriber]
```

- **Broker**：消息中转站。常见软件：Mosquitto、EMQX。默认端口 `1883`。
- **Publisher**：往某个 topic 发消息的人（例如云端 OTA 平台）。
- **Subscriber**：订阅某个 topic、被动收消息的人（例如板上的 `ota-agent`）。

同一个程序既可以发也可以收。

### 1.3 Topic 像“频道名”

本仓库默认：

| 用途 | Topic | 谁发 / 谁收 |
|---|---|---|
| 下发升级命令 | `device/ota/command` | 云端发，板子收 |
| 上报升级状态 | `device/ota/status` | 板子发，云端收 |

配置在板子：`/etc/default/ota-agent`

```bash
OTA_MQTT_HOST=127.0.0.1
OTA_MQTT_PORT=1883
OTA_MQTT_CLIENT_ID=ota-agent
OTA_MQTT_COMMAND_TOPIC=device/ota/command
OTA_MQTT_STATUS_TOPIC=device/ota/status
OTA_DOWNLOAD_PATH=/var/tmp/ota-download.swu
```

实验室请把 `OTA_MQTT_HOST` 改成跑 Mosquitto 的 PC IP（例如 `192.168.5.27`）。

### 1.4 消息内容是什么

MQTT 本身不管业务格式。本仓库约定命令是一段 **JSON 文本**，例如：

```json
{
  "requestId": "req-1001",
  "version": "2.0.0",
  "url": "http://192.168.5.27/updates/app.swu",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "autoReboot": true
}
```

字段含义：

| 字段 | 含义 |
|---|---|
| `requestId` | 本次任务唯一 ID；重复会拒绝 |
| `version` | 目标版本；不高于当前版本会拒绝 |
| `url` | `.swu` 下载地址 |
| `sha256` | 包校验和（64 位十六进制） |
| `autoReboot` | 是否自动重启（`true`/`false`） |

状态回报 JSON 大致为：

```json
{
  "requestId": "req-1001",
  "phase": "completed",
  "result": "success",
  "detail": "升级命令已执行"
}
```

---

## 2. 本仓库 OTA 流程（对照 MQTT）

完整闭环：

```text
云端 --MQTT publish--> device/ota/command
                           |
                           v
                      ota-agent 解析 JSON
                           |
                           v
                 下载 .swu → sha256 校验 → board-apply-update
                           |
                           v
                      写 A/B 槽 / 重启
                           |
                           v
云端 <--MQTT publish-- device/ota/status
```

不经过 broker 时，仍可用命令行注入同一条 JSON：

```bash
ota-agent --mqtt-command '<JSON字符串>' /tmp/download.swu
```

这等价于：**跳过无线收包，直接测业务逻辑**。

---

## 3. 验证路径总览

建议按顺序做：

1. **宿主机单元测试**（不插板也能跑）— 确认解析/状态机逻辑  
2. **板上 `--mqtt-command` 注入** — 确认下载与升级调用  
3. **真 broker 联调**（Mosquitto）— 确认 MQTT 收发闭环  

---

## 4. 路径 A：宿主机跑测试（推荐先做）

在开发机仓库根目录：

```bash
./tests/ota-agent/test_ota_recipe.sh
./tests/ota-agent/test_ota_cpp_build.sh
./tests/ota-agent/test_ota_guard.sh
./tests/ota-agent/test_ota_recovery.sh
./tests/ota-agent/test_service_lifecycle.sh
```

需要本机有 `g++` / `make`。宿主机构建使用 `OTA_MQTT_BACKEND=stub`，无需安装 paho。

---

## 5. 路径 B：板上验证（模拟 MQTT 命令）

### 5.1 前提

- 板子已能进系统（TF 或 NFS）
- 镜像里有 `ota-agent`（`alientek-image-base` 已安装）
- 能 SSH 或串口登录
- 若要真下载 `.swu`，PC 上要能提供 HTTP/文件 URL

查看服务：

```bash
/etc/init.d/ota-agent status
ls -l /usr/bin/ota-agent
cat /etc/default/ota-agent
```

前台看日志（可选，停掉后台服务再跑）：

```bash
/etc/init.d/ota-agent stop
ota-agent
# 预期：
# ota-agent foreground mode
# 若 broker 不可达：MQTT 连接失败（将后台重试）: ...
# 若 broker 可达且配置正确：无连接失败日志，可继续发命令
```

### 5.2 准备一个最小 `.swu` 与 SHA256

实验室若已有正式 `.swu`：

```bash
# 在 PC 上
sha256sum your.swu
# 把文件放到板子能访问的 HTTP，或 scp 到板子本地
```

### 5.3 用 `--mqtt-command` 注入一条命令

在板子上（示例，请改 URL / sha256 / version）：

```bash
mkdir -p /tmp
CMD='{"requestId":"req-demo-001","version":"9.9.9","url":"http://192.168.5.27/updates/demo.swu","sha256":"这里填64位sha256","autoReboot":false}'

ota-agent --mqtt-command "$CMD" /tmp/ota-download.swu
echo "exit=$?"
```

观察：

1. **标准输出**里是否出现状态 JSON（`emitMqttStatus` 仍会 `puts`，便于串口观察），例如含 `"phase":"accepted"` / `"completed"` / `"failed"`  
2. 若 MQTT 已连接，同一状态也会发到 `device/ota/status`  
3. `/var/lib/ota-agent/state.json` 是否更新  
4. 若下载成功：落盘文件是否存在且校验通过  

查看状态文件：

```bash
cat /var/lib/ota-agent/state.json
```

### 5.4 故意测失败用例（建议都做）

**重复 requestId：**

```bash
ota-agent --mqtt-command "$CMD" /tmp/ota-download.swu
# 预期失败，detail 类似：重复 requestId
```

**版本不高于当前：**

```bash
CMD2='{"requestId":"req-demo-002","version":"0.0.1",...}'
ota-agent --mqtt-command "$CMD2" /tmp/ota-download.swu
```

**坏 JSON：**

```bash
ota-agent --mqtt-command '{"requestId":"x"}' /tmp/x.swu
# 预期：退出码 2，JSON 无效
```

### 5.5 和真实升级的关系

成功后仍会走：下载 → sha256 → `board-apply-update` → 按 `autoReboot` 重启 → 启动恢复判定 committed/failed。

**真刷写前请确认 A/B 分区、`.swu` 为本板产物、有串口兜底。**

---

## 6. 路径 C：真 MQTT 联调（Mosquitto）

### 6.1 PC 上装 Broker

```bash
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

确认监听（默认允许局域网连接时，请按需改 `/etc/mosquitto/` 监听与 ACL）：

```bash
ss -lntp | grep 1883
```

### 6.2 改板配置

编辑 `/etc/default/ota-agent`：

```bash
export OTA_MQTT_HOST=192.168.5.27   # PC 的 IP
export OTA_MQTT_PORT=1883
export OTA_DOWNLOAD_PATH=/var/tmp/ota-download.swu
```

重启 agent：

```bash
/etc/init.d/ota-agent restart
```

### 6.3 PC 订阅状态、发布命令

终端 1（看板子回报）：

```bash
mosquitto_sub -h 127.0.0.1 -t 'device/ota/status' -v
```

终端 2（发升级命令）：

```bash
mosquitto_pub -h 127.0.0.1 -t 'device/ota/command' -m '{"requestId":"req-live-1","version":"9.9.9","url":"http://192.168.5.27/updates/demo.swu","sha256":"...","autoReboot":false}'
```

预期：终端 1 陆续看到 `accepted` → `completed`/`failed` 等状态；板上 `OTA_DOWNLOAD_PATH` 出现下载文件（若 URL 可达）。

---

## 7. 常用文件与命令速查

| 路径/命令 | 作用 |
|---|---|
| `/usr/bin/ota-agent` | OTA 主程序 |
| `/etc/default/ota-agent` | MQTT 主机/端口/topic/下载路径 |
| `/etc/init.d/ota-agent` | 启停服务 |
| `/var/lib/ota-agent/state.json` | 任务状态 |
| `/var/run/ota-agent.lock` | 防并发锁 |
| `ota-agent --mqtt-command '<json>' <下载落盘路径>` | 模拟收到 MQTT 命令 |
| `ota-agent --apply <url> <sha256> <path> [--reboot]` | 直接执行升级流水线 |
| `tests/ota-agent/*` | 宿主机自动化验证 |

---

## 8. 验收清单

### 概念

- [ ] 能说清 Broker / Topic / Publish / Subscribe  
- [ ] 知道本仓库两个默认 topic  

### 实现认知

- [ ] 知道板端用 paho；宿主测试用 stub  
- [ ] 知道 `--mqtt-command` 与真 MQTT 共用同一套业务流水线  

### 宿主机

- [ ] `tests/ota-agent` 相关脚本通过  

### 板端

- [ ] `ota-agent status` 可查  
- [ ] `--mqtt-command` 合法 JSON 有状态输出 / state 文件  
- [ ] 重复 `requestId` / 过低 `version` / 坏 JSON 行为符合预期  
- [ ] Mosquitto 联调：pub 命令后 status topic 有回报  

---

## 9. 常见问题

**Q: 启动提示 MQTT 连接失败（将后台重试）？**  
A: broker 未开、IP/端口不对、或防火墙拦了 `1883`。daemon 会继续跑并周期性重连；业务仍可用 `--mqtt-command` 验证。

**Q: 状态发到哪了？**  
A: 已连接时发到 `OTA_MQTT_STATUS_TOPIC`；同时 `emitMqttStatus` 仍会打印到 stdout，方便串口观察。若 publish 失败且 `errno==ENOSYS`（stub），最终态也会 `puts`。

**Q: 宿主机构建要装 paho 吗？**  
A: 不必。测试脚本使用 `OTA_MQTT_BACKEND=stub`。只有 Yocto 镜像构建会链接真实库。

**Q: `requestId` 能不能每次都一样？**  
A: 不能。重复会被拒绝，这是防重放设计。

---

## 10. 实现备注

- 客户端库：`paho-mqtt-c`（meta-oe，kas 已含 `meta-oe`）  
- 配方：`DEPENDS` / `RDEPENDS` + `EXTRA_OEMAKE = "OTA_MQTT_BACKEND=paho"`  
- 代码：`meta-alientek/recipes-core/ota-agent/files/src/ota-mqtt.cpp`  

文档版本：与 paho MQTT 传输及 stub 双后端对齐。  
相关代码：`meta-alientek/recipes-core/ota-agent/`，测试：`tests/ota-agent/`。
