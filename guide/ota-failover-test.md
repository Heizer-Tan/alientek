# 稳态 A/B Failover 实验室测试

验证：当前 rootfs 槽连续启动失败后，U-Boot 自动切到另一槽（且只切一次）。

相关总览见 [ota-swupdate.md](./ota-swupdate.md)。板端脚本：`ota-failover-test`（包 `board-update-tools`）。

---

## 1. 原理

### 1.1 正常启动时 bootcount 怎么走

```
U-Boot 启动
    │  upgrade_available=1 时：bootcount++
    │  若 bootcount > bootlimit(默认 3) → 跑 altbootcmd（切槽或进 bootmenu）
    │  否则按 active_slot 挂 rootfs，bootz
    ▼
Linux → /sbin/init → …
    │
    ▼
board-boot-confirm（SysV，较早）
    │  fw_setenv bootcount 0
    │  fw_setenv upgrade_available 1   # 下次进 U-Boot 仍会计数
    │  打开 /dev/watchdog 并周期喂狗
    ▼
正常业务
```

要点：

| 机制 | 作用 |
|------|------|
| `upgrade_available=1` | 打开 U-Boot `BOOTCOUNT_ENV` 计数（稳态也保持为 1） |
| `bootcount` | 连续「未确认成功」的启动次数 |
| `bootlimit=3` | 触发条件是 **`bootcount > bootlimit`**，即计数到 **4** 才切槽 |
| `board-boot-confirm` | 进系统后清零 bootcount，并启动看门狗 |
| `failover_done` | 已自动切过一次则为 1；再失败进 `bootmenu`，避免 A↔B 死循环 |

`altbootcmd`（简化）：

- `failover_done != 1`：切换 `active_slot`，设 `failover_done=1`，清 `bootcount`，再 `run bootcmd`
- 已是 1：清 `bootcount`，进 `bootmenu`，不再切

### 1.2 为什么「删掉 init」测不成

早期做法：`mv /sbin/init /sbin/init.bak`，指望卡死 → 看门狗复位 → 计数。

实际会：

1. `reboot`（不带 `-f`）还要再 exec `/sbin/init`，init 已没 → `cannot execute /sbin/init`
2. 内核依次试 `/sbin/init`、`/etc/init`、`/bin/init`，最后落到 **`/bin/sh` 应急壳**
3. 应急壳里 **没有** `board-boot-confirm`，看门狗从未打开 → **不会自动复位**
4. `bootcount` 停在 1，永远到不了切槽阈值

因此实验室脚本 **`break` 不用「删 init」**，而用：

1. 备份：`/sbin/init` → `/sbin/init.bak`
2. 写入桩脚本 `/sbin/init`：内容等价于 `exec reboot -f`
3. 每次启动：内核跑桩 → **立刻硬复位** → 再进 U-Boot → `bootcount++`
4. 约第 4 次进 U-Boot 时 `bootcount > 3` → 串口打印 `FAILSAFE: switched to …`

看门狗路径（卡死、进程挂死）是量产真实场景；硬复位桩只用于**可控地堆高 bootcount**，不替代 WDT 验证。

### 1.3 soft 模式（不破坏 init）

关掉 `board-boot-confirm` 执行位后，系统仍能完整进 Linux，但 **不会清 bootcount**。每次 `reboot -f`（或正常重启）都会让计数累加，同样可触发切槽。测完必须 `soft-undo`。

---

## 2. 前提

- 镜像含带 `failover_done` 的 U-Boot、`board-boot-confirm`、`ota-failover-test`
- **对槽必须能启动**（整卡 `.wic` 两槽都有系统，或曾成功 OTA 过）
- 串口 115200 打开，盯 U-Boot 日志
- 破坏性子命令必须加 `--yes`

分区约定：`mmcblk0p2` = A，`mmcblk0p3` = B。

---

## 3. 操作过程（推荐：break 桩）

假设当前在 **B**，对槽 **A** 完好（你板上曾是这种状态）。A↔B 对调理解即可。

### 步骤 1：看现状

```bash
ota-failover-test status
```

关注：`检测槽位`、`active_slot`、`failover_done`、`bootcount`、`upgrade_available`。

