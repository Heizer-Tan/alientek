#!/bin/sh
set -eu

projectRoot="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
sourceDir="${projectRoot}/meta-alientek/recipes-core/ota-agent/files/src"
initScript="${projectRoot}/meta-alientek/recipes-core/ota-agent/files/ota-agent.init"
tempDir="$(mktemp -d)"
pidFile="${tempDir}/ota-agent.pid"
agentBinary="${sourceDir}/ota-agent"
unrelatedPid=""

cleanup() {
    if [ -n "${unrelatedPid}" ]; then
        kill "${unrelatedPid}" 2>/dev/null || true
        wait "${unrelatedPid}" 2>/dev/null || true
    fi
    if [ -f "${pidFile}" ]; then
        pid="$(cat "${pidFile}")"
        kill "${pid}" 2>/dev/null || true
    fi
    make -C "${sourceDir}" clean >/dev/null
    rm -rf "${tempDir}"
}
trap cleanup EXIT INT TERM

make -C "${sourceDir}" clean >/dev/null
make -C "${sourceDir}" \
    CXXFLAGS='-O2 -Wall -Wextra -Werror -std=c++17' >/dev/null

runService() {
    OTA_AGENT_BINARY="${agentBinary}" \
        OTA_AGENT_PID_FILE="${pidFile}" \
        OTA_AGENT_LOCK_FILE="${tempDir}/ota-agent.lock" \
        OTA_AGENT_CONFIG_FILE="${tempDir}/missing-default" \
        sh "${initScript}" "$1"
}

runService start
sleep 1
runService status

agentPid="$(cat "${pidFile}")"
if ! kill -0 "${agentPid}" 2>/dev/null; then
    echo "错误：服务启动后未保持运行" >&2
    exit 1
fi

runService stop
if kill -0 "${agentPid}" 2>/dev/null; then
    echo "错误：服务停止后进程仍在运行" >&2
    exit 1
fi
if [ -e "${pidFile}" ]; then
    echo "错误：服务停止后 PID 文件仍然存在" >&2
    exit 1
fi

sleep 30 &
unrelatedPid="$!"
printf '%s\n' "${unrelatedPid}" >"${pidFile}"
runService stop

if ! kill -0 "${unrelatedPid}" 2>/dev/null; then
    echo "错误：陈旧 PID 文件导致无关进程被终止" >&2
    exit 1
fi
if [ -e "${pidFile}" ]; then
    echo "错误：陈旧 PID 文件未清理" >&2
    exit 1
fi

printf '%s\n' "${unrelatedPid}" >"${pidFile}"
runService restart
sleep 1
runService status
restartedPid="$(cat "${pidFile}")"
if ! kill -0 "${unrelatedPid}" 2>/dev/null; then
    echo "错误：重启时陈旧 PID 文件导致无关进程被终止" >&2
    exit 1
fi
if [ "${restartedPid}" = "${unrelatedPid}" ] ||
    ! kill -0 "${restartedPid}" 2>/dev/null; then
    echo "错误：清理陈旧 PID 文件后服务未能重启" >&2
    exit 1
fi
runService stop

echo "ota-agent service lifecycle passed"
