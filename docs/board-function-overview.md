# 当前板载功能梳理

本文档面向“当前这块已适配 Yocto BSP 的阿尔法板能做什么”这一问题，汇总当前已经集成、已验证或可直接在板端操作的功能，方便后续演示、联调和量产前评审。

## 1. 基础软件栈

- Bootloader：主线 `U-Boot 2026.07`
- Kernel：主线 `Linux 7.1.4`
- 设备树：`imx6ull-alientek-alpha.dtb`
- Rootfs：Yocto `core-image-base` 衍生镜像 `alientek-image-base`
- 启动方式：支持 TF 卡本地启动，也保留 NFS netboot 作为调试入口

当前镜像默认集成：

- `openssh`
- `iproute2`、`iputils`、`ethtool`
- `i2c-tools`
- `sqlite3`
- `swupdate`
- `libubootenv` / `fw_printenv` / `fw_setenv`
- 板级工具与业务程序

## 2. 启动与存储布局

当前量产导向布局采用 A/B rootfs 方案，核心目标是支持单文件升级与失败回滚。

默认 TF 卡布局：

- 原始区域写入 `u-boot.imx`
- 一个共享 `boot` 分区，保存 `zImage`、`dtb`、`boot.scr`
- 两个根文件系统槽位：`rootfsA`、`rootfsB`

当前启动逻辑特性：

- `boot.scr` 根据 `active_slot` 选择 `rootfsA` 或 `rootfsB`
- 当前实现通过 `/dev/mmcblk0p2`、`/dev/mmcblk0p3` 传递根分区
- 本地启动默认走 `run mmcboot`
- 调试时仍可切回 `run netboot`

## 3. 升级能力

当前已经具备单文件升级能力，支持把以下内容打进一个 `.swu` 包：

- `rootfs`
- `u-boot.imx`
- `zImage`
- `imx6ull-alientek-alpha.dtb`
- `boot.scr`

板端升级流程：

1. 执行 `board-apply-update /tmp/xxx.swu`
2. 自动识别当前活动槽位
3. 自动把升级内容写入非活动槽位
4. 自动设置 `active_slot`、`upgrade_available`、`bootcount`
5. 重启后切到新槽位
6. 首启成功后由 `board-upgrade-commit` 提交新槽位

当前已验证通过的能力：

- `.swu` 包可正常解析和安装
- `rootfs` 可写入备用槽位
- 内核、设备树、`boot.scr` 可写入共享启动分区
- 升级后可自动切槽
- 首启后会自动清除 `upgrade_available`
- `last_good_slot` 会落盘保存

当前尚未实测的部分：

- 真实损坏场景下的自动失败回滚

### 3.1 MQTT OTA Agent

镜像内的 `ota-agent` 当前具备：

- MQTT OTA 命令 JSON 的严格解析和状态 payload 生成
- `.swu` 下载、SHA256 校验、升级 guard 和 `board-apply-update` 调用
- 升级前持久化请求、目标版本、目标槽位、阶段和自动重启选项
- 重启后结合实际根分区及 U-Boot 环境恢复任务
- 对成功提交回报 `committed/success`，对回滚或槽位不一致回报 `failed/error`

能力边界：

- MQTT broker 连接、订阅和发布目前仍是 `ENOSYS` 薄接缝，不包含真实常驻消息循环
- 发布接缝不可用时会在本地输出完整最终状态 payload，恢复结论仍可观察
- 最终成功仍以 `board-upgrade-commit` 完成槽位提交为准，不包含业务服务健康检查

## 4. 网络能力

当前镜像区分两类网络场景：

### 4.1 本地 MMC 启动

镜像内置 `board-network` 服务，开机自动配置：

- 网口：`eth1`
- IPv4：`192.168.5.201/24`
- 网关：`192.168.5.1`

可通过 `etc/default/board-network` 调整静态 IP 参数。

### 4.2 NFS 调试启动

仍保留 netboot 调试入口，便于：

- 内核/设备树联调
- 不重烧卡快速验证 rootfs
- 调试构建产物导出链路

NFS 场景下，内核通过 `ip=` 参数完成基础网络配置。

## 5. 时间同步能力

当前已具备“RTC 恢复 + 启动后 NTP 校时”的双保险机制。

具体链路如下：

1. 开机早期由 `busybox-hwclock` 从板载 `SNVS RTC` 恢复系统时间
2. 用户态启动 `board-ntpdate`
3. `board-ntpdate` 使用 BusyBox `ntpd -q` 做一次性对时
4. 关机或手工执行 `hwclock -w` 时把系统时间写回 RTC

当前特点：

- 板子断网时仍可依赖 RTC 维持时间
- 网络恢复后可自动校正时间
- 默认在 `ap3216c-logger` 之前执行，避免传感器入库时间长期错误

## 6. 传感器与业务能力

### 6.1 AP3216C 传感器读取

当前已集成：

- `ap3216c-module`
- `ap3216c-read`
- `i2c-tools`

可用于：

- 检查 I2C 总线
- 验证 AP3216C 驱动加载
- 实时读取 `ir`、`als`、`ps` 数据

### 6.2 传感器定时入库

`ap3216c-logger` 已集成到镜像并支持开机自启。

当前行为：

- 周期性采样 AP3216C
- 写入 `/var/lib/ap3216c/ap3216c.db`
- 数据默认保留 7 天
- 通过 `syslog` 输出运行日志

### 6.3 Web 查询界面

`board-web` 已集成并支持开机自启。

当前行为：

- 监听 `:8080`
- 从 SQLite 读取传感器历史数据
- 在浏览器中查看表格和图形化结果

这意味着当前板子已经具备“传感器采集 -> 本地存储 -> 浏览器查看”的基本业务闭环。

## 7. 人机交互与调试工具

当前镜像还包含：

- `key-monitor`：监视 `gpio-keys` 按键事件
- `fw_printenv` / `fw_setenv`：查看与修改 U-Boot 环境变量
- `swupdate`：执行升级包安装
- `i2cdetect`、`i2cget`、`i2cdump`：I2C 调试

这些工具覆盖了当前调试最常见的几条链路：

- 启动状态检查
- 升级状态检查
- 传感器总线检查
- 按键输入验证

## 8. 当前可直接演示的完整能力

如果按“可对外展示”的角度看，当前板子已经可以演示以下完整流程：

1. TF 卡启动进入本地系统
2. 网络自动拉起
3. RTC 恢复时间并执行 NTP 校时
4. 读取 AP3216C 传感器
5. 后台定时把数据写入 SQLite
6. 浏览器访问 `board-web` 查看历史数据
7. 执行单文件 `.swu` 升级
8. 重启后自动切到新槽位继续运行

## 9. 当前限制与后续建议

当前仍有几项边界需要明确：

- 失败回滚链路的真实损坏场景还没有完成板端实测
- MQTT broker 常驻连接与真实发布尚未接入，当前使用本地 payload 输出验证
- `sw-description` 中 `compressed = true` 仍是兼容写法，建议改为 `compressed = "zlib"`
- 当前根分区选择仍依赖 `/dev/mmcblk0p2/p3`，尚未切到更稳健的 `PARTUUID` 方案
- 升级成功判定目前以“进入用户态并执行提交脚本”为准，还没有把业务服务健康检查纳入最终提交条件

总体上，当前板子已经从“能启动、能读传感器”提升到“具备本地业务闭环和 A/B 单文件升级能力”的阶段，已经适合继续做回滚验证、远程升级入口和量产前收尾。