### 步骤 2：武装计数器

```bash
ota-failover-test arm --yes
```

效果：`failover_done=0`、`bootcount=0`、`upgrade_available=1`。  
若 `failover_done` 仍为 1，U-Boot **只会进 bootmenu，不会切槽**。

### 步骤 3：安装复位桩并重启

```bash
ota-failover-test break --yes --reboot
```

脚本会：

- 确认当前槽可识别，且对槽存在
- `mv /sbin/init /sbin/init.bak`
- 写桩 `/sbin/init`（`reboot -f`）
- `reboot -f`

之后机器会反复：U-Boot → 挂坏槽 → 跑桩 → 硬复位。串口上 `bootcount` 递增。

### 步骤 4：等待切槽

约 **4 次**进 U-Boot 后应看到类似：

```text
Warning: Bootlimit (3) exceeded. ...
FAILSAFE: switched to A
```

（目标槽随当时 `active_slot` 而变。）

进入对槽后：

```bash
ota-failover-test status
# 期望：检测槽位=A（或对槽），active_slot 已切换，failover_done=1
```

### 步骤 5：修好坏槽

仍在**好槽**上：

```bash
ota-failover-test restore --yes
```

会挂载对槽（坏槽）分区，把 `init.bak` 移回 `/sbin/init`（若上面是测试桩则先删掉）。

### 步骤 6：恢复「还能再自动切一次」

```bash
ota-failover-test arm --yes
# 即 failover_done=0，否则下次失败不会再切
```

---

## 4. 若已掉进应急壳（旧 break / 误操作）

提示符类似 `~ #`，`/proc` 可能不完整：

**继续堆 bootcount（手动）：**

```sh
reboot -f
```

每掉进壳再执行一次，直到串口出现 `FAILSAFE`。

**立刻修好当前槽、放弃测试：**

```sh
mv /sbin/init.bak /sbin/init
reboot -f
```

---

## 5. soft 模式（不破坏 init）

```bash
ota-failover-test status
ota-failover-test arm --yes
ota-failover-test soft --yes --reboot
```

每次再：

```bash
reboot -f
```

约 4 次后应切槽。测完：

```bash
ota-failover-test soft-undo --yes
ota-failover-test arm --yes    # 按需清 failover_done
```

---

## 6. 命令速查

| 命令 | 作用 |
|------|------|
| `status` | 只读：cmdline、槽位、U-Boot 变量 |
| `arm --yes` | 清 `failover_done` / `bootcount`，开 `upgrade_available` |
| `break --yes [--reboot]` | 备份 init，装 `reboot -f` 桩 |
| `restore --yes` | 在对槽（或当前根若仍有 bak）恢复 init |
| `restore-here --yes` | 只恢复当前根上的 `init.bak` |
| `soft --yes [--reboot]` | 禁用 `board-boot-confirm` |
| `soft-undo --yes` | 恢复并启动 `board-boot-confirm` |

---

## 7. 常见问题

| 现象 | 原因 | 处理 |
|------|------|------|
| 停在 `~ #`，不复位 | 只有 init 被删/改名，落进应急壳 | `reboot -f`；或换用新版 `break` 桩 |
| `bootcount` 到 3 仍不切 | 条件是 **`>`** bootlimit，不是 `≥` | 再复位一次到 4 |
| 进 bootmenu，不切槽 | `failover_done=1` | `ota-failover-test arm --yes` |
| 切槽后对槽也起不来 | 对槽本来就坏 / 两槽都 break | 整卡重烧或从好介质恢复 rootfs |
| `reboot` 报 cannot execute init | 普通 reboot 仍要走 init | 用 `reboot -f` |

---

## 8. 与量产行为的关系

| 实验室 | 量产 |
|--------|------|
| init 桩反复 `reboot -f` | 内核 panic、卡死、看门狗超时复位 |
| 人为堆高 `bootcount` | 进系统失败 → 未跑 `board-boot-confirm` → 计数不清零 |
| `ota-failover-test restore` | OTA 成功提交或人工修坏槽后 `failover_done=0` |

脚本路径：`meta-alientek/recipes-core/ota/board-update-tools/files/ota-failover-test`。
