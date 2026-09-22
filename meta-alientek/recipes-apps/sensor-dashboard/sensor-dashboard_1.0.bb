SUMMARY = "LCD 传感器仪表盘（Qt6 linuxfb）"
DESCRIPTION = "主页卡片进入 AP3216C / ICM20608 详情，触摸操作"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://main.cpp;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "qtbase"
# qtbase-plugins 含 linuxfb/evdev；字体优先文泉驿正黑（meta-oe）
RDEPENDS:${PN} = " \
    qtbase \
    qtbase-plugins \
    ap3216c-module \
    icm20608-module \
    ttf-wqy-zenhei \
"

SRC_URI = " \
    file://src/main.cpp \
    file://src/dashboard.cpp \
    file://src/dashboard.hpp \
    file://src/sensors.cpp \
    file://src/sensors.hpp \
    file://src/CMakeLists.txt \
    file://sensor-dashboard.init \
    file://sensor-dashboard.default \
"
S = "${WORKDIR}/src"

inherit qt6-cmake update-rc.d

INITSCRIPT_NAME = "sensor-dashboard"
INITSCRIPT_PARAMS = "defaults 90"

do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/sensor-dashboard.init \
        ${D}${sysconfdir}/init.d/sensor-dashboard
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/sensor-dashboard.default \
        ${D}${sysconfdir}/default/sensor-dashboard
}

FILES:${PN} += "${sysconfdir}/init.d/sensor-dashboard ${sysconfdir}/default/sensor-dashboard"
