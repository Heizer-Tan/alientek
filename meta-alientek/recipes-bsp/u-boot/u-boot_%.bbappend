FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:${THISDIR}/../device-tree/alientek-aes:"

SRC_URI += " \
    file://mx6ull_aes_defconfig \
    file://imx6ull-alientek-alpha.dts \
    file://imx6ull-alientek-alpha.dtsi \
    file://boot.cmd \
    file://0001-mx6ullevk-default-fdt-alientek-alpha.patch \
"

# 解包后注入板级 defconfig / 板级 DTS，并登记 Makefile
do_configure:prepend() {
    src_def="${WORKDIR}/mx6ull_aes_defconfig"
    src_dts="${WORKDIR}/imx6ull-alientek-alpha.dts"
    src_dtsi="${WORKDIR}/imx6ull-alientek-alpha.dtsi"
    if [ ! -f "${src_def}" ] || [ ! -f "${src_dts}" ] || [ ! -f "${src_dtsi}" ]; then
        die "未找到阿尔法 U-Boot defconfig 或板级 dts（WORKDIR=${WORKDIR}）"
    fi
    install -D -m 0644 "${src_def}" ${S}/configs/mx6ull_aes_defconfig
    install -D -m 0644 "${src_dts}" ${S}/arch/arm/dts/imx6ull-alientek-alpha.dts
    install -D -m 0644 "${src_dtsi}" ${S}/arch/arm/dts/imx6ull-alientek-alpha.dtsi
    mk="${S}/arch/arm/dts/Makefile"
    if [ ! -f "${mk}" ]; then
        die "未找到 ${mk}，u-boot DTS 布局已变"
    fi
    if ! grep -q 'imx6ull-alientek-alpha.dtb' "${mk}"; then
        awk '{print} /imx6ull-14x14-evk\.dtb/ && !done {print "\timx6ull-alientek-alpha.dtb \\"; done=1}' \
            "${mk}" > "${mk}.tmp" && mv "${mk}.tmp" "${mk}"
    fi
}
