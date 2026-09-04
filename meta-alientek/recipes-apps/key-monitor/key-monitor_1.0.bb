SUMMARY = "读取 gpio-keys 的 EV_KEY 并打印 type/code/value"
DESCRIPTION = "前台或 SysV 可选服务；默认不开机自启"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://key-monitor.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

# file://src/... 解压到 ${WORKDIR}/src/，避免 S=WORKDIR 触发 pseudo path mismatch
SRC_URI = " \
    file://src/key-monitor.c \
    file://src/Makefile \
    file://key-monitor.init \
"

S = "${WORKDIR}/src"

# 不用 oe_runmake install：make -j 的 /tmp/GMfifo* 会在 kas-container 里触发 Pseudo abort
do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o key-monitor key-monitor.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/key-monitor ${D}${bindir}/key-monitor
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/key-monitor.init ${D}${sysconfdir}/init.d/key-monitor
}

# 不 inherit update-rc.d：只安装脚本，默认不 enable
FILES:${PN} += "${sysconfdir}/init.d/key-monitor"
