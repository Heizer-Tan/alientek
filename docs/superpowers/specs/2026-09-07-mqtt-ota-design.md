# MQTT OTA 方案设计

## 1. 背景

当前项目已经具备以下本地升级能力：

- 基于 `SWUpdate` 的单文件 `.swu` 升级包
- A/B 根文件系统槽位切换
- `board-apply-update` 板端升级执行脚本
- `board-upgrade-commit` 首启提交机制
- U-Boot `bootcount` / `bootlimit` 回滚基础

现阶段要解决的问题不是“如何重新设计升级机制”，而是如何在现有本地升级链路上，增加一层适合实验室局域网场景的远程 OTA 编排能力。

## 2. 目标

本方案目标如下：

- 保留现有 `SWUpdate + A/B + .swu` 升级链路
- 增加基于 MQTT 的远程升级命令下发
- 通过 HTTP 分发 `.swu` 升级包
- 在板端完成下载、校验、安装、重启、提交、回报状态的闭环
- 第一版以少量设备、局域网环境、实验室可用为目标
- 第一版只要求 `SHA256` 完整性校验

## 3. 非目标

第一版不包含以下能力：

- 公网部署与跨地域升级调度
- 完整的云端 OTA 平台
- 升级包签名校验
- 增量升级或差分升级
- 复杂灰度发布策略
- 数据分区迁移编排

## 4. 总体架构

方案采用“四层结构”：

1. 升级包生产层  
   继续使用 Yocto 构建并产出 `.swu`，不改变当前升级包主线。

2. 文件分发层  
   使用局域网 HTTP 服务器提供升级包、校验值和可选版本清单。

3. 控制编排层  
   使用 MQTT broker 下发升级命令，并接收板端状态回报。

4. 板端执行层  
   新增一个板端常驻进程 `ota-agent`，负责命令接收、下载、校验、执行升级和状态恢复。

该结构的核心原则是：远程 OTA 只增加“入口”和“调度”，不替换当前已经验证过的板端安装链路。

## 5. 板端组件设计

板端新增 `ota-agent`，建议按职责拆成以下模块：

### 5.1 `mqttClient`

职责：

- 连接 MQTT broker
- 订阅 OTA 命令主题
- 发布 OTA 状态主题
- 维护连接状态

### 5.2 `commandValidator`

职责：

- 解析 OTA 命令 JSON
- 校验字段完整性与格式
- 校验 `requestId` 是否重复
- 检查当前系统是否允许升级

### 5.3 `downloadManager`

职责：

- 使用系统里的 `curl` 下载 `.swu`
- 将文件保存到固定缓存目录
- 调用 `sha256sum` 做完整性校验
- 对下载失败做有限次重试

### 5.4 `upgradeExecutor`

职责：

- 调用现有 `board-apply-update`
- 根据命令决定是否追加 `--reboot`
- 统一采集执行输出和退出码

### 5.5 `stateStore`

职责：

- 保存当前 OTA 任务状态
- 保存最近执行的 `requestId`
- 保存目标版本、目标槽位、自动重启配置和错误原因
- 支持重启后恢复升级状态

### 5.6 `reporter`

职责：

- 将板端执行阶段转换为统一状态消息
- 对外提供稳定的 MQTT 状态输出

## 6. MQTT 协议设计

### 6.1 主题设计

第一版建议只做单设备主题：

- 命令主题：`alientek/<deviceId>/ota/cmd`
- 状态主题：`alientek/<deviceId>/ota/status`

后续如需扩展多设备广播，可增加：

- `alientek/group/<groupName>/ota/cmd`

### 6.2 下发命令格式

```json
{
  "requestId": "20260907-001",
  "version": "5.0.20",
  "url": "http://192.168.5.27/ota/alientek-image-update-imx6ull-alientek-alpha.swu",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "autoReboot": true
}
```

字段要求：

- `requestId`：唯一任务 ID，用于去重与追踪
- `version`：目标版本号，用于拒绝重复升级
- `url`：HTTP 下载地址
- `sha256`：64 位十六进制字符串
- `autoReboot`：是否在升级成功后自动重启

