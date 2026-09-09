#!/bin/sh
set -eu

projectRoot="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
sourceDir="${projectRoot}/meta-alientek/recipes-core/ota-agent/files/src"
tempDir="$(mktemp -d)"
mockBin="${tempDir}/bin"
agentBinary="${sourceDir}/ota-agent"
servicePid=""

cleanup() {
    if [ -n "${servicePid}" ]; then
        kill "${servicePid}" 2>/dev/null || true
        wait "${servicePid}" 2>/dev/null || true
    fi
    make -C "${sourceDir}" clean >/dev/null 2>&1 || true
    rm -rf "${tempDir}"
}
trap cleanup EXIT INT TERM

for sourceFile in ota-download.cpp ota-download.hpp ota-exec.cpp ota-exec.hpp; do
    if [ ! -f "${sourceDir}/${sourceFile}" ]; then
        printf 'expected failure: 下载/校验/升级执行接口尚未实现 (%s)\n' "${sourceFile}" >&2
        exit 1
    fi
done

mkdir -p "${mockBin}"
printf 'test swu payload\n' >"${tempDir}/package.swu"
expectedSha256="$(sha256sum "${tempDir}/package.swu" | awk '{print $1}')"

cat >"${mockBin}/curl" <<'EOF'
#!/bin/sh
set -eu
outputPath=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) outputPath="$2"; shift 2 ;;
        --) shift; break ;;
        *) shift ;;
    esac
done
test "$#" -eq 1
printf '%s\n' "$1" >"${MOCK_CURL_LOG}"
cp "${MOCK_PACKAGE}" "${outputPath}"
EOF

cat >"${mockBin}/fw_printenv" <<'EOF'
#!/bin/sh
set -eu
test "$1" = "-n"
case "$2" in
    active_slot) printf '%s\n' "${MOCK_ACTIVE_SLOT}" ;;
    upgrade_available) printf '%s\n' "${MOCK_UPGRADE_AVAILABLE}" ;;
    *) exit 2 ;;
esac
EOF

cat >"${mockBin}/board-apply-update" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$@" >"${MOCK_UPGRADE_LOG}"
EOF
cat >"${mockBin}/fw_setenv" <<'EOF'
#!/bin/sh
set -eu
printf '%s=%s\n' "$1" "${2-}" >>"${MOCK_FW_SETENV_LOG}"
EOF
chmod +x "${mockBin}/curl" "${mockBin}/fw_printenv" \
    "${mockBin}/board-apply-update" "${mockBin}/fw_setenv"

make -C "${sourceDir}" clean >/dev/null
make -C "${sourceDir}" CXXFLAGS='-O2 -Wall -Wextra -Werror -std=c++17' >/dev/null

runAgent() {
    MOCK_PACKAGE="${tempDir}/package.swu" \
        MOCK_FW_SETENV_LOG="${tempDir}/fw-setenv.log" \
        MOCK_CURL_LOG="${tempDir}/curl.log" \
        MOCK_UPGRADE_LOG="${tempDir}/upgrade.log" \
        MOCK_UPGRADE_AVAILABLE="$1" \
        MOCK_ACTIVE_SLOT="A" \
        OTA_AGENT_LOCK_FILE="${tempDir}/ota-agent.lock" \
        OTA_AGENT_STATE_FILE="${tempDir}/state.json" \
        PATH="${mockBin}:${PATH}" \
        "${agentBinary}" --apply \
        "https://updates.example.test/package.swu" \
        "${expectedSha256}" "${tempDir}/downloaded.swu" --reboot
}

if runAgent 1 >"${tempDir}/guard.out" 2>"${tempDir}/guard.err"; then
    echo "错误：待提交升级未被拒绝" >&2
    exit 1
fi
grep -q 'upgrade_available=1' "${tempDir}/guard.err"
test ! -e "${tempDir}/curl.log"
test ! -e "${tempDir}/upgrade.log"

MOCK_UPGRADE_AVAILABLE=0 \
    MOCK_ACTIVE_SLOT="A" \
    MOCK_FW_SETENV_LOG="${tempDir}/fw-setenv.log" \
    OTA_AGENT_LOCK_FILE="${tempDir}/ota-agent.lock" \
    OTA_AGENT_STATE_FILE="${tempDir}/state.json" \
    PATH="${mockBin}:${PATH}" \
    "${agentBinary}" >"${tempDir}/service.out" 2>"${tempDir}/service.err" &
servicePid="$!"
sleep 1
if ! kill -0 "${servicePid}" 2>/dev/null; then
    echo "错误：常驻 ota-agent 未保持运行" >&2
    exit 1
