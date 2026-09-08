SUMMARY = "Mainline Linux kernel for Alientek i.MX6ULL Alpha (AES DT)"
DESCRIPTION = "kernel.org stable 7.2.4 with Alientek AES board device tree"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

inherit kernel

LINUX_VERSION = "7.2.4"
PV = "${LINUX_VERSION}"
LOCALVERSION = "-alientek"

# 官方路径：${KERNELORG_MIRROR}/linux/kernel/v7.x/...
# 清华实际是 /kernel/v7.x/（无 linux/ 段）；在 kas 里用 MIRRORS 改写
SRC_URI = "${KERNELORG_MIRROR}/linux/kernel/v7.x/linux-${PV}.tar.xz \
    file://nfs.cfg \
    file://imx6ull-alientek-alpha.dts \
    file://imx6ull-alientek-alpha.dtsi \
"
SRC_URI[sha256sum] = "01710ee01737dac492f1bae52becd057e08d20d11589089aa06accff415c28dd"

S = "${WORKDIR}/linux-${PV}"

COMPATIBLE_MACHINE = "imx6ull-alientek-alpha"
KBUILD_DEFCONFIG = "imx_v6_v7_defconfig"
KCONFIG_MODE = "--alldefconfig"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:${THISDIR}/../../recipes-bsp/device-tree/alientek-aes:"

KERNEL_EXTRA_ARGS += "LOADADDR=${UBOOT_ENTRYPOINT}"

do_configure:prepend() {
    # 拷板级 AES DTS；若无 imx_v6_v7_defconfig 则回退 multi_v7
    if [ ! -f "${S}/arch/${ARCH}/configs/${KBUILD_DEFCONFIG}" ]; then
        KBUILD_DEFCONFIG="multi_v7_defconfig"
        export KBUILD_DEFCONFIG
    fi
    if [ ! -f "${B}/.config" ] && [ -f "${S}/arch/${ARCH}/configs/${KBUILD_DEFCONFIG}" ]; then
        oe_runmake -C ${S} O=${B} ${KBUILD_DEFCONFIG}
    fi

    dts_dir="${S}/arch/arm/boot/dts/nxp/imx"
    install -D -m 0644 ${WORKDIR}/imx6ull-alientek-alpha.dts ${dts_dir}/imx6ull-alientek-alpha.dts
    install -D -m 0644 ${WORKDIR}/imx6ull-alientek-alpha.dtsi ${dts_dir}/imx6ull-alientek-alpha.dtsi
    mk="${dts_dir}/Makefile"
    if [ ! -f "${mk}" ]; then
        die "未找到 ${mk}，Linux 7.2 DTS 布局已变"
    fi
    if ! grep -q 'imx6ull-alientek-alpha.dtb' "${mk}"; then
        awk '{print} /imx6ull-14x14-evk\.dtb/ && !done {print "\timx6ull-alientek-alpha.dtb \\"; done=1}' \
            "${mk}" > "${mk}.tmp" && mv "${mk}.tmp" "${mk}"
    fi
}

do_configure:append() {
    if [ -f ${WORKDIR}/nfs.cfg ]; then
        ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${WORKDIR}/nfs.cfg
    fi
}
