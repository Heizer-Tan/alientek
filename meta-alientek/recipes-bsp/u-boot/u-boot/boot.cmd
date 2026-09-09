setenv fdtfile imx6ull-alientek-alpha.dtb
setenv fdt_file imx6ull-alientek-alpha.dtb
setenv console ttymxc0,115200
setenv mmcdev 0
setenv mmcpart 1
setenv nfsroot /srv/nfs/nfs_rootfs

# Kernel and DTB need separate addresses; unset fdt_addr_r falls back to loadaddr and overwrites zImage
setenv loadaddr 0x80800000
setenv fdt_addr_r 0x83000000
setenv fdt_addr 0x83000000

# NFS/TFTP static addressing (edit on-site then saveenv)
setenv serverip 192.168.5.27
setenv ipaddr 192.168.5.201
setenv gatewayip 192.168.5.1
setenv netmask 255.255.255.0

# Locally administered MACs (env default / saveenv corruption often clears these)
if test -z "${ethaddr}"; then setenv ethaddr 02:11:22:33:44:55; fi
if test -z "${eth1addr}"; then setenv eth1addr 02:11:22:33:44:56; fi
setenv ethprime eth0
setenv ethact eth0

# A/B helpers for manual run mmcboot (p2=rootfsA, p3=rootfsB)
setenv select_slot 'if test -z "${active_slot}"; then setenv active_slot A; fi; if test "${active_slot}" = "B"; then setenv rootpart 3; setenv rootslot rootfsB; else setenv rootpart 2; setenv rootslot rootfsA; setenv active_slot A; fi'
setenv mmcargs 'run select_slot; setenv rootdev /dev/mmcblk${mmcdev}p${rootpart}; setenv bootargs console=${console} root=${rootdev} rootwait rw'
setenv rollback_slot 'if test "${active_slot}" = "B"; then setenv active_slot A; else setenv active_slot B; fi; setenv upgrade_available 0; setenv bootcount 0; saveenv'
setenv mmcboot 'echo Booting from MMC slot ${active_slot}...; run mmcargs; fatload mmc ${mmcdev}:${mmcpart} ${loadaddr} zImage; fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}'

# Manual NFS helper (run netboot). Menu path uses boot_nfs which is saveenv-safe.
setenv netboot 'echo Booting from NFS...; setenv ethaddr 02:11:22:33:44:55; setenv eth1addr 02:11:22:33:44:56; setenv ethprime eth0; setenv ethact eth0; setenv bootargs console=ttymxc0,115200 root=/dev/nfs rw nfsroot=192.168.5.27:/srv/nfs/nfs_rootfs,nfsvers=3,tcp ip=192.168.5.201:192.168.5.27:192.168.5.1:255.255.255.0::eth0:off; echo bootargs=${bootargs}; if ping 192.168.5.27; then tftp 0x80800000 zImage; tftp 0x83000000 imx6ull-alientek-alpha.dtb; bootz 0x80800000 - 0x83000000; else echo ERROR: ping failed, check cable on ENET2/20b4000; fi'

# Boot menu: remember last choice; first boot defaults to TF; must end with bootmenu
# IMPORTANT: after saveenv, do NOT run long script vars (they may be corrupted in RAM).
# Keep all boot steps inline in boot_tf / boot_nfs after saveenv.
if test -z "${boot_mode}"; then setenv boot_mode mmc; fi
if test -z "${bootmenu_default}"; then setenv bootmenu_default 0; fi

setenv boot_tf 'setenv boot_mode mmc; setenv bootmenu_default 0; saveenv; if test -z "${active_slot}"; then setenv active_slot A; fi; if test "${active_slot}" = "B"; then setenv rootpart 3; else setenv rootpart 2; setenv active_slot A; fi; setenv bootargs console=ttymxc0,115200 root=/dev/mmcblk0p${rootpart} rootwait rw; echo Booting from MMC p${rootpart} slot ${active_slot}...; fatload mmc 0:1 0x80800000 zImage; fatload mmc 0:1 0x83000000 imx6ull-alientek-alpha.dtb; bootz 0x80800000 - 0x83000000'

setenv boot_nfs 'setenv boot_mode nfs; setenv bootmenu_default 1; saveenv; setenv ethaddr 02:11:22:33:44:55; setenv eth1addr 02:11:22:33:44:56; setenv ethprime eth0; setenv ethact eth0; setenv bootargs console=ttymxc0,115200 root=/dev/nfs rw nfsroot=192.168.5.27:/srv/nfs/nfs_rootfs,nfsvers=3,tcp ip=192.168.5.201:192.168.5.27:192.168.5.1:255.255.255.0::eth0:off; echo bootargs=${bootargs}; if ping 192.168.5.27; then tftp 0x80800000 zImage; tftp 0x83000000 imx6ull-alientek-alpha.dtb; bootz 0x80800000 - 0x83000000; else echo ERROR: ping failed, check cable on ENET2/20b4000; fi'

setenv bootmenu_0 'Boot from TF (mmc)=run boot_tf'
setenv bootmenu_1 'Boot from NFS=run boot_nfs'
setenv bootmenu_delay 5

setenv bootcmd bootmenu
bootmenu
