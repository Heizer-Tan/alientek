SUMMARY = "Read ICM20608 values via IIO"
DESCRIPTION = "读取 IIO sysfs（name=icm20608）并打印 accel/gyro/temp"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://icm20608-read.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6"

FILESEXTRAPATHS:prepend := "${THISDIR}/../common:"

SRC_URI = " \
    file://src/icm20608-read.c \
    file://src/Makefile \
    file://iio-icm.c \
    file://iio-icm.h \
"

S = "${WORKDIR}/src"

do_configure() {
    cp -f "${WORKDIR}/iio-icm.c" "${WORKDIR}/iio-icm.h" "${S}/"
}

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o icm20608-read icm20608-read.c iio-icm.c -lm
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/icm20608-read ${D}${bindir}/icm20608-read
}
