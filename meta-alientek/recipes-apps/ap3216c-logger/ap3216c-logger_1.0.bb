SUMMARY = "AP3216C 定时采样写入 SQLite"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://ap3216c-logger.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3"

SRC_URI = " \
    file://src/ap3216c-logger.c \
    file://src/Makefile \
    file://ap3216c-logger.init \
"
S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "ap3216c-logger"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o ap3216c-logger ap3216c-logger.c -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/ap3216c-logger ${D}${bindir}/ap3216c-logger
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/ap3216c-logger.init ${D}${sysconfdir}/init.d/ap3216c-logger
    install -d ${D}/var/lib/ap3216c
}

FILES:${PN} += "${sysconfdir}/init.d/ap3216c-logger /var/lib/ap3216c"
