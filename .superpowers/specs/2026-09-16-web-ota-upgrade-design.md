# 板端 Web 固件升级设计

日期：2026-09-16  
状态：待实现  
相关：`webserver`、`board-apply-update`、A/B SWUpdate

## 目标

在现有板端 Web（`http://<板IP>:8080/`，传感器查询页）上增加 **`.swu` 上传与升级**：自动选择非活动 A/B 槽、更新 U-Boot 环境并 **自动重启**。不使用 SWUpdate 内置 Mongoose 网页。

## 已确认需求

| 项 | 选择 |
|---|---|
| 刷写成功后 | 自动重启（等同 `board-apply-update --reboot`） |
| 鉴权 | 无（实验室局域网开放，与传感器页一致） |
| A/B | 自动，复用现有脚本逻辑 |

## 非目标

- MQTT OTA 路径不变（`mqtt-agent` / `ota-agent`）
- SWUpdate 内置 Web UI（将关闭此前试用配置）
- 分块上传、断点续传、进度百分比推送（首版不做）
- 口令 / HTTPS

## 方案

**上传落盘 → 调用 `board-apply-update --reboot`**，不在 C 中重写 `swupdate`/`fw_setenv`。

### 用户流程

1. 打开 `http://<板IP>:8080/`，在「固件升级」区选择 `.swu`
2. 点击上传/升级；页面显示进行中
3. 服务端保存文件并执行 `board-apply-update --reboot <path>`
4. 成功则返回 JSON；设备随即重启切到新槽
5. 失败则返回错误信息，不重启

### HTTP API

- **`POST /api/upgrade`**
  - `Content-Type: multipart/form-data`
  - 文件字段名：`swu`
  - 成功：`200`，`{"ok":true,"message":"...","targetSlot":"B"}`（槽位若脚本 stdout 可解析则填，否则可省略）
  - 失败：`4xx/5xx`，`{"ok":false,"message":"..."}`
- 升级进行中再次请求：`409`，忙锁拒绝

### 服务端行为（webserver.c）

1. 解析 multipart，流式写入 `/var/tmp/web-upgrade.swu`（覆盖写）
2. 校验非空、扩展名建议 `.swu`（宽松：以内容落盘为主）
3. 进程内标志位忙锁；持锁期间拒绝新的 `/api/upgrade`
4. `fork`/`exec` 或 `system` 调用：  
   `/usr/bin/board-apply-update --reboot /var/tmp/web-upgrade.swu`  
   （路径以镜像实际安装为准）
5. 捕获退出码与 stderr 摘要写入响应；成功路径上 reboot 可能打断连接，属预期
6. 释放锁（若进程在 reboot 前仍存活）

### 前端（index.html）

- 增加升级区块：`input[type=file]` + 按钮 + 状态文案
- `fetch('/api/upgrade', { method:'POST', body: FormData })`
- 成功提示「升级成功，设备即将重启」；失败展示 `message`
- 保持现有传感器查询 UI 不变

### 配方与依赖

- `webserver_1.0.bb`：`RDEPENDS` 增加 `board-update-tools`
- 关闭 SWUpdate 内置 Web：恢复 `swupdate-minimal.cfg` 关闭 `CONFIG_WEBSERVER`/`CONFIG_MONGOOSE`；删除试用端口片段 `15-webserver-port` 及相关 bbappend 安装；README 去掉「可选 SWUpdate 内置网页」试用段，改为说明板端 Web 升级

### 错误与边界

| 情况 | 行为 |
|---|---|
| 无文件 / 空文件 | 400 |
| `upgrade_available=1` 等脚本拒绝 | 把脚本 stderr 返回 409/500 |
| NFS 根 | 脚本已允许并按 `active_slot` 选槽；行为与 CLI 一致 |
| 上传超大导致超时 | 首版接受；可后续加大客户端/服务端超时 |
| 并发升级 | 忙锁 409 |

## 测试要点

- 宿主：若有现成 webserver 单测则扩展；至少手工或脚本校验 multipart 解析边界
- 板端：上传合法 `.swu` → 非活动槽写入 → env 切槽 → 自动重启 → TF 进新槽
- 忙锁：升级中第二次 POST 返回 409
- 非法包 / 脚本失败：不重启，页面可见错误

## 实现顺序建议

1. 回退 SWUpdate Web 试用配置与 README
2. `webserver.c`：multipart + `/api/upgrade` + 忙锁
3. `index.html`：升级 UI
4. bb `RDEPENDS` + 文档同步
