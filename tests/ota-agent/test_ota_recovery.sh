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

mkdir -p "${mockBin}"
cat >"${mockBin}/fw_printenv" <<'EOF'
#!/bin/sh
set -eu
test "$1" = "-n"
case "$2" in
    active_slot) printf '%s\n' "${MOCK_ACTIVE_SLOT}" ;;
    last_good_slot) printf '%s\n' "${MOCK_LAST_GOOD_SLOT}" ;;
    upgrade_available) printf '%s\n' "${MOCK_UPGRADE_AVAILABLE}" ;;
    *) exit 2 ;;
esac
EOF
chmod +x "${mockBin}/fw_printenv"
cat >"${mockBin}/curl" <<'EOF'
#!/bin/sh
set -eu
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) outputPath="$2"; shift 2 ;;
        --) shift; break ;;
        *) shift ;;
    esac
done
cp "${MOCK_PACKAGE}" "${outputPath}"
EOF
cat >"${mockBin}/board-apply-update" <<'EOF'
#!/bin/sh
set -eu
test -f "${1}"
EOF
chmod +x "${mockBin}/curl" "${mockBin}/board-apply-update"

cat >"${tempDir}/test_ota_recovery.c" <<'EOF'
#include "ota-exec.h"
#include "ota-mqtt.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void setMockEnvironment(const char *activeSlot, const char *lastGoodSlot,
                               const char *upgradeAvailable)
{
    assert(setenv("MOCK_ACTIVE_SLOT", activeSlot, 1) == 0);
    assert(setenv("MOCK_LAST_GOOD_SLOT", lastGoodSlot, 1) == 0);
    assert(setenv("MOCK_UPGRADE_AVAILABLE", upgradeAvailable, 1) == 0);
}

static void writeCmdline(const char *path, const char *rootDevice)
{
    FILE *file = fopen(path, "w");

    assert(file != NULL);
    assert(fprintf(file, "console=ttymxc0 root=%s rootwait\n", rootDevice) > 0);
    assert(fclose(file) == 0);
}

static OtaState createPendingState(void)
{
    OtaState state = {0};

    strcpy(state.requestId, "req-001");
    strcpy(state.version, "5.0.20");
    strcpy(state.targetSlot, "B");
    strcpy(state.phase, "upgrading");
    state.autoReboot = 1;
    return state;
}

static void testCommittedRecovery(const char *cmdlinePath)
{
    OtaState state = createPendingState();

    writeCmdline(cmdlinePath, "/dev/mmcblk0p3");
    setMockEnvironment("B", "B", "0");
    assert(otaRecoverPendingState(&state) == 0);
    assert(strcmp(state.phase, "committed") == 0);
    assert(strcmp(state.result, "success") == 0);
    assert(otaReportCommittedState(&state) == 0);
}

static void testRollbackRecovery(const char *cmdlinePath)
{
    OtaState state = createPendingState();

    writeCmdline(cmdlinePath, "/dev/mmcblk0p2");
    setMockEnvironment("A", "A", "0");
    assert(otaRecoverPendingState(&state) == 0);
    assert(strcmp(state.phase, "failed") == 0);
    assert(strcmp(state.result, "error") == 0);
    assert(otaReportFailureState(&state, "检测到启动槽位回滚") == 0);
}

static void testPendingCommitIsDeferred(const char *cmdlinePath)
{
    OtaState state = createPendingState();

    writeCmdline(cmdlinePath, "/dev/mmcblk0p3");
    setMockEnvironment("B", "A", "1");
    errno = 0;
    assert(otaRecoverPendingState(&state) == -1);
    assert(errno == EAGAIN);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(setenv("BOARD_CMDLINE_FILE", argv[1], 1) == 0);
    testCommittedRecovery(argv[1]);
    testRollbackRecovery(argv[1]);
    testPendingCommitIsDeferred(argv[1]);
    puts("ota recovery tests passed");
    return 0;
}
EOF

