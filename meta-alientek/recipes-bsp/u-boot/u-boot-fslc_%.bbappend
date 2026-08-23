FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += " \
    file://0001-configs-add-mx6ull_alientek_alpha_defconfig.patch \
    file://boot.cmd \
"
