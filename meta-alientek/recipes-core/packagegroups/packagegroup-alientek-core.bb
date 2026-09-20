SUMMARY = "阿尔法核心业务包：网络/NTP/传感器入库/Web/OTA"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# allarch packagegroup 不能依赖会被 debian 规则改名的库（如 libubootenv→libubootenv0）
PACKAGE_ARCH = "${TUNE_PKGARCH}"

inherit packagegroup

RDEPENDS:${PN} = " \
    curl \
    ethtool \
    iproute2 \
    iputils \
    i2c-tools \
    libubootenv-bin \
    ap3216c-module \
    ap3216c-logger \
    icm20608-module \
    icm20608-logger \
    board-network \
    board-ntpdate \
    busybox-hwclock \
    webserver \
    board-update-tools \
    ota-agent \
    mqtt-agent \
    swupdate \
    sqlite3 \
    openssh-keygen \
"
