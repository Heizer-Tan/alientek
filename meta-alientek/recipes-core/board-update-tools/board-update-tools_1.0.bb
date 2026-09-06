SUMMARY = "板级 A/B 升级辅助脚本与首启确认服务"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "swupdate libubootenv-bin"

SRC_URI = " \
    file://board-apply-update \
    file://board-upgrade-commit.init \
"

inherit update-rc.d

INITSCRIPT_NAME = "board-upgrade-commit"
INITSCRIPT_PARAMS = "defaults 99 01"

do_install() {
    install -d "${D}${sbindir}"
    install -m 0755 "${WORKDIR}/board-apply-update" "${D}${sbindir}/board-apply-update"

    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/board-upgrade-commit.init" \
        "${D}${sysconfdir}/init.d/board-upgrade-commit"
}

FILES:${PN} += " \
    ${sysconfdir}/init.d/board-upgrade-commit \
"
