SUMMARY = "LCD 板级控制台（Qt6 linuxfb）"
DESCRIPTION = "传感器、系统信息、LED/蜂鸣器、按键与 OTA 只读状态"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://main.cpp;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "qtbase"
# qtbase-plugins 含 linuxfb/evdev；字体用微米黑
RDEPENDS:${PN} = " \
    qtbase \
    qtbase-plugins \
    ap3216c-module \
    icm20608-module \
    ttf-wqy-microhei \
    libubootenv-bin \
"

FILESEXTRAPATHS:prepend := "${THISDIR}/../common:"

SRC_URI = " \
    file://src/main.cpp \
    file://src/nofocus_style.hpp \
    file://src/dashboard.cpp \
    file://src/dashboard.hpp \
    file://src/sensors.cpp \
    file://src/sensors.hpp \
    file://src/leds.cpp \
    file://src/leds.hpp \
    file://src/sysinfo.cpp \
    file://src/sysinfo.hpp \
    file://src/ota_status.cpp \
    file://src/ota_status.hpp \
    file://src/keys.cpp \
    file://src/keys.hpp \
    file://src/CMakeLists.txt \
    file://iio-icm.c \
    file://iio-icm.h \
    file://input-device.c \
    file://input-device.h \
    file://sensor-dashboard.init \
    file://sensor-dashboard.default \
"
S = "${WORKDIR}/src"

inherit qt6-cmake update-rc.d

INITSCRIPT_NAME = "sensor-dashboard"
INITSCRIPT_PARAMS = "defaults 90"

do_configure:prepend() {
    cp -f "${WORKDIR}/iio-icm.c" "${WORKDIR}/iio-icm.h" \
          "${WORKDIR}/input-device.c" "${WORKDIR}/input-device.h" "${S}/"
}

do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/sensor-dashboard.init \
        ${D}${sysconfdir}/init.d/sensor-dashboard
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/sensor-dashboard.default \
        ${D}${sysconfdir}/default/sensor-dashboard
}

FILES:${PN} += "${sysconfdir}/init.d/sensor-dashboard ${sysconfdir}/default/sensor-dashboard"
