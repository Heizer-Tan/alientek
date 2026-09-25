# 板端应用与演示

## 按键演示（key-monitor）

串口登录后：`key-monitor`（自动找 `gpio-keys`），按 KEY0 会打印 `type=1 code=28 value=1/0`。可选自启：`update-rc.d key-monitor defaults && /etc/init.d/key-monitor start`（写 syslog）。

## 触摸演示（touch-monitor）

对应设备树 `gt9147@5d`（Goodix，INT=`GPIO1_IO09`）。串口登录后：

```bash
# 原始事件流 + 每帧摘要
touch-monitor
# 仅摘要（手指移动时持续刷 x/y）
touch-monitor -S
# 指定设备
touch-monitor -d /dev/input/event2
cat /proc/bus/input/devices   # 确认 Goodix 节点
```

需内核含 `CONFIG_TOUCHSCREEN_GOODIX`（见 `recipes-kernel/linux/linux/touch.cfg`），且电阻屏 `&tsc` 已关闭以免抢 INT 脚。

## AP3216C 演示

设备树节点挂在 `i2c1@0x1e`，镜像内包含 `i2c-tools`、`ap3216c-module` 与 `ap3216c-read`。可先用 `i2cdetect` / `i2cget` / `i2cdump` 排查总线，再读取驱动导出的数据：

```bash
i2cdetect -y 0
ap3216c-read
ap3216c-read -w
# 示例：ir=12 als=345 ps=28
```

若手靠近传感器，`ps` 应上升；遮光/打光时，`als` 应变化。

从芯片到 `/dev/ap3216c` 的完整调用链见：  
`meta-alientek/recipes-kernel/modules/ap3216c/AP3216C-DEVICE-PATH.md`。

## ICM20608 演示

设备树节点挂在 `ecspi3` CS0（`compatible = "alientek,icm20608"`）。驱动为 **IIO sysfs**（不再提供 `/dev/icm20608`），镜像含 `icm20608-module` 与 `icm20608-read`：

```bash
lsmod | grep icm20608
# 确认 IIO 设备（name=icm20608）
grep -H . /sys/bus/iio/devices/iio:device*/name
# 可选：直接读 raw/scale
# cat /sys/bus/iio/devices/iio:deviceX/in_accel_z_raw
icm20608-read
icm20608-read -w
# 示例：ax=... ay=... az=... gx=... gy=... gz=... temp_raw=... ax_g=... ay_g=... az_g=... gx_dps=... gy_dps=... gz_dps=... temp_c=...
# 环境变量 ICM20608_IIO_NAME 默认 icm20608
```

静止时 `az_g` 约 ±1g，角速度接近 0。历史数据见下方 Web「六轴」页。`ls /dev/icm20608` 应失败（已硬切掉 misc）。

## LCD 板级控制台

镜像含 `dashboard`（**Qt6 Widgets + linuxfb** `/dev/fb0` + evdev/Goodix 触摸），开机自启。窗口标题为「板级控制台」，主页 **2×3** 入口：

- 光感 AP3216C / 六轴 ICM20608（详情页）
- 系统信息（IP、运行时间、内存、负载）
- 灯控（LED 开/关/恢复呼吸灯，蜂鸣器开/关）
- 按键状态（`gpio-key`）
- OTA 只读槽位（`fw_printenv`，升级仍走 Web）

手动：`/etc/init.d/dashboard start|stop`  
可选环境变量见 `/etc/default/dashboard`（如 `DASHBOARD_IFACE`、`DASHBOARD_LED_NAME`、`QT_QPA_PLATFORM`）。

需确认 `ls -l /dev/fb0`，触摸为 Goodix event 节点。

## 传感器入库与 Web 查询

- 光感采集：`ap3216c-logger` 每 5 分钟写入 `/var/lib/ap3216c/ap3216c.db`，保留 7 天
- 六轴采集：`icm20608-logger` 默认每 5 秒写入 `/var/lib/icm20608/icm20608.db`，保留 7 天
- 查询：浏览器打开 `http://<板子IP>:8080/`（AP3216C）；六轴页 `http://<板子IP>:8080/icm20608`
- 固件升级：光感首页「固件升级」上传 `.swu`（自动 A/B，成功后重启）；详见 [ota-swupdate.md](./ota-swupdate.md)
- 启停：`/etc/init.d/ap3216c-logger`、`/etc/init.d/icm20608-logger`、`/etc/init.d/webserver`
- 手动读数：`ap3216c-read`、`icm20608-read`
