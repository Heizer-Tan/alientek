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

# A/B rootfs 约定：p2=rootfsA，p3=rootfsB；启动参数使用 PARTLABEL，避免 mmc 编号漂移
setenv select_slot 'if test -z "${active_slot}"; then setenv active_slot A; fi; if test "${active_slot}" = "B"; then setenv rootlabel rootfsB; else setenv rootlabel rootfsA; setenv active_slot A; fi'
setenv mmcargs 'run select_slot; setenv bootargs console=${console} root=PARTLABEL=${rootlabel} rootwait rw'
setenv rollback_slot 'if test "${active_slot}" = "B"; then setenv active_slot A; else setenv active_slot B; fi; setenv upgrade_available 0; setenv bootcount 0; saveenv'
setenv mmcboot "echo Booting from MMC slot ${active_slot}...; run mmcargs; fatload mmc ${mmcdev}:${mmcpart} ${loadaddr} zImage; fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

# 方案 A：内核/DTB 走 TFTP，根文件系统走 NFS；网口固定 eth1
setenv netargs "setenv bootargs console=${console} root=/dev/nfs rw nfsroot=${serverip}:${nfsroot},nfsvers=3,tcp ip=${ipaddr}:${serverip}:${gatewayip}:${netmask}::eth1:off"
setenv netboot "echo Booting from NFS...; run netargs; tftp ${loadaddr} zImage; tftp ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

setenv bootcmd "run mmcboot"
run mmcboot