gcc -O2 -Wall -Wextra -Werror -I"${sourceDir}" \
    "${tempDir}/test_ota_recovery.c" \
    "${sourceDir}/ota-exec.c" \
    "${sourceDir}/ota-mqtt.c" \
    "${sourceDir}/ota-state.c" \
    -o "${tempDir}/test_ota_recovery"

PATH="${mockBin}:${PATH}" \
    OTA_MQTT_STATUS_TOPIC="device/test/ota/status" \
    "${tempDir}/test_ota_recovery" "${tempDir}/cmdline" \
    >"${tempDir}/recovery.out"

grep -q '"phase":"committed"' "${tempDir}/recovery.out"
grep -q '"result":"success"' "${tempDir}/recovery.out"
grep -q '"phase":"failed"' "${tempDir}/recovery.out"
grep -q '"result":"error"' "${tempDir}/recovery.out"
grep -q '"detail":"检测到启动槽位回滚"' "${tempDir}/recovery.out"
grep -q 'ota recovery tests passed' "${tempDir}/recovery.out"

cat >"${tempDir}/state.json" <<'EOF'
{
  "requestId": "req-startup",
  "version": "5.0.20",
  "targetSlot": "B",
  "phase": "upgrading",
  "result": "",
  "detail": "",
  "autoReboot": 1
}
EOF
printf '%s\n' 'console=ttymxc0 root=/dev/mmcblk0p3 rootwait' \
    >"${tempDir}/cmdline"
make -C "${sourceDir}" clean >/dev/null
make -C "${sourceDir}" CFLAGS='-O2 -Wall -Wextra -Werror' >/dev/null
printf '%s\n' 'test swu payload' >"${tempDir}/package.swu"
printf '%s\n' 'console=ttymxc0 root=/dev/mmcblk0p2 rootwait' \
    >"${tempDir}/cmdline-a"
packageSha256="$(sha256sum "${tempDir}/package.swu" | awk '{print $1}')"
commandJson="$(printf \
    '{"requestId":"req-target","version":"5.0.20","url":"https://updates.example.test/package.swu","sha256":"%s","autoReboot":false}' \
    "${packageSha256}")"
PATH="${mockBin}:${PATH}" \
    MOCK_PACKAGE="${tempDir}/package.swu" \
    MOCK_ACTIVE_SLOT="A" \
    MOCK_LAST_GOOD_SLOT="A" \
    MOCK_UPGRADE_AVAILABLE="0" \
    BOARD_CMDLINE_FILE="${tempDir}/cmdline-a" \
    OTA_AGENT_STATE_FILE="${tempDir}/target-state.json" \
    OTA_AGENT_LOCK_FILE="${tempDir}/target.lock" \
    "${agentBinary}" --mqtt-command "${commandJson}" "${tempDir}/download.swu" \
    >"${tempDir}/command.out"
grep -Eq '"targetSlot":[[:space:]]*"B"' "${tempDir}/target-state.json"

set +e
PATH="${mockBin}:${PATH}" \
    MOCK_ACTIVE_SLOT="B" \
    MOCK_LAST_GOOD_SLOT="B" \
    MOCK_UPGRADE_AVAILABLE="0" \
    BOARD_CMDLINE_FILE="${tempDir}/cmdline" \
    OTA_AGENT_STATE_FILE="${tempDir}/state.json" \
    OTA_AGENT_LOCK_FILE="${tempDir}/ota-agent.lock" \
    timeout 1 stdbuf -o0 -e0 "${agentBinary}" \
    >"${tempDir}/startup.out" 2>"${tempDir}/startup.err"
startupStatus="$?"
set -e
test "${startupStatus}" -eq 124
grep -q '"requestId":"req-startup"' "${tempDir}/startup.out"
grep -q '"phase":"committed"' "${tempDir}/startup.out"
grep -Eq '"phase":[[:space:]]*"committed"' "${tempDir}/state.json"

echo "ota-agent recovery and final status passed"
