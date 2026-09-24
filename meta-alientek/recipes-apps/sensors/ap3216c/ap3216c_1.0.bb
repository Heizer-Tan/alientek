SUMMARY = "AP3216C user tools (read + logger)"
DESCRIPTION = "一 recipe 产出 ap3216c-read 与 ap3216c-logger 两包"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "\
    file://ap3216c-read.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6 \
    file://ap3216c-logger.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6 \
"

DEPENDS = "sqlite3"

SRC_URI = " \
    file://src/ap3216c-read.c \
    file://src/ap3216c-logger.c \
    file://ap3216c-logger.init \
    file://ap3216c-logger.default \
"

S = "${WORKDIR}/src"

PACKAGES =+ "ap3216c-read ap3216c-logger"
ALLOW_EMPTY:${PN} = "1"
FILES:${PN} = ""

RDEPENDS:ap3216c-logger = "sqlite3 libsqlite3"

inherit update-rc.d
INITSCRIPT_PACKAGES = "ap3216c-logger"
INITSCRIPT_NAME:ap3216c-logger = "ap3216c-logger"
INITSCRIPT_PARAMS:ap3216c-logger = "defaults"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o ap3216c-read ap3216c-read.c
    ${CC} ${CFLAGS} ${LDFLAGS} -o ap3216c-logger ap3216c-logger.c -lsqlite3
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/ap3216c-read ${D}${bindir}/ap3216c-read
    install -m 0755 ${S}/ap3216c-logger ${D}${bindir}/ap3216c-logger

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/ap3216c-logger.init \
        ${D}${sysconfdir}/init.d/ap3216c-logger
    install -d ${D}${sysconfdir}/default
    install -m 0644 ${WORKDIR}/ap3216c-logger.default \
        ${D}${sysconfdir}/default/ap3216c-logger
    install -d ${D}/var/lib/ap3216c
}

FILES:ap3216c-read = "${bindir}/ap3216c-read"
FILES:ap3216c-logger = "\
    ${bindir}/ap3216c-logger \
    ${sysconfdir}/init.d/ap3216c-logger \
    ${sysconfdir}/default/ap3216c-logger \
    /var/lib/ap3216c \
"
