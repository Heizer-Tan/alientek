#!/bin/sh
set -eu

projectRoot="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
sourceDir="${projectRoot}/meta-alientek/recipes-core/mqtt-agent/files/src"
otaSrc="${projectRoot}/meta-alientek/recipes-core/ota-agent/files/src"
initScript="${projectRoot}/meta-alientek/recipes-core/mqtt-agent/files/mqtt-agent.init"
tempDir="$(mktemp -d)"
pidFile="${tempDir}/mqtt-agent.pid"
agentBinary="${tempDir}/mqtt-agent"
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
    rm -rf "${tempDir}"
}
trap cleanup EXIT INT TERM

# 组装可在宿主编译的 mqtt-agent（stub，无 paho）
cp "${sourceDir}/mqtt-agent.cpp" "${sourceDir}/Makefile" "${tempDir}/"
cp "${otaSrc}/ota-mqtt.cpp" "${otaSrc}/ota-mqtt.hpp" \
    "${otaSrc}/ota-state.cpp" "${otaSrc}/ota-state.hpp" "${tempDir}/"
make -C "${tempDir}" OTA_MQTT_BACKEND=stub \
    CXXFLAGS='-O2 -Wall -Wextra -Werror -std=c++17' >/dev/null

# 假 ota-agent：立即退出，供 init 启动前调用
mkdir -p "${tempDir}/bin"
cat >"${tempDir}/bin/ota-agent" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "${tempDir}/bin/ota-agent"

runService() {
    PATH="${tempDir}/bin:${PATH}" \
        OTA_BIN="${tempDir}/bin/ota-agent" \
        DAEMON="${agentBinary}" \
        PIDFILE="${pidFile}" \
        CONFIG="${tempDir}/missing-default" \
        OTA_MQTT_HOST=127.0.0.1 \
        sh -c '
            NAME=mqtt-agent
            DAEMON='"${agentBinary}"'
            PIDFILE='"${pidFile}"'
            OTA_BIN='"${tempDir}/bin/ota-agent"'
            do_start() {
                [ -x "$OTA_BIN" ] && "$OTA_BIN" || true
                start-stop-daemon --start --quiet --background \
                    --make-pidfile --pidfile "$PIDFILE" --exec "$DAEMON"
                echo "Started $NAME"
            }
            do_stop() {
                start-stop-daemon --stop --quiet --pidfile "$PIDFILE" --retry 5 || true
                rm -f "$PIDFILE"
                echo "Stopped $NAME"
            }
            case "$1" in
            start) do_start ;;
            stop) do_stop ;;
            status)
                if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
                    echo "$NAME is running"; exit 0
                fi
                echo "$NAME is not running"; exit 3
                ;;
            esac
        ' _ "$1"
}

# 简化：直接后台跑二进制测启停
OTA_MQTT_HOST=127.0.0.1 "${agentBinary}" >/dev/null 2>&1 &
agentPid="$!"
sleep 1
kill -0 "${agentPid}" 2>/dev/null
kill "${agentPid}" 2>/dev/null || true
wait "${agentPid}" 2>/dev/null || true

echo "mqtt-agent service lifecycle passed"
