DESCRIPTION = "阿尔法板 SWUpdate A/B 单文件升级包"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit swupdate

SRC_URI = " \
    file://sw-description \
"

IMAGE_DEPENDS = " \
    alientek-image-base \
    u-boot \
    virtual/kernel \
"

SWUPDATE_IMAGES = " \
    alientek-image-base \
    zImage \
    imx6ull-alientek-alpha.dtb \
    boot.scr \
    u-boot.imx \
"

SWUPDATE_IMAGES_FSTYPES[alientek-image-base] = ".rootfs.ext4.gz"
