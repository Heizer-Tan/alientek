SUMMARY = "板端 OTA Agent（下载/校验/刷写/恢复，不连 MQTT）"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "curl coreutils board-update-tools"

SRC_URI = " \
    file://src/ota-agent.cpp \
    file://src/ota-agent.hpp \
    file://src/ota-state.cpp \
    file://src/ota-state.hpp \
    file://src/ota-download.cpp \
    file://src/ota-download.hpp \
    file://src/ota-exec.cpp \
    file://src/ota-exec.hpp \
    file://src/ota-mqtt.cpp \
    file://src/ota-mqtt.hpp \
    file://src/Makefile \
    file://ota-agent.default \
"

S = "${WORKDIR}/src"

# MQTT 发布由 mqtt-agent 负责；本包仅保留 JSON 解析（stub 后端）
EXTRA_OEMAKE = "OTA_MQTT_BACKEND=stub"

do_compile() {
    oe_runmake
}

do_install() {
    install -d "${D}${bindir}"
    install -m 0755 "${S}/ota-agent" "${D}${bindir}/ota-agent"

    install -d "${D}${localstatedir}/lib/ota-agent"

    install -d "${D}${sysconfdir}/default"
    install -m 0644 "${WORKDIR}/ota-agent.default" \
        "${D}${sysconfdir}/default/ota-agent"
}

FILES:${PN} += " \
    ${sysconfdir}/default/ota-agent \
    ${localstatedir}/lib/ota-agent \
"
