SUMMARY = "Alientek board LED misc driver"
DESCRIPTION = "platform 驱动，导出 /dev/alientek-led（compatible = alientek,led）"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://alientek-led.c;beginline=1;endline=1;md5=a9f1449b768f69dcffc44cb5e556b102"

inherit module

SRC_URI = " \
    file://alientek-led.c;subdir=src \
    file://Makefile;subdir=src \
"

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

KERNEL_MODULE_AUTOLOAD += "alientek-led"
