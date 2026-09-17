SUMMARY = "阿尔法演示/调试工具：按键、触摸、传感器即时读取"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${TUNE_PKGARCH}"

inherit packagegroup

RDEPENDS:${PN} = " \
    key-monitor \
    touch-monitor \
    ap3216c-read \
"
