SUMMARY = "AP3216C I2C misc driver"
DESCRIPTION = "为阿尔法板 AP3216C 导出 /dev/ap3216c"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://ap3216c.c;beginline=1;endline=1;md5=a9f1449b768f69dcffc44cb5e556b102"

inherit module

SRC_URI = " \
    file://ap3216c.c;subdir=src \
    file://Makefile;subdir=src \
"

# file:// 本地源码统一解到 ${WORKDIR}/src，避免直接使用 WORKDIR 根目录。
S = "${WORKDIR}/src"
B = "${S}"

# 直接绑定到已准备好的内核 build 目录，避免依赖外层 O= 透传
EXTRA_OEMAKE += "KERNELDIR=${STAGING_KERNEL_BUILDDIR}"

# 依赖内核 do_compile；若共享构建产物被清掉，则补一次 modules_prepare
#do_compile/do_install 依赖保证任务顺序，prepend 负责产物自愈
#do_compile[depends] 只看任务戳，不保证 auto.conf 文件仍在
#do_configure 仍由 module.bbclass 执行 clean

do_compile[depends] += "virtual/kernel:do_compile"
do_install[depends] += "virtual/kernel:do_compile"

do_compile:prepend() {
    if [ ! -f ${STAGING_KERNEL_BUILDDIR}/include/config/auto.conf ]; then
        unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
        oe_runmake -C ${STAGING_KERNEL_DIR} O=${STAGING_KERNEL_BUILDDIR} olddefconfig modules_prepare
    fi
}

KERNEL_MODULE_AUTOLOAD += "ap3216c"
