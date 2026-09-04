require u-boot-common_2025.04.inc
require recipes-bsp/u-boot/u-boot.inc

DEPENDS += "bc-native dtc-native python3-pyelftools-native"

# WKS imx-uboot-bootpart 需要 ${UBOOT_BINARY}.tagged（UUU 分区尾标记）
inherit uuu_bootloader_tag

PACKAGE_ARCH = "${MACHINE_ARCH}"
