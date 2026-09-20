SUMMARY = "通用板端 Web：查询传感器历史与固件升级"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://webserver.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3 board-update-tools"

SRC_URI = " \
    file://src/webserver.c \
    file://src/webserver.h \
    file://src/webserver-http.c \
    file://src/webserver-samples.c \
    file://src/webserver-icm.c \
    file://src/webserver-upgrade.c \
    file://src/index.html \
    file://src/icm20608.html \
    file://src/Makefile \
    file://webserver.init \
    file://webserver.default \
"
S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "webserver"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o webserver \
        webserver.c webserver-http.c webserver-samples.c webserver-icm.c \
        webserver-upgrade.c \
        -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/webserver ${D}${bindir}/webserver
    install -d ${D}${datadir}/webserver
    install -m 0644 ${S}/index.html ${D}${datadir}/webserver/index.html
    install -m 0644 ${S}/icm20608.html ${D}${datadir}/webserver/icm20608.html
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/webserver.init ${D}${sysconfdir}/init.d/webserver
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/webserver.default ${D}${sysconfdir}/default/webserver
}

FILES:${PN} += "${datadir}/webserver ${sysconfdir}/init.d/webserver ${sysconfdir}/default/webserver"
