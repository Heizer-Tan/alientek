SUMMARY = "LCD 传感器仪表盘（LVGL）"
DESCRIPTION = "主页卡片进入 AP3216C / ICM20608 详情，触摸操作"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://main.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "lvgl"
RDEPENDS:${PN} = "lvgl ap3216c-module icm20608-module"

SRC_URI = " \
    file://src/main.c \
    file://src/ui.c \
    file://src/ui.h \
    file://src/sensors.c \
    file://src/sensors.h \
    file://src/lv_font_dashboard.c \
    file://src/Makefile \
    file://sensor-dashboard.init \
    file://sensor-dashboard.default \
"
S = "${WORKDIR}/src"

inherit update-rc.d pkgconfig

INITSCRIPT_NAME = "sensor-dashboard"
INITSCRIPT_PARAMS = "defaults 90"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        $(pkg-config --cflags lvgl 2>/dev/null || echo "-I${STAGING_INCDIR}/lvgl") \
        -o sensor-dashboard main.c ui.c sensors.c lv_font_dashboard.c \
        $(pkg-config --libs lvgl 2>/dev/null || echo "-llvgl") \
        -lm -lpthread
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/sensor-dashboard ${D}${bindir}/sensor-dashboard
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/sensor-dashboard.init \
        ${D}${sysconfdir}/init.d/sensor-dashboard
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/sensor-dashboard.default \
        ${D}${sysconfdir}/default/sensor-dashboard
}

FILES:${PN} += "${sysconfdir}/init.d/sensor-dashboard ${sysconfdir}/default/sensor-dashboard"