fi

runAgent 0 >"${tempDir}/normal.out" 2>"${tempDir}/normal.err"
cmp "${tempDir}/package.swu" "${tempDir}/downloaded.swu"
grep -qx 'https://updates.example.test/package.swu' "${tempDir}/curl.log"
test "$(sed -n '1p' "${tempDir}/upgrade.log")" = "--reboot"
test "$(sed -n '2p' "${tempDir}/upgrade.log")" = "${tempDir}/downloaded.swu"
if ! grep -Eq '"autoReboot":[[:space:]]*1' "${tempDir}/state.json"; then
    echo "错误：自动重启选项未写入 OTA 状态" >&2
    exit 1
fi

cat >"${tempDir}/version-test.c" <<'EOF'
#include "ota-exec.hpp"
#include <stdio.h>

int main(void)
{
    char version[32];

    if (otaReadCurrentVersion(version, sizeof(version)) != 0) {
        return 1;
    }
    return printf("%s\n", version) < 0;
}
EOF
printf 'NAME=Test\nVERSION_ID="3.2.1"\n' >"${tempDir}/os-release"
g++ -O2 -Wall -Wextra -Werror -std=c++17 -x c++ -I"${sourceDir}" \
    "${tempDir}/version-test.c" "${sourceDir}/ota-exec.cpp" \
    "${sourceDir}/ota-state.cpp" \
    -o "${tempDir}/version-test"
OTA_AGENT_OS_RELEASE_FILE="${tempDir}/os-release" \
    "${tempDir}/version-test" >"${tempDir}/version.out"
grep -qx '3.2.1' "${tempDir}/version.out"

cat >"${tempDir}/os-release" <<'EOF'
NAME=Test
VERSION_ID="3.2.1"
EOF
cat >"${tempDir}/state.json" <<'EOF'
{
  "requestId": "req-dup",
  "version": "3.2.1",
  "targetSlot": "B",
  "phase": "completed",
  "result": "success",
  "detail": "done",
  "autoReboot": 0
}
EOF
duplicateCommandJson="$(printf \
    '{"requestId":"req-dup","version":"3.2.2","url":"https://updates.example.test/package.swu","sha256":"%s","autoReboot":false}' \
    "${expectedSha256}")"
if PATH="${mockBin}:${PATH}" \
    OTA_AGENT_OS_RELEASE_FILE="${tempDir}/os-release" \
    MOCK_UPGRADE_AVAILABLE=0 \
    MOCK_ACTIVE_SLOT="A" \
    MOCK_FW_SETENV_LOG="${tempDir}/fw-setenv.log" \
    OTA_AGENT_LOCK_FILE="${tempDir}/ota-agent.lock" \
    OTA_AGENT_STATE_FILE="${tempDir}/state.json" \
    "${agentBinary}" --mqtt-command "${duplicateCommandJson}" \
    "${tempDir}/duplicate.swu" >"${tempDir}/duplicate.out" \
    2>"${tempDir}/duplicate.err"; then
    echo "错误：重复 requestId 未被拒绝" >&2
    exit 1
fi
grep -q '"phase":"failed"' "${tempDir}/duplicate.out"
test ! -e "${tempDir}/duplicate.swu"

rm -f "${tempDir}/state.json" "${tempDir}/upgrade.log" "${tempDir}/curl.log"
lowVersionCommandJson="$(printf \
    '{"requestId":"req-low","version":"3.2.1","url":"https://updates.example.test/package.swu","sha256":"%s","autoReboot":false}' \
    "${expectedSha256}")"
if PATH="${mockBin}:${PATH}" \
    OTA_AGENT_OS_RELEASE_FILE="${tempDir}/os-release" \
    MOCK_UPGRADE_AVAILABLE=0 \
    MOCK_ACTIVE_SLOT="A" \
    MOCK_FW_SETENV_LOG="${tempDir}/fw-setenv.log" \
    OTA_AGENT_LOCK_FILE="${tempDir}/ota-agent.lock" \
    OTA_AGENT_STATE_FILE="${tempDir}/state.json" \
    "${agentBinary}" --mqtt-command "${lowVersionCommandJson}" \
    "${tempDir}/low-version.swu" >"${tempDir}/low-version.out" \
    2>"${tempDir}/low-version.err"; then
    echo "错误：未升级版本命令未被拒绝" >&2
    exit 1
fi
grep -q '"phase":"failed"' "${tempDir}/low-version.out"
test ! -e "${tempDir}/low-version.swu"

echo "ota-agent guard and execution pipeline passed"
