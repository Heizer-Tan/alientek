# 烧写 TF 卡（mmcboot）

确认设备节点后：

```bash
sudo bmaptool copy alientek-image-base-imx6ull-alientek-alpha.rootfs.wic.gz /dev/sdX
# 或：sudo dd if=alientek-image-base-imx6ull-alientek-alpha.rootfs.wic of=/dev/sdX bs=4M conv=fsync
```

镜像产物路径见 [build.md](./build.md)。

拨码选择 SD 启动，串口 115200。U-Boot 执行 `run mmcboot`。若找不到系统分区，在 U-Boot 里 `mmc list` 后 `setenv mmcdev 0` 再 `run mmcboot`。

相关排障见 [troubleshooting.md](./troubleshooting.md)。
