SUMMARY = "读取电容触摸 input 事件流（默认 Goodix GT9147）"
DESCRIPTION = "前台工具；默认查找名称含 Goodix 的 event 设备并打印触摸流"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://touch-monitor.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

FILESEXTRAPATHS:prepend := "${THISDIR}/../common:"

SRC_URI = " \
    file://src/touch-monitor.c \
    file://src/Makefile \
    file://input-device.c \
    file://input-device.h \
"

S = "${WORKDIR}/src"

do_configure() {
    cp -f "${WORKDIR}/input-device.c" "${WORKDIR}/input-device.h" "${S}/"
}

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o touch-monitor touch-monitor.c input-device.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/touch-monitor ${D}${bindir}/touch-monitor
}
