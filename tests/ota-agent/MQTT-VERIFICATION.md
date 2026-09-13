# MQTT OTA 验证指南（Alientek Alpha）

面向第一次验证本仓库 `ota-agent` 的同学。  
目标：先理解 MQTT，再按**当前代码真实能力**把通路跑通。

实验室常用约定（可按现场改）：

| 角色 | 示例 |
|---|---|
| 板子 eth0 | `192.168.5.201`（ENET2 / `ethernet@20b4000`） |
| PC / HTTP / Mosquitto | `192.168.5.27` |
| 命令 topic | `device/ota/command` |
| 状态 topic | `device/ota/status` |

---

## 0. 当前能力（先看这里）

板端镜像里的 `ota-agent` 已用 **Eclipse Paho MQTT C**（Yocto 包 `paho-mqtt-c`，链接 `-lpaho-mqtt3c`）对接真实 broker。

| 能力 | 状态 |
|---|---|
| 连接 broker / 订阅命令 / 发布状态 | 已实现（板端 `OTA_MQTT_BACKEND=paho`） |
| daemon 收 MQTT 后自动走升级流水线 | 已实现（`yield` + `popCommand`） |
| 断线后台重连并重新订阅 | 已实现 |
| `--mqtt-command` 注入同一套业务逻辑 | 可用（不依赖 broker） |
| 宿主机单测 | 可用（`OTA_MQTT_BACKEND=stub`，无需装 paho） |

> 若板上仍提示 `MQTT 接缝尚不可用` / `ENOSYS`，说明镜像偏旧，需包含提交 `feat: wire ota-agent MQTT transport via paho-mqtt-c` 及之后的构建。

建议验证顺序：

1. 宿主机脚本（不插板）  
2. 板上 `--mqtt-command`（测下载/升级业务）  
3. Mosquitto 真 MQTT 联调（测收发包闭环）

---

## 1. MQTT 是什么（零基础）

### 1.1 一句话

设备不互相直连，都连到 **Broker**；通过 **Topic（主题）** 收发消息。

```text
[发布者] --publish--> [Broker] --push--> [订阅者]
```

- **Broker**：消息中转站（如 Mosquitto），默认端口 `1883`
- **Publisher**：发消息的一方（PC / 云端）
- **Subscriber**：收消息的一方（板上 `ota-agent`）

同一个程序可以既发又收。

### 1.2 本仓库 Topic

| 用途 | Topic | 谁发 / 谁收 |
|---|---|---|
| 下发升级命令 | `device/ota/command` | PC/云端 → 板子 |
| 上报升级状态 | `device/ota/status` | 板子 → PC/云端 |

板端配置：`/etc/default/ota-agent`

```bash
export OTA_MQTT_HOST=192.168.5.27          # 改成跑 Mosquitto 的 PC IP
export OTA_MQTT_PORT=1883
export OTA_MQTT_CLIENT_ID=ota-agent
export OTA_MQTT_COMMAND_TOPIC=device/ota/command
export OTA_MQTT_STATUS_TOPIC=device/ota/status
export OTA_DOWNLOAD_PATH=/var/tmp/ota-download.swu
```

默认文件里 `OTA_MQTT_HOST` 是 `127.0.0.1`（只适合 broker 跑在板子本机）。实验室联调请改成 PC IP 并保存；`ota-agent` 启动时会自动加载该文件（已在环境中的变量不会被覆盖）。也可用 `/etc/init.d/ota-agent restart` 启动服务。

### 1.3 命令 JSON

```json
{
  "requestId": "req-1001",
  "version": "2.0.0",
  "url": "http://192.168.5.27/updates/app.swu",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "autoReboot": false
}
```

| 字段 | 含义 |
|---|---|
| `requestId` | 任务唯一 ID；重复会拒绝 |
| `version` | 目标版本；不高于当前 `VERSION_ID` 会拒绝 |
| `url` | `.swu` 下载地址（板子要能访问） |
| `sha256` | 64 位十六进制校验和 |
| `autoReboot` | `true` / `false`，是否升级后自动重启 |

