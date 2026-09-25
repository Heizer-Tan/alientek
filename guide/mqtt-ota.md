# MQTT OTA

零基础验证步骤见：`tests/ota-agent/MQTT-VERIFICATION.md`。

板端拆成两个进程：

- **`mqtt-agent`**（常驻）：用 `paho-mqtt-c` 连接/探测 broker、心跳、订阅 `device/ota/command`；收到命令后 `exec`/`popen` `ota-agent --mqtt-command`，并把 stdout 状态 JSON 再发布到 `device/ota/status`。
- **`ota-agent`**（短命）：无参只做 A/B 恢复后退出；`--mqtt-command` / `--apply` 跑下载校验与 `board-apply-update`。不再常驻 MQTT。

配置：`/etc/default/mqtt-agent`（`OTA_MQTT_HOST=auto` 可扫 eth0 网段 `1883`）、`/etc/default/ota-agent`（状态路径等）。启停：`/etc/init.d/mqtt-agent`。宿主测试默认 stub，可不装 paho。

底层切槽逻辑见 [ota-swupdate.md](./ota-swupdate.md)。
