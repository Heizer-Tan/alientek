setenv fdtfile imx6ull-alientek-alpha.dtb
setenv console ttymxc0,115200
setenv mmcdev 1
setenv mmcpart 1
setenv nfsroot /srv/nfs/alientek

setenv mmcargs "setenv bootargs console=${console} root=/dev/mmcblk1p2 rootwait rw"
setenv mmcboot "echo Booting from MMC...; run mmcargs; fatload mmc ${mmcdev}:${mmcpart} ${loadaddr} zImage; fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

setenv netargs "setenv bootargs console=${console} root=/dev/nfs nfsroot=${serverip}:${nfsroot},nfsvers=3,tcp ip=dhcp"
setenv netboot "echo Booting from NFS...; dhcp; run netargs; tftp ${loadaddr} zImage; tftp ${fdt_addr_r} ${fdtfile}; bootz ${loadaddr} - ${fdt_addr_r}"

setenv bootcmd "run mmcboot"