### 1.4 状态 JSON 与常见 phase

```json
{
  "requestId": "req-1001",
  "phase": "accepted",
  "result": "running",
  "detail": "命令已解析"
}
```

| phase（常见） | 含义 |
|---|---|
| `accepted` | 命令已解析，开始执行 |
| `completed` | 本次命令流水线执行成功（下载/刷写调用完成） |
| `failed` | 解析失败、版本拒绝、下载失败、忙锁等 |
| `committed` | 重启后确认新槽位提交成功（恢复逻辑） |

`emitMqttStatus` 会：

1. 尽量 `otaMqttPublishStatus` 发到 status topic  
2. 同时 `puts` 打到 stdout（串口/前台日志好观察）

---

## 2. 端到端流程

```text
PC mosquitto_pub --> device/ota/command
                         |
                         v
                    ota-agent 解析 JSON
                         |
                         v
              下载 .swu → sha256 → board-apply-update
                         |
                         v
                   A/B 切槽 /（可选）重启
                         |
                         v
PC mosquitto_sub <-- device/ota/status
```

不经 broker 时，同一业务可用：

```bash
ota-agent --mqtt-command '<JSON>' /tmp/download.swu
```

---

## 3. 路径 A：宿主机测试（推荐先做）

开发机仓库根目录（脚本未必带执行位，可用 `sh`）：

```bash
cd /path/to/alientek
sh tests/ota-agent/test_ota_recipe.sh
sh tests/ota-agent/test_ota_cpp_build.sh
sh tests/ota-agent/test_ota_guard.sh
sh tests/ota-agent/test_ota_recovery.sh
sh tests/ota-agent/test_service_lifecycle.sh
```

需要：`g++`、`make`。  
宿主测试强制/默认 **stub** 后端，**不必**在 PC 上安装 paho。

---

## 4. 路径 B：板上 `--mqtt-command`（模拟已收到 MQTT）

### 4.1 前提

- 板子已进系统（TF / NFS）
- 镜像含新版 `ota-agent` + `paho-mqtt-c`
- 能串口或 SSH
- 若要真下载：PC 提供 HTTP（或板子本地文件路径方案）

检查：

```bash
/etc/init.d/ota-agent status
ls -l /usr/bin/ota-agent
cat /etc/default/ota-agent
# 可选：确认链了 paho
ldd /usr/bin/ota-agent | grep -i paho
```

前台观察（先停后台，避免抢锁）：

```bash
/etc/init.d/ota-agent stop
ota-agent
# 预期首行：ota-agent foreground mode
# broker 不可达时：MQTT 连接失败（将后台重试）: ...
# 这不影响 --mqtt-command；联调 MQTT 时再开 broker 并改 HOST
```

> `--mqtt-command` 与 daemon 共用锁 `/var/run/ota-agent.lock`。一边升级时另一边会报 `ota-agent busy`。测注入时建议先 `stop` 服务，或确认 daemon 空闲。

### 4.2 准备包与 SHA256

```bash
# 在 PC 上
sha256sum demo.swu
# 放到板子可访问的 HTTP，例如 http://192.168.5.27/updates/demo.swu
```

### 4.3 注入一条命令

```bash
# 在板子上；请替换 sha256 / version / url
CMD='{"requestId":"req-demo-001","version":"9.9.9","url":"http://192.168.5.27/updates/demo.swu","sha256":"这里填64位sha256","autoReboot":false}'

ota-agent --mqtt-command "$CMD" /tmp/ota-download.swu
echo "exit=$?"
cat /var/lib/ota-agent/state.json
```

观察：