### 6.3 状态上报格式

```json
{
  "requestId": "20260907-001",
  "deviceId": "imx6ull-alpha-001",
  "phase": "installing",
  "result": "running",
  "version": "5.0.20",
  "detail": "board-apply-update started",
  "timestamp": "2026-09-07T23:10:00Z"
}
```

状态约定：

- `phase`：`idle` / `received` / `downloading` / `verifying` / `installing` / `rebooting` / `committed` / `failed`
- `result`：`running` / `success` / `error`

## 7. 板端状态机

第一版状态机采用单任务、线性执行模型：

1. `idle`
2. 收到命令后进入 `received`
3. 下载升级包进入 `downloading`
4. 校验哈希进入 `verifying`
5. 执行 `board-apply-update` 进入 `installing`
6. 若启用自动重启则进入 `rebooting`
7. 新系统首启提交成功后进入 `committed`
8. 任意阶段失败则进入 `failed`

最终成功判定必须延续到首启提交完成，不能只以 `swupdate` 返回成功作为升级完成标志。

## 8. 与现有升级链路的衔接

远程 OTA 只在现有能力外层增加调用流程：

1. `ota-agent` 收到 MQTT 命令
2. 下载 `.swu`
3. 校验 `SHA256`
4. 调用 `board-apply-update` 或 `board-apply-update --reboot`
5. 重启后由 `board-upgrade-commit` 完成新槽位提交
6. `ota-agent` 在启动后恢复状态并回报最终结果

因此，远程 OTA 和手工板端升级共享同一条底层升级路径，便于复用既有验证结果，也便于排障。

## 9. 幂等与防误操作设计

### 9.1 防重入

同一时间只允许一个 OTA 任务执行。  
`ota-agent` 启动升级前必须创建锁文件，若检测到已有任务在跑，则拒绝新的 OTA 命令。

### 9.2 防重复请求

`stateStore` 记录最近一次执行中的 `requestId` 和成功的 `requestId`。  
收到相同 `requestId` 时，只返回当前状态，不重复下载和安装。

### 9.3 防“未重启再次升级”

复用当前 `board-apply-update` 中已加入的保护：

- 当 `upgrade_available=1` 时拒绝执行新升级
- 当当前实际启动槽位与 `active_slot` 不一致时拒绝执行

这样可以避免“第一次升级后未重启就执行第二次升级，结果覆盖当前正在运行槽位”的问题。

### 9.4 防无意义升级

第一版建议默认拒绝以下情况：

- 目标版本等于当前版本
- 目标版本低于当前版本

如需回退升级，可作为后续显式放开的扩展能力。

## 10. 下载与缓存设计

推荐目录：

- 下载缓存：`/var/cache/ota/`
- 状态文件：`/var/lib/ota/state.json`
- 锁文件：`/var/run/ota-agent.lock`

下载流程：

1. 清理上次失败留下的临时文件
2. 下载到临时文件，例如 `update.swu.part`
3. 下载成功后重命名为正式文件
4. 执行 `sha256sum` 校验
5. 校验通过后才允许安装

第一版不做断点续传，以降低复杂度。

## 11. 错误处理设计

### 11.1 命令错误

包括：

- JSON 格式不合法
- 缺字段
- `sha256` 格式错误
- URL 为空

处理策略：直接拒绝，不进入下载阶段，并上报 `failed`。

### 11.2 环境错误

包括：

- 当前已有待提交升级
- 当前槽位状态异常
- `fw_printenv` 失败
- 硬件兼容配置缺失

处理策略：直接拒绝升级，并上报明确错误原因。

### 11.3 下载错误

包括：

- HTTP 404
- 网络不可达
- 文件下载中断
- `SHA256` 不匹配

处理策略：有限重试，最终失败时清理临时文件并上报 `failed`。

### 11.4 安装错误

包括：

- `board-apply-update` 返回非 0
- `swupdate` 安装失败
- 自动重启后未进入目标槽位
- 未完成首启提交

