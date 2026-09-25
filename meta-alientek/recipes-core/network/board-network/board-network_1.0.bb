SUMMARY = "板级静态网络初始化服务"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "iproute2"

SRC_URI = " \
    file://board-network.init \
    file://board-network.default \
"

inherit update-rc.d

INITSCRIPT_NAME = "board-network"
INITSCRIPT_PARAMS = "defaults 10 90"

do_install() {
    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/board-network.init" \
        "${D}${sysconfdir}/init.d/board-network"

    install -d "${D}${sysconfdir}/default"
    install -m 0644 "${WORKDIR}/board-network.default" \
        "${D}${sysconfdir}/default/board-network"
}

FILES:${PN} += " \
    ${sysconfdir}/default/board-network \
    ${sysconfdir}/init.d/board-network \
"
