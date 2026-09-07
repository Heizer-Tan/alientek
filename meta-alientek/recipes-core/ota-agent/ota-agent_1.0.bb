SUMMARY = "板端 MQTT OTA Agent"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "curl coreutils board-update-tools"

SRC_URI = " \
    file://src/ota-agent.c \
    file://src/ota-agent.h \
    file://src/ota-state.c \
    file://src/ota-state.h \
    file://src/Makefile \
    file://ota-agent.init \
    file://ota-agent.default \
"

S = "${WORKDIR}/src"

inherit update-rc.d

INITSCRIPT_NAME = "ota-agent"
INITSCRIPT_PARAMS = "defaults"

do_compile() {
    oe_runmake
}

do_install() {
    install -d "${D}${bindir}"
    install -m 0755 "${S}/ota-agent" "${D}${bindir}/ota-agent"

    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/ota-agent.init" \
        "${D}${sysconfdir}/init.d/ota-agent"

    install -d "${D}${sysconfdir}/default"
    install -m 0644 "${WORKDIR}/ota-agent.default" \
        "${D}${sysconfdir}/default/ota-agent"
}

FILES:${PN} += " \
    ${sysconfdir}/default/ota-agent \
    ${sysconfdir}/init.d/ota-agent \
"