处理策略：记录失败阶段，依赖现有 A/B 机制与 U-Boot 回滚基础，并回报失败状态。

## 12. 重启后状态恢复

在调用 `board-apply-update` 前，`ota-agent` 必须将以下信息写入状态文件：

- `requestId`
- `targetVersion`
- `targetSlot`
- `phase`
- `autoReboot`
- 时间戳

系统重启后，`ota-agent` 启动时需结合以下信息恢复上下文：

- `/proc/cmdline`
- `fw_printenv active_slot`
- `fw_printenv last_good_slot`
- `fw_printenv upgrade_available`
- 本地状态文件

恢复后的判定规则：

- 若当前已进入目标槽位，且 `upgrade_available=0`，并且 `last_good_slot` 与目标槽位一致，则上报 `committed/success`
- 若未进入目标槽位，或检测到已回滚到旧槽位，则上报 `failed/error`

## 13. 板端实现建议

第一版建议：

- `ota-agent` 使用 C 语言实现
- 通过 SysV init 自启动
- MQTT 使用轻量库
- 下载调用现有 `curl`
- 哈希校验调用现有 `sha256sum`
- 日志统一写入 `syslog`

这样做可以与当前项目风格保持一致，同时避免引入额外大型运行时。

## 14. 主机端配套设计

实验室第一版只需要 3 类配套：

1. MQTT broker  
   例如 Mosquitto，用于命令下发与状态回收。

2. HTTP 文件服务器  
   保存 `.swu`、`sha256` 和可选版本说明。

3. 发布脚本  
   在主机上生成命令 JSON 并发布到指定设备主题。

第一版不要求复杂后台界面，也不要求任务数据库。

## 15. 测试设计

测试建议分 4 层：

### 15.1 协议解析测试

验证以下情况：

- 合法命令
- 缺字段命令
- 非法 `sha256`
- 重复 `requestId`

### 15.2 下载与校验测试

验证以下情况：

- HTTP 下载成功
- 校验成功
- 校验失败
- 下载中断

### 15.3 本地升级联调测试

验证以下情况：

- 能正确调用 `board-apply-update`
- 默认模式不重启
- `autoReboot=true` 时触发自动重启
- 防重入与防重复升级保护生效

### 15.4 跨重启闭环测试

验证以下情况：

- 下载成功
- 升级执行成功
- 自动重启
- 切换到新槽
- 首启提交成功
- MQTT 正确上报 `committed`

## 16. 风险与后续演进

### 16.1 第一版风险

- 当前升级成功判定仍以“进入用户态并完成首启提交”为准，尚未纳入业务服务健康检查
- 未引入签名校验，安全能力仍以完整性校验为主
- 下载缓存空间管理在大包场景下需要额外关注

### 16.2 后续演进方向

- 引入包签名校验
- 引入设备分组命令
- 引入版本清单 `manifest`
- 引入更细粒度的健康检查与提交条件
- 引入更完整的灰度发布控制

## 17. 分阶段落地建议

### 阶段 1：最小远程触发闭环

- 新增 `ota-agent`
- MQTT 收命令
- HTTP 下载 `.swu`
- SHA256 校验
- 调用 `board-apply-update`

### 阶段 2：跨重启状态恢复

- 增加本地状态文件
- 启动后恢复升级上下文
- 上报最终成功或失败状态

### 阶段 3：工程化收尾

- 锁文件机制
- `requestId` 去重
- 空间检查
- 更清晰的错误分类
- 文档与测试补齐

## 18. 结论

最终采用的设计结论如下：

- 保留现有 `.swu + SWUpdate + A/B` 升级链路
- 新增一个 C 语言实现的 `ota-agent`
- MQTT 负责命令下发与状态回报
- HTTP 负责升级包分发
- 第一版使用 SHA256 完整性校验
- 最终升级成功以“首启提交完成”为准

该方案兼顾了现阶段的可落地性、调试便利性和后续扩展空间，适合作为当前项目的实验室版远程 OTA 方案。
