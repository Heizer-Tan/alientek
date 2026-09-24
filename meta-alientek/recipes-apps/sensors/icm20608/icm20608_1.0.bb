SUMMARY = "ICM20608 user tools via IIO (read + logger)"
DESCRIPTION = "一 recipe 产出 icm20608-read 与 icm20608-logger 两包"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "\
    file://icm20608-read.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6 \
    file://icm20608-logger.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6 \
"

DEPENDS = "sqlite3"

FILESEXTRAPATHS:prepend := "${THISDIR}/../../common:"

SRC_URI = " \
    file://src/icm20608-read.c \
    file://src/icm20608-logger.c \
    file://iio-icm.c \
    file://iio-icm.h \
    file://icm20608-logger.init \
    file://icm20608-logger.default \
"

S = "${WORKDIR}/src"

PACKAGES =+ "icm20608-read icm20608-logger"
ALLOW_EMPTY:${PN} = "1"
FILES:${PN} = ""

RDEPENDS:icm20608-logger = "sqlite3 libsqlite3"

inherit update-rc.d
INITSCRIPT_PACKAGES = "icm20608-logger"
INITSCRIPT_NAME:icm20608-logger = "icm20608-logger"
INITSCRIPT_PARAMS:icm20608-logger = "defaults"

do_configure() {
    cp -f "${WORKDIR}/iio-icm.c" "${WORKDIR}/iio-icm.h" "${S}/"
}

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o icm20608-read icm20608-read.c iio-icm.c -lm
    ${CC} ${CFLAGS} ${LDFLAGS} -o icm20608-logger icm20608-logger.c iio-icm.c -lsqlite3 -lm
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/icm20608-read ${D}${bindir}/icm20608-read
    install -m 0755 ${S}/icm20608-logger ${D}${bindir}/icm20608-logger

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/icm20608-logger.init \
        ${D}${sysconfdir}/init.d/icm20608-logger
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/icm20608-logger.default \
        ${D}${sysconfdir}/default/icm20608-logger
    install -d ${D}/var/lib/icm20608
}

FILES:icm20608-read = "${bindir}/icm20608-read"
FILES:icm20608-logger = "\
    ${bindir}/icm20608-logger \
    ${sysconfdir}/init.d/icm20608-logger \
    ${sysconfdir}/default/icm20608-logger \
    /var/lib/icm20608 \
"
