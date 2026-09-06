DEPENDS += "bc-native dtc-native python3-pyelftools-native"

# WKS imx-uboot-bootpart 需要 ${UBOOT_BINARY}.tagged（UUU 分区尾标记）
inherit uuu_bootloader_tag

PACKAGE_ARCH = "${MACHINE_ARCH}"
