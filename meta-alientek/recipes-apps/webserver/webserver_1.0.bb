SUMMARY = "通用板端 Web：查询传感器历史"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://webserver.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3"

SRC_URI = " \
    file://src/webserver.c \
    file://src/index.html \
    file://src/Makefile \
    file://webserver.init \
"
S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "webserver"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o webserver webserver.c -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/webserver ${D}${bindir}/webserver
    install -d ${D}${datadir}/webserver
    install -m 0644 ${S}/index.html ${D}${datadir}/webserver/index.html
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/webserver.init ${D}${sysconfdir}/init.d/webserver
}

FILES:${PN} += "${datadir}/webserver ${sysconfdir}/init.d/webserver"
