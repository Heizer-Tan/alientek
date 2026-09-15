SUMMARY = "板端 MQTT 会话 Agent（与 OTA 进程分离）"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "paho-mqtt-c"
RDEPENDS:${PN} = "paho-mqtt-c ota-agent"

FILESEXTRAPATHS:prepend := "${THISDIR}/../ota-agent/files/src:"

SRC_URI = " \
    file://src/mqtt-agent.cpp \
    file://src/Makefile \
    file://ota-mqtt.cpp \
    file://ota-mqtt.hpp \
    file://ota-state.cpp \
    file://ota-state.hpp \
    file://mqtt-agent.init \
    file://mqtt-agent.default \
"

S = "${WORKDIR}/src"

inherit update-rc.d
INITSCRIPT_NAME = "mqtt-agent"
INITSCRIPT_PARAMS = "defaults"

EXTRA_OEMAKE = "OTA_MQTT_BACKEND=paho"

do_configure() {
    # FILESEXTRAPATHS 拉来的 ota-* 在 ${WORKDIR}，与 mqtt-agent 源码对齐到 ${S}
    cp -f "${WORKDIR}/ota-mqtt.cpp" "${WORKDIR}/ota-mqtt.hpp" \
        "${WORKDIR}/ota-state.cpp" "${WORKDIR}/ota-state.hpp" "${S}/"
}

do_compile() {
    oe_runmake
}

do_install() {
    install -d "${D}${bindir}"
    install -m 0755 "${S}/mqtt-agent" "${D}${bindir}/mqtt-agent"

    install -d "${D}${localstatedir}/lib/mqtt-agent"

    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/mqtt-agent.init" \
        "${D}${sysconfdir}/init.d/mqtt-agent"

    install -d "${D}${sysconfdir}/default"
    install -m 0644 "${WORKDIR}/mqtt-agent.default" \
        "${D}${sysconfdir}/default/mqtt-agent"
}

FILES:${PN} += " \
    ${sysconfdir}/default/mqtt-agent \
    ${sysconfdir}/init.d/mqtt-agent \
    ${localstatedir}/lib/mqtt-agent \
"
