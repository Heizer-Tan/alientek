FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += " \
    file://imx6ull-alientek-alpha.dts \
    file://nfs.cfg \
    file://0001-arm-dts-imx-add-imx6ull-alientek-alpha-to-Makefile.patch \
"

do_configure:prepend() {
    install -D -m 0644 ${UNPACKDIR}/imx6ull-alientek-alpha.dts \
        ${S}/arch/arm/boot/dts/nxp/imx/imx6ull-alientek-alpha.dts
}

do_configure:append() {
    if [ -f ${UNPACKDIR}/nfs.cfg ]; then
        ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${UNPACKDIR}/nfs.cfg
    fi
}
