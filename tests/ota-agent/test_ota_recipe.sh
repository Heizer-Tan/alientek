#!/bin/sh
set -eu

recipe="meta-alientek/recipes-core/ota-agent/ota-agent_1.0.bb"
image="meta-alientek/recipes-core/images/alientek-image-base.bb"

for sourceFile in \
    meta-alientek/recipes-core/ota-agent/files/src/Makefile \
    meta-alientek/recipes-core/ota-agent/files/src/ota-agent.hpp \
    meta-alientek/recipes-core/ota-agent/files/src/ota-agent.cpp \
    meta-alientek/recipes-core/ota-agent/files/ota-agent.init \
    meta-alientek/recipes-core/ota-agent/files/ota-agent.default
do
    test -f "${sourceFile}"
done

grep -q 'ota-agent' "${image}"
grep -q 'inherit update-rc.d' "${recipe}"
grep -q 'INITSCRIPT_NAME = "ota-agent"' "${recipe}"
if ! grep -q 'RDEPENDS:${PN} = "curl coreutils board-update-tools"' "${recipe}"; then
    echo "expected: coreutils provides sha256sum without a standalone package" >&2
    exit 1
fi
grep -q '${bindir}/ota-agent' "${recipe}"
grep -q '${sysconfdir}/default/ota-agent' "${recipe}"
grep -q '${sysconfdir}/init.d/ota-agent' "${recipe}"

echo "ota-agent recipe scaffold integrated"