1. stdout 是否出现含 `"phase":"accepted"` / `"completed"` / `"failed"` 的 JSON  
2. `state.json` 是否更新  
3. URL 可达时，`/tmp/ota-download.swu` 是否落盘且校验通过  

查看当前系统版本（用于构造合法 `version`）：

```bash
grep VERSION_ID /etc/os-release
```

### 4.4 失败用例（建议都做）

**重复 requestId：**

```bash
ota-agent --mqtt-command "$CMD" /tmp/ota-download.swu
# 预期失败，detail 含：重复 requestId
```

**版本不高于当前：**

```bash
CMD2='{"requestId":"req-demo-002","version":"0.0.1","url":"http://192.168.5.27/updates/demo.swu","sha256":"这里填同一份64位sha256","autoReboot":false}'
ota-agent --mqtt-command "$CMD2" /tmp/ota-download.swu
# 预期：目标版本不高于当前版本，拒绝执行
```

**坏 JSON：**

```bash
ota-agent --mqtt-command '{"requestId":"x"}' /tmp/x.swu
# 预期：退出码 2
```

### 4.5 真刷写注意

成功路径仍会：下载 → sha256 → `board-apply-update` →（`autoReboot`）重启 → 启动恢复回报 `committed`/`failed`。

真刷写前确认：A/B 分区正常、`.swu` 为本板产物、有串口兜底。

---

## 5. 路径 C：真 MQTT 联调（Mosquitto）

### 5.1 PC 安装并允许局域网连接

```bash
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

默认许多发行版只监听本机。要让板子连上，增加配置，例如：

```bash
# /etc/mosquitto/conf.d/lab.conf  （路径以发行版为准）
listener 1883 0.0.0.0
allow_anonymous true
```

然后：

```bash
sudo systemctl restart mosquitto
ss -lntp | grep 1883
# 本机自测
mosquitto_sub -h 127.0.0.1 -t 'device/ota/status' -v &
mosquitto_pub -h 127.0.0.1 -t 'device/ota/status' -m '{"ping":1}'
```

防火墙若开启，放行 TCP `1883`。

### 5.2 板端配置并重启服务

```bash
# /etc/default/ota-agent
export OTA_MQTT_HOST=192.168.5.27
export OTA_MQTT_PORT=1883
export OTA_DOWNLOAD_PATH=/var/tmp/ota-download.swu

