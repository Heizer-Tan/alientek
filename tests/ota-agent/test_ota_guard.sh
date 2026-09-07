#!/bin/sh
set -eu

projectRoot="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
sourceDir="${projectRoot}/meta-alientek/recipes-core/ota-agent/files/src"
tempDir="$(mktemp -d)"
mockBin="${tempDir}/bin"
agentBinary="${sourceDir}/ota-agent"

cleanup() {
    make -C "${sourceDir}" clean >/dev/null 2>&1 || true
    rm -rf "${tempDir}"
}
trap cleanup EXIT INT TERM

for sourceFile in ota-download.c ota-download.h ota-exec.c ota-exec.h; do
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
chmod +x "${mockBin}/curl" "${mockBin}/fw_printenv" \
    "${mockBin}/board-apply-update"

make -C "${sourceDir}" clean >/dev/null
make -C "${sourceDir}" CFLAGS='-O2 -Wall -Wextra -Werror' >/dev/null

runAgent() {
    MOCK_PACKAGE="${tempDir}/package.swu" \
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
#include "ota-exec.h"
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
gcc -O2 -Wall -Wextra -Werror -I"${sourceDir}" \
    "${tempDir}/version-test.c" "${sourceDir}/ota-exec.c" \
    "${sourceDir}/ota-state.c" \
    -o "${tempDir}/version-test"
OTA_AGENT_OS_RELEASE_FILE="${tempDir}/os-release" \
    "${tempDir}/version-test" >"${tempDir}/version.out"
grep -qx '3.2.1' "${tempDir}/version.out"

echo "ota-agent guard and execution pipeline passed"
