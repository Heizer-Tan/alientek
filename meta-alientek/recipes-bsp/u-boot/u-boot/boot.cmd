setenv fdtfile imx6ull-alientek-alpha.dtb
setenv fdt_file imx6ull-alientek-alpha.dtb
setenv console ttymxc0,115200
setenv mmcdev 0
setenv mmcpart 1
setenv nfsroot /srv/nfs/nfs_rootfs

# 内核与 DTB 必须分地址；fdt_addr_r 未设时会落到 loadaddr，覆盖 zImage
setenv loadaddr 0x80800000
setenv fdt_addr_r 0x83000000
setenv fdt_addr 0x83000000

# NFS/TFTP 静态地址（按现场修改后 saveenv）
setenv serverip 192.168.5.27
setenv ipaddr 192.168.5.201
setenv gatewayip 192.168.5.1
setenv netmask 255.255.255.0

setenv mmcargs "setenv bootargs console=${console} root=/dev/mmcblk0p2 rootwait rw"
setenv mmcboot "echo Booting from MMC...; run mmcargs; fatload mmc ${mmcdev}:${mmcpart} ${loadaddr} zImage; fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

# 方案 A：内核/DTB 走 TFTP，根文件系统走 NFS；网口固定 eth1
setenv netargs "setenv bootargs console=${console} root=/dev/nfs rw nfsroot=${serverip}:${nfsroot},nfsvers=3,tcp ip=${ipaddr}:${serverip}:${gatewayip}:${netmask}::eth1:off"
setenv netboot "echo Booting from NFS...; run netargs; tftp ${loadaddr} zImage; tftp ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

setenv bootcmd "run netboot"
run netboot