/etc/init.d/ota-agent restart
/etc/init.d/ota-agent status
```

前台确认（可选）：

```bash
/etc/init.d/ota-agent stop
ota-agent
# 配置正确且 broker 可达时，不应再刷「连接失败」；
# 偶发网络抖动会打印断开并后台重连
```

### 5.3 PC：订状态、发命令

终端 1：

```bash
mosquitto_sub -h 127.0.0.1 -t 'device/ota/status' -v
```

终端 2（每次换新的 `requestId`，`version` 高于板上 `VERSION_ID`）：

```bash
mosquitto_pub -h 127.0.0.1 -t 'device/ota/command' -m \
'{"requestId":"req-live-1","version":"9.9.9","url":"http://192.168.5.27/updates/demo.swu","sha256":"这里填64位sha256","autoReboot":false}'
```

预期：

1. 终端 1 陆续看到 `accepted` → `completed` 或 `failed`  
2. URL 可达时，板上 `OTA_DOWNLOAD_PATH`（默认 `/var/tmp/ota-download.swu`）出现文件  
3. 板子串口/前台也能看到同样的状态 JSON  

daemon 收到命令后落盘路径来自 **`OTA_DOWNLOAD_PATH`**，不是 `--mqtt-command` 的第二个参数。

---

## 6. 常用路径与命令

| 路径/命令 | 作用 |
|---|---|
| `/usr/bin/ota-agent` | OTA 主程序 |
| `/etc/default/ota-agent` | MQTT / 下载路径配置 |
| `/etc/init.d/ota-agent` | 启停服务 |
| `/var/lib/ota-agent/state.json` | 任务状态 |
| `/var/run/ota-agent.lock` | 防并发锁 |
| `ota-agent --mqtt-command '<json>' <落盘路径>` | 模拟收到命令 |
| `ota-agent --apply <url> <sha256> <path> [--reboot]` | 直接跑升级流水线 |
| `tests/ota-agent/*` | 宿主机自动化测试 |

相关源码：

- `meta-alientek/recipes-core/ota-agent/files/src/ota-mqtt.cpp`（paho / stub）  
- `meta-alientek/recipes-core/ota-agent/files/src/ota-agent.cpp`（主循环与命令执行）  
- `meta-alientek/recipes-core/ota-agent/ota-agent_1.0.bb`（`DEPENDS`/`RDEPENDS` + `OTA_MQTT_BACKEND=paho`）

---

## 7. 验收清单

### 概念

- [ ] 能说清 Broker / Topic / Publish / Subscribe  
- [ ] 知道两个默认 topic 与 `OTA_MQTT_HOST` 要指向 PC  

### 实现

- [ ] 板端 paho；宿主测试 stub  
- [ ] `--mqtt-command` 与真 MQTT 共用同一业务流水线  
- [ ] daemon 落盘用 `OTA_DOWNLOAD_PATH`  

### 宿主机

- [ ] `tests/ota-agent` 相关脚本通过  

### 板端

- [ ] `ldd` 能看到 paho（新镜像）  
- [ ] `--mqtt-command` 合法 JSON 有状态输出 / `state.json`  
- [ ] 重复 `requestId`、过低 `version`、坏 JSON 行为符合预期  
- [ ] Mosquitto：`pub` 命令后 status topic 有回报  

---

## 8. 常见问题

**Q: 启动一直 “MQTT 连接失败（将后台重试）”？**  
A: 检查 `OTA_MQTT_HOST`、PC 上 Mosquitto 是否监听 `0.0.0.0:1883`、板子能否 `ping` PC、防火墙是否放行。daemon 会继续跑并重连；业务仍可用 `--mqtt-command`。

**Q: PC 上 `mosquitto_pub` 了，板子没反应？**  
A: 逐项查：镜像是否含 paho 版 agent、服务是否在跑、topic 是否一致、broker 是否允许远程、板子到 PC:1883 是否通。可在 PC 上用另一终端 `mosquitto_sub` 同一 command topic 自证消息是否进了 broker。

**Q: 状态发到哪了？**  
A: 已连接时发到 `OTA_MQTT_STATUS_TOPIC`；同时 stdout 仍会打印。stub / publish 失败且 `ENOSYS` 时，部分最终态会退回 `puts`。

**Q: 宿主机构建要装 paho 吗？**  
A: 不必。只有 Yocto 镜像构建链接真实库。

**Q: `requestId` 能重复吗？**  
A: 不能；重复会拒绝（防重放）。

**Q: 为什么 MQTT 触发的文件不在我指定的 `/tmp/xxx.swu`？**  
A: 只有 `--mqtt-command` 的第二个参数指定落盘路径；daemon 收包使用 `OTA_DOWNLOAD_PATH`。

---

## 9. 实现备注

| 项 | 说明 |
|---|---|
| 客户端库 | `paho-mqtt-c`（`meta-oe`，kas 已含） |
| 板端后端 | `EXTRA_OEMAKE = "OTA_MQTT_BACKEND=paho"` |
| 宿主后端 | Makefile 默认 `stub` |
| QoS | 命令订阅 / 状态发布使用 QoS 1 |
| 命令缓冲 | 单槽；上一命令未处理完时新命令会丢弃并打日志 |

文档版本：与 paho MQTT 传输、stub 双后端及 `OTA_DOWNLOAD_PATH` 行为对齐。  
本地说明副本（若存在）：`docs/mqtt-ota-beginner-verification-guide.md`（`docs/` 通常被 gitignore）。
