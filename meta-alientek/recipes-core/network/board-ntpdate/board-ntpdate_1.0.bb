SUMMARY = "开机一次性 NTP 校时（BusyBox ntpd）"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

SRC_URI = "file://board-ntpdate.init"

S = "${WORKDIR}"

inherit update-rc.d

# 先于 ap3216c-logger（S20）校时
INITSCRIPT_NAME = "board-ntpdate"
INITSCRIPT_PARAMS = "start 15 2 3 4 5 ."

RDEPENDS:${PN} = "busybox"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/board-ntpdate.init ${D}${sysconfdir}/init.d/board-ntpdate
}

FILES:${PN} += "${sysconfdir}/init.d/board-ntpdate"
