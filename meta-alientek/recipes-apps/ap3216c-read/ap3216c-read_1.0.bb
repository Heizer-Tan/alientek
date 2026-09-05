SUMMARY = "Read AP3216C values"
DESCRIPTION = "读取 /dev/ap3216c 并打印 IR/ALS/PS"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://ap3216c-read.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

SRC_URI = " \
    file://src/ap3216c-read.c \
    file://src/Makefile \
"

S = "${WORKDIR}/src"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o ap3216c-read ap3216c-read.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/ap3216c-read ${D}${bindir}/ap3216c-read
}
