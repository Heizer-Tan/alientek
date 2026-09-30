# 升级方案（SWUpdate + A/B + 稳态 Failover）

量产路径：**单一 `.swu` 写入非活动 rootfs 槽 → 试跑 → 提交**；启动失败 / 看门狗超时可自动切槽一次。整卡布局与分区表变更需烧 `.wic`，不能只靠 OTA。

相关：启动链 [boot-chain.md](./boot-chain.md)、烧卡 [flash-tf.md](./flash-tf.md)、MQTT [mqtt-ota.md](./mqtt-ota.md)。稳态设计笔记见仓库内 `docs/superpowers/specs/2026-10-01-steady-ab-failover-design.md`。

---

## 1. 磁盘布局与谁被升级

布局定义：`meta-alientek/wic/imx6ull-alientek-ab.wks.in`。

```
[raw u-boot] | boot(FAT) | rootfsA | rootfsB | data(ext4 → /data)
```

| 区域 | OTA `.swu` | 整卡 `.wic` | 说明 |
|------|------------|-------------|------|
| u-boot raw | 一般不改 | 覆盖 | U-Boot 环境在 MMC env |
| `boot` | 视 sw-description；当前以 rootfs 为主 | 覆盖 | 共享 `zImage`、DTB、`boot.scr` |
| rootfsA / rootfsB | **只写非活动槽** | 两槽都写 | A/B 根文件系统 |
| `data` | **不写** | 覆盖 | 持久区；初值约 64MiB，首启 `board-data-grow` 可扩到盘尾 |

根槽由 U-Boot `active_slot` 决定：优先 `rootfs_a_partuuid` / `rootfs_b_partuuid`，否则 `/dev/mmcblk0p2`（A）/ `p3`（B）。

`data` 上有 LED DTB 切换脚本：`/data/bin/switch-led-dtb`（封装 `/usr/sbin/switch-led-dtb`），见 [board-apps.md](./board-apps.md)。

---

## 2. OTA 与整卡重烧怎么选

| 场景 | 用什么 |
|------|--------|
| 只更新 rootfs（应用/配置） | `.swu` / 下述任一入口 |
| 新分区、改 wks、U-Boot failover、默认策略等 | **整卡 `.wic`**（bmaptool / Rufus DD） |
| 只切 LED 用 DTB（chardev ↔ gpio-leds） | `switch-led-dtb` + 重启（不换 rootfs） |

---

## 3. 升级入口（殊途同归）

最终都落到 **`ota-apply <xxx.swu>`** → `swupdate` 写非活动槽 → 改环境变量 → 自动重启。

### 构建 `.swu`

```bash
./scripts/build.sh alientek-image-update
```

产物在 `build/tmp/deploy/images/imx6ull-alientek-alpha/`：

- `alientek-image-update-imx6ull-alientek-alpha.swu`
- `alientek-image-base-*.rootfs.ext4.gz`、`u-boot.imx`、`zImage`、DTB、`boot.scr`

### 命令行

```bash
ota-apply /tmp/alientek-image-update-imx6ull-alientek-alpha.swu
```

按当前 `active_slot` 选 `stable,slotA` 或 `stable,slotB`，始终写**非活动**槽。

### Web

浏览器打开 `http://<板子IP>:8080/`，「固件升级」上传 `.swu`。服务端调用 `ota-apply`。无口令，仅建议实验室可信局域网。

### Qt dashboard 拉取最新

```bash
ota-agent --pull-latest
# 或指定目录：
ota-agent --pull-latest http://192.168.5.13:8000
```

默认目录：`/etc/default/ota-agent` 的 `OTA_FIRMWARE_BASE`（默认 `http://192.168.5.13:8000`）。抓取目录 HTML，在 `alientek-image-update*.swu` 中：

1. **优先**带时间戳包：`…rootfs-YYYYMMDDHHMMSS.swu`（取最大时间戳）
2. 否则回退无时间戳名 `…rootfs.swu`（Yocto 最新软链）

实验室不校验远端 sha256；下载后直接 `ota-apply`。stdout 有 `OTA_PROGRESS <0-100> <stage>` 供 dashboard 进度条。仅解析选包：`OTA_PULL_DRY_RUN=1 ota-agent --pull-latest`。

### MQTT

见 [mqtt-ota.md](./mqtt-ota.md)（同样落到 apply）。

---

## 4. 一次 OTA 的生命周期

```
当前槽 A 运行（示例）
    │
    ▼
检查：ota_pending≠1，且 cmdline 槽与 active_slot 一致
    │
    ▼
swupdate 写入 B（stable,slotB）
    │
    ▼
fw_setenv: active_slot=B, ota_pending=1,
           upgrade_available=1, bootcount=0
    │
    ▼
reboot → 从 B 启动（试跑）
    │
    ├─ 成功进系统
    │     board-boot-confirm: bootcount=0, upgrade_available=1, 开看门狗喂狗
    │     board-upgrade-commit（ota_pending=1）: 健康检查
    │           ├─ 通过 → ota_pending=0, failover_done=0, last_good_slot=B  【提交】
    │           └─ 失败 → 切回 A, failover_done=1, 重启                  【用户态回滚】
    │
    └─ 起不来 / 看门狗反复复位（bootcount≥3）
          altbootcmd:
            failover_done≠1 → 切到另一槽, failover_done=1, ota_pending=0
            failover_done=1 → 进 bootmenu，不再自动切
```

要点：**新槽先试跑，确认成功才算提交**；失败可回旧槽。`ota_pending=1` 期间拒绝再次升级。

---

## 5. 稳态 Failover（启动失败 / 看门狗）

与 OTA 共用 bootcount，**不限于升级窗口**：

1. `upgrade_available=1` 常开 → 每次 U-Boot 启动 `bootcount++`（`BOOTCOUNT_ENV`，`bootlimit=3`）
2. 正常进系统 → `board-boot-confirm` 清 `bootcount` 并喂狗（`/dev/watchdog`）
3. 起不来或卡死被 WDT 复位且累计 ≥ 3 → 自动切**一次**槽（`failover_done=1`）
4. 切过后仍失败 → 进 `bootmenu`，避免 A↔B 死循环
5. 修好后再允许自动切一次：`fw_setenv failover_done 0`

OTA 成功提交时也会清 `failover_done`，恢复「再坏可再切一次」的资格。

---

## 6. 关键环境变量

| 变量 | 作用 |
|------|------|
| `active_slot` | 当前应启动的根槽 A/B |
| `ota_pending` | `1`=OTA 已写新槽、尚未提交；拒绝再次升级 |
| `upgrade_available` | **bootcount 使能**（保持 `1`）；不再表示「OTA 未提交」 |
| `bootcount` / `bootlimit` | 连续未确认启动次数 / 上限（默认 3） |
| `failover_done` | 已做过一次自动切槽 |
| `last_good_slot` | 上次提交成功的槽 |
| `rootfs_a_partuuid` / `rootfs_b_partuuid` | 选根用的 PARTUUID 缓存 |

```bash
fw_printenv active_slot ota_pending upgrade_available bootcount failover_done
```

---

## 7. 明确不做的

- 双槽都坏时自动 NFS 救援
- 业务探针失败也切槽（只覆盖启动失败 + 看门狗）
- `.swu` 更新 `data` 分区内容
