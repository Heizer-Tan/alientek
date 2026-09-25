#!/bin/sh
set -eu

recipe="meta-alientek/recipes-core/ota/ota-agent/ota-agent_1.0.bb"
image="meta-alientek/recipes-core/images/alientek-image-base.bb"

for sourceFile in \
    meta-alientek/recipes-core/ota/ota-agent/files/src/Makefile \
    meta-alientek/recipes-core/ota/ota-agent/files/src/ota-agent.hpp \
    meta-alientek/recipes-core/ota/ota-agent/files/src/ota-agent.cpp \
    meta-alientek/recipes-core/ota/ota-agent/files/ota-agent.default \
    meta-alientek/recipes-core/ota/mqtt-agent/mqtt-agent_1.0.bb \
    meta-alientek/recipes-core/ota/mqtt-agent/files/src/mqtt-agent.cpp \
    meta-alientek/recipes-core/ota/mqtt-agent/files/mqtt-agent.init \
    meta-alientek/recipes-core/ota/mqtt-agent/files/mqtt-agent.default
do
    test -f "${sourceFile}"
done

grep -q 'ota-agent' meta-alientek/recipes-core/packagegroups/packagegroup-alientek-core.bb
grep -q 'mqtt-agent' meta-alientek/recipes-core/packagegroups/packagegroup-alientek-core.bb
grep -q 'packagegroup-alientek-core' "${image}"
grep -q 'EXTRA_OEMAKE = "OTA_MQTT_BACKEND=stub"' "${recipe}"
grep -q 'paho-mqtt-c' meta-alientek/recipes-core/ota/mqtt-agent/mqtt-agent_1.0.bb
grep -q '${bindir}/ota-agent' "${recipe}"
grep -q '${sysconfdir}/default/ota-agent' "${recipe}"

echo "ota-agent / mqtt-agent recipe scaffold integrated"
