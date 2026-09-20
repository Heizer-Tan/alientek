SUMMARY = "ICM20608 定时采样写入 SQLite"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://icm20608-logger.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3 libsqlite3"

SRC_URI = " \
    file://src/icm20608-logger.c \
    file://src/Makefile \
    file://icm20608-logger.init \
    file://icm20608-logger.default \
"
S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "icm20608-logger"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o icm20608-logger icm20608-logger.c -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/icm20608-logger ${D}${bindir}/icm20608-logger
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/icm20608-logger.init ${D}${sysconfdir}/init.d/icm20608-logger
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/icm20608-logger.default ${D}${sysconfdir}/default/icm20608-logger
    install -d ${D}/var/lib/icm20608
}

FILES:${PN} += "${sysconfdir}/init.d/icm20608-logger ${sysconfdir}/default/icm20608-logger /var/lib/icm20608"
