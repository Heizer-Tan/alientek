SUMMARY = "板级 A/B 升级辅助脚本、启动确认与看门狗喂狗"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "swupdate libubootenv-bin"

SRC_URI = " \
    file://ota-apply \
    file://board-slot-lib.sh \
    file://board-upgrade-commit.init \
    file://board-upgrade-commit.default \
    file://board-upgrade-healthcheck \
    file://board-boot-confirm.init \
    file://board-boot-confirm.default \
    file://board-watchdog-feed \
    file://hwrevision \
"

inherit update-rc.d

# 两个 SysV 服务：启动确认（早）+ OTA 提交（晚）
INITSCRIPT_PACKAGES = "${PN}-boot-confirm ${PN}"
INITSCRIPT_NAME:${PN}-boot-confirm = "board-boot-confirm"
INITSCRIPT_PARAMS:${PN}-boot-confirm = "defaults 12"
INITSCRIPT_NAME:${PN} = "board-upgrade-commit"
INITSCRIPT_PARAMS:${PN} = "defaults 99 01"

PACKAGES =+ "${PN}-boot-confirm"
FILES:${PN}-boot-confirm = " \
    ${sysconfdir}/init.d/board-boot-confirm \
    ${sysconfdir}/default/board-boot-confirm \
    ${sbindir}/board-watchdog-feed \
"
RDEPENDS:${PN}-boot-confirm = "libubootenv-bin"
RDEPENDS:${PN} += "${PN}-boot-confirm"

do_install() {
    install -d "${D}${sbindir}"
    install -m 0755 "${WORKDIR}/ota-apply" "${D}${sbindir}/ota-apply"
    # 兼容旧命令名
    ln -sf ota-apply "${D}${sbindir}/board-apply-update"
    install -m 0755 "${WORKDIR}/board-upgrade-healthcheck" \
        "${D}${sbindir}/board-upgrade-healthcheck"
    install -m 0755 "${WORKDIR}/board-watchdog-feed" \
        "${D}${sbindir}/board-watchdog-feed"

    install -d "${D}${datadir}/board-update-tools"
    install -m 0644 "${WORKDIR}/board-slot-lib.sh" \
        "${D}${datadir}/board-update-tools/board-slot-lib.sh"

    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/board-upgrade-commit.init" \
        "${D}${sysconfdir}/init.d/board-upgrade-commit"
    install -m 0755 "${WORKDIR}/board-boot-confirm.init" \
        "${D}${sysconfdir}/init.d/board-boot-confirm"

    install -d "${D}${sysconfdir}/default"
    install -m 0644 "${WORKDIR}/board-upgrade-commit.default" \
        "${D}${sysconfdir}/default/board-upgrade-commit"
    install -m 0644 "${WORKDIR}/board-boot-confirm.default" \
        "${D}${sysconfdir}/default/board-boot-confirm"

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
