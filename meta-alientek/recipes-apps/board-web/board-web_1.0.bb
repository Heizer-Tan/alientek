SUMMARY = "通用板端 Web：查询传感器历史"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://board-web.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3"

SRC_URI = " \
    file://src/board-web.c \
    file://src/index.html \
    file://src/Makefile \
    file://board-web.init \
"
S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "board-web"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o board-web board-web.c -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/board-web ${D}${bindir}/board-web
    install -d ${D}${datadir}/board-web
    install -m 0644 ${S}/index.html ${D}${datadir}/board-web/index.html
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/board-web.init ${D}${sysconfdir}/init.d/board-web
}

FILES:${PN} += "${datadir}/board-web ${sysconfdir}/init.d/board-web"
