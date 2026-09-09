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

# A/B rootfs 约定：p2=rootfsA，p3=rootfsB；当前板级 U-Boot 未启用 part 命令，直接拼 Linux 块设备名
setenv select_slot 'if test -z "${active_slot}"; then setenv active_slot A; fi; if test "${active_slot}" = "B"; then setenv rootpart 3; setenv rootslot rootfsB; else setenv rootpart 2; setenv rootslot rootfsA; setenv active_slot A; fi'
setenv mmcargs 'run select_slot; setenv rootdev /dev/mmcblk${mmcdev}p${rootpart}; setenv bootargs console=${console} root=${rootdev} rootwait rw'
setenv rollback_slot 'if test "${active_slot}" = "B"; then setenv active_slot A; else setenv active_slot B; fi; setenv upgrade_available 0; setenv bootcount 0; saveenv'
setenv mmcboot "echo Booting from MMC slot ${active_slot}...; run mmcargs; fatload mmc ${mmcdev}:${mmcpart} ${loadaddr} zImage; fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

# 方案 A：内核/DTB 走 TFTP，根文件系统走 NFS；U-Boot/Linux 均用 eth1（ENET1）
# MDIO 在 ENET2 引脚上，双 FEC 都要保留；无 MAC 时补实验室本地管理地址
setenv netargs "setenv bootargs console=${console} root=/dev/nfs rw nfsroot=${serverip}:${nfsroot},nfsvers=3,tcp ip=${ipaddr}:${serverip}:${gatewayip}:${netmask}::eth1:off"
setenv netboot 'echo Booting from NFS...; if test -z "${ethaddr}"; then setenv ethaddr 02:11:22:33:44:55; fi; if test -z "${eth1addr}"; then setenv eth1addr 02:11:22:33:44:56; fi; setenv ethprime eth1; setenv ethact eth1; run netargs; tftp ${loadaddr} zImage; tftp ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}'

# 启动菜单：记住上次选择；首次默认 TF
if test -z "${boot_mode}"; then setenv boot_mode mmc; fi
if test -z "${bootmenu_default}"; then setenv bootmenu_default 0; fi

setenv boot_tf 'setenv boot_mode mmc; setenv bootmenu_default 0; saveenv; run mmcboot'
setenv boot_nfs 'setenv boot_mode nfs; setenv bootmenu_default 1; saveenv; run netboot'

setenv bootmenu_0 'Boot from TF (mmc)=run boot_tf'
setenv bootmenu_1 'Boot from NFS=run boot_nfs'
setenv bootmenu_delay 5

setenv bootcmd bootmenu
bootmenu
