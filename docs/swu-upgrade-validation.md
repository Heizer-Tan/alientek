# SWUpdate 板端升级与回滚实测

本文档用于验证阿尔法板当前 A/B 升级链路是否按预期工作，覆盖：

- `.swu` 升级包写入非活动槽位
- U-Boot 根据 `active_slot` 切换启动根分区
- 首启成功后用户态提交新槽位
- 新槽位连续启动失败后自动回滚

## 前提条件

- 已刷入支持 A/B 布局的最新 TF 卡镜像
- 板上已包含 `swupdate`、`board-apply-update`、`fw_printenv`、`fw_setenv`
- `/etc/hwrevision` 已存在，内容应与升级包内 `hardware-compatibility` 匹配
- 可通过串口观察 U-Boot 和 Linux 启动日志
- 已将升级包拷到板子，例如 `/tmp/alientek-image-update-imx6ull-alientek-alpha.rootfs.swu`

## 1. 升级前检查

先确认工具和当前槽位状态：

```bash
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount

which swupdate
which board-apply-update
which fw_printenv
which fw_setenv
```

再确认分区标签：

```bash
blkid /dev/mmcblk0p1 /dev/mmcblk0p2 /dev/mmcblk0p3
```

预期：

- `/dev/mmcblk0p1` 为 `boot`
- `/dev/mmcblk0p2` 为 `rootfsA`
- `/dev/mmcblk0p3` 为 `rootfsB`

若想从干净状态开始，先复位环境变量：

```bash
fw_setenv active_slot A
fw_setenv upgrade_available 0
fw_setenv bootcount 0
sync
reboot
```

系统起来后检查当前根分区：

```bash
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
cat /proc/cmdline
mount | grep " / "
```

若当前在 A 槽，`/proc/cmdline` 应包含：

```bash
root=/dev/mmcblk0p2
```

## 2. 验证升级成功路径

执行升级：

```bash
board-apply-update /tmp/alientek-image-update-imx6ull-alientek-alpha.rootfs.swu
```

若当前 `active_slot=A`，预期脚本打印：

```bash
当前槽位: A，执行模式: stable,slotB
```

升级完成后、重启前检查：

```bash
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
sync
```

预期：

- `active_slot=B`
- `upgrade_available=1`
- `bootcount=0`

然后重启：

```bash
reboot
```

系统启动完成后再次检查：

```bash
cat /proc/cmdline
mount | grep " / "
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
fw_printenv last_good_slot
```

预期：

- `root=/dev/mmcblk0p3`
- `active_slot=B`
- `upgrade_available=0`
- `bootcount=0`
- `last_good_slot=B`

如需再确认业务服务：

```bash
/etc/init.d/ap3216c-logger status || true
/etc/init.d/board-web status || true
ps | grep -E "ap3216c-logger|board-web"
```

## 3. 验证失败回滚路径

建议在串口下进行，便于观察多次重启后的回退行为。

### 方法一：破坏目标槽位的 `init`

先确保当前运行在 A 槽：

```bash
fw_setenv active_slot A
fw_setenv upgrade_available 0
fw_setenv bootcount 0
sync
reboot
```

重启回 A 后，正常执行一次升级到 B：

```bash
board-apply-update /tmp/alientek-image-update-imx6ull-alientek-alpha.rootfs.swu
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
```

此时先不要验证成功，而是故意让 B 槽无法完成早期启动：

```bash
mkdir -p /mnt/slotB
mount /dev/mmcblk0p3 /mnt/slotB
mv /mnt/slotB/sbin/init /mnt/slotB/sbin/init.bak
sync
umount /mnt/slotB
reboot
```

预期行为：

- U-Boot 先尝试启动 B 槽
- Linux 因缺少 `/sbin/init` 提前失败
- 失败累计超过 `bootlimit=3` 后，U-Boot 自动切回 A 槽

回到系统后检查：

```bash
cat /proc/cmdline
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
```

预期：

- `root=/dev/mmcblk0p2`
- `active_slot=A`
- `upgrade_available=0`
- `bootcount=0`

恢复被破坏的 B 槽：

```bash
mkdir -p /mnt/slotB
mount /dev/mmcblk0p3 /mnt/slotB
mv /mnt/slotB/sbin/init.bak /mnt/slotB/sbin/init
sync
umount /mnt/slotB
```

### 方法二：直接验证 U-Boot 回滚状态机

如果暂时不想修改文件系统，可直接构造回滚条件：

```bash
fw_setenv active_slot B
fw_setenv upgrade_available 1
fw_setenv bootcount 4
reset
```

由于 `bootlimit=3`，预期 U-Boot 启动阶段就会执行回退。系统起来后检查：

```bash
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
cat /proc/cmdline
```

预期：

- `active_slot=A`
- `upgrade_available=0`
- `bootcount=0`
- `root=PARTLABEL=rootfsA`

此方法验证速度快，但只能证明回滚状态机正常，不能代替真实升级后的损坏回滚测试。

## 4. 建议记录项

每次测试建议记录以下信息：

```bash
date
fw_printenv active_slot
fw_printenv upgrade_available
fw_printenv bootcount
cat /proc/cmdline
mount | grep " / "
```

## 5. 当前实现的注意点

当前 `board-upgrade-commit` 以“成功进入用户态并拉起该服务”为升级成功信号，因此它主要覆盖：

- 内核能启动
- 根文件系统能挂载
- 基本用户态可运行

它还没有把 `ap3216c-logger`、`board-web` 等业务服务健康检查纳入“提交新槽位”的判定。如果后续要更稳，可以把提交动作延后到业务健康检查通过之后。

## 6. 验证 MQTT OTA 跨重启恢复

先确认状态文件记录了本次请求和目标槽位：

```bash
cat /var/lib/ota-agent/state.json
```

完成升级并重启后，检查恢复判定所使用的全部输入：

```bash
cat /proc/cmdline
fw_printenv active_slot
fw_printenv last_good_slot
fw_printenv upgrade_available
cat /var/lib/ota-agent/state.json
```

成功提交路径应满足：实际启动槽位、`active_slot`、`last_good_slot` 均等于状态文件中的 `targetSlot`，且 `upgrade_available=0`。最终状态文件和本地 payload 应包含：

```json
{"phase":"committed","result":"success"}
```

失败回滚路径应看到实际启动槽位或 `active_slot` 与 `targetSlot` 不同。最终状态文件和本地 payload 应包含：

```json
{"phase":"failed","result":"error"}
```

当前 MQTT 发布接缝返回 `ENOSYS` 时，`ota-agent` 会把完整 payload 输出到本地。可停止服务后以前台方式复核：

```bash
/etc/init.d/ota-agent stop
ota-agent
```

观察到最终 payload 后按 `Ctrl+C` 退出，再执行 `/etc/init.d/ota-agent start` 恢复服务。若状态仍处于 `upgrading` 且 `upgrade_available=1`，agent 会等待首启提交完成后再次判定，不会提前回报成功。
