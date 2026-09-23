SUMMARY = "ICM20608 SPI IIO driver"
DESCRIPTION = "为阿尔法板 ICM20608 导出 iio:device sysfs（accel/anglvel/temp）"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://icm20608.c;beginline=1;endline=1;md5=a9f1449b768f69dcffc44cb5e556b102"

inherit module

SRC_URI = " \
    file://icm20608.c;subdir=src \
    file://Makefile;subdir=src \
"

# file:// 本地源码统一解到 ${WORKDIR}/src，避免直接使用 WORKDIR 根目录。
S = "${WORKDIR}/src"
B = "${S}"

EXTRA_OEMAKE += "KERNELDIR=${STAGING_KERNEL_BUILDDIR}"

do_compile[depends] += "virtual/kernel:do_compile"
do_install[depends] += "virtual/kernel:do_compile"

do_compile:prepend() {
    if [ ! -f ${STAGING_KERNEL_BUILDDIR}/include/config/auto.conf ]; then
        unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
        oe_runmake -C ${STAGING_KERNEL_DIR} O=${STAGING_KERNEL_BUILDDIR} olddefconfig modules_prepare
    fi
}

KERNEL_MODULE_AUTOLOAD += "icm20608"
