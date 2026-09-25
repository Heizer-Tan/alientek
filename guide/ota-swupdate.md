# 单一升级包（SWUpdate + A/B）

当前量产升级链路采用：

- 一个共享 `boot` FAT 分区，保存 `zImage`、`imx6ull-alientek-alpha.dtb`、`boot.scr`
- 两个 ext4 根文件系统槽位：`rootfsA`、`rootfsB`
- U-Boot 环境变量 `active_slot`、`upgrade_available`、`bootcount`
- Linux 用户态通过 `swupdate` 写入「非活动槽位」，首启成功后由 `board-upgrade-commit` 提交新槽

默认分区布局见 `meta-alientek/wic/imx6ull-alientek-ab.wks.in`：

- `u-boot`：原始写入 TF 卡前部
- `boot`：FAT，共享启动文件
- `rootfsA` / `rootfsB`：当前槽位或候选槽位

## 构建 `.swu`

```bash
./scripts/build.sh alientek-image-update
```

生成物位于 `build/tmp/deploy/images/imx6ull-alientek-alpha/`，包含：

- `alientek-image-update-imx6ull-alientek-alpha.swu`
- `alientek-image-base-imx6ull-alientek-alpha.rootfs.ext4.gz`
- `u-boot.imx`、`zImage`、`imx6ull-alientek-alpha.dtb`、`boot.scr`

## 板端执行升级

将 `.swu` 拷到板子后执行：

```bash
board-apply-update /tmp/alientek-image-update-imx6ull-alientek-alpha.swu
```

`board-apply-update` 会读取当前 `active_slot`，自动选择 `stable,slotA` 或 `stable,slotB`，始终写入非活动 rootfs 槽；成功后自动重启以切槽。

## 板端 Web 升级

浏览器打开 `http://<板子IP>:8080/`，在「固件升级」区选择 `.swu` 后上传。服务端调用 `board-apply-update` 自动选非活动槽并切环境，成功后自动重启。无口令，仅建议在实验室可信局域网使用。

## Qt 拉取最新（HTTP 目录）

dashboard OTA 页「拉取最新并升级」会调用：

```bash
ota-agent --pull-latest
# 或指定目录：
ota-agent --pull-latest http://192.168.5.13:8000
```

默认目录来自 `/etc/default/ota-agent` 的 `OTA_FIRMWARE_BASE`（默认 `http://192.168.5.13:8000`）。  
板端抓取目录 HTML，在 `alientek-image-update*.swu` 中：

1. **优先**带时间戳的真实包：`…rootfs-YYYYMMDDHHMMSS.swu`（取最大时间戳）
2. 若没有，再回退到无时间戳名 `…rootfs.swu`（Yocto 指向最新构建的软链）

实验室场景不校验远端 sha256；下载后直接 `board-apply-update` 并重启。  
`ota-agent` 向 stdout 输出 `OTA_PROGRESS <0-100> <stage>`，dashboard 进度条据此更新（解析目录 → 下载 10%–90% → 刷写 → 完成）。  
仅解析选包可设：`OTA_PULL_DRY_RUN=1 ota-agent --pull-latest`（只打印选中 URL）。

## 首启确认与回滚

- 升级阶段会把 `upgrade_available=1` 并切换 `active_slot`
- U-Boot 启用 `bootcount` / `bootlimit=3`
- 若连续启动失败超过限制，`altbootcmd` 会把槽位切回上一个分区
- 新系统正常启动后，`/etc/init.d/board-upgrade-commit` 会清除 `upgrade_available` 并把 `bootcount` 归零
- 板端实测步骤见本地 `docs/swu-upgrade-validation.md`（该目录不入库，仅本机设计/验证笔记）

可在板上查看状态：

```bash
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
```

当前板级 `boot.scr` 会按 `active_slot` 选择根分区：优先使用 U-Boot 环境中的 `rootfs_a_partuuid` / `rootfs_b_partuuid`（`part uuid mmc 0:2/3`，需 `CONFIG_CMD_PART`），未缓存时回退 `/dev/mmcblk0p2` 与 `/dev/mmcblk0p3`。

MQTT 远程升级见 [mqtt-ota.md](./mqtt-ota.md)。
Qt 拉取最新见上文「Qt 拉取最新（HTTP 目录）」。
