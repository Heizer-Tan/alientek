SUMMARY = "板级 A/B 升级辅助脚本与首启确认服务"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "swupdate libubootenv-bin"

SRC_URI = " \
    file://board-apply-update \
    file://board-slot-lib.sh \
    file://board-upgrade-commit.init \
    file://board-upgrade-commit.default \
    file://board-upgrade-healthcheck \
    file://hwrevision \
"

inherit update-rc.d

INITSCRIPT_NAME = "board-upgrade-commit"
INITSCRIPT_PARAMS = "defaults 99 01"

do_install() {
    install -d "${D}${sbindir}"
    install -m 0755 "${WORKDIR}/board-apply-update" "${D}${sbindir}/board-apply-update"
    install -m 0755 "${WORKDIR}/board-upgrade-healthcheck" \
        "${D}${sbindir}/board-upgrade-healthcheck"

    install -d "${D}${datadir}/board-update-tools"
    install -m 0644 "${WORKDIR}/board-slot-lib.sh" \
        "${D}${datadir}/board-update-tools/board-slot-lib.sh"

    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/board-upgrade-commit.init" \
        "${D}${sysconfdir}/init.d/board-upgrade-commit"

    install -d "${D}${sysconfdir}/default"
    install -m 0644 "${WORKDIR}/board-upgrade-commit.default" \
        "${D}${sysconfdir}/default/board-upgrade-commit"

    install -d "${D}${sysconfdir}"
    install -m 0644 "${WORKDIR}/hwrevision" "${D}${sysconfdir}/hwrevision"
}

FILES:${PN} += " \
    ${sysconfdir}/hwrevision \
    ${sysconfdir}/default/board-upgrade-commit \
    ${sysconfdir}/init.d/board-upgrade-commit \
    ${datadir}/board-update-tools \
    ${sbindir}/board-upgrade-healthcheck \
"
