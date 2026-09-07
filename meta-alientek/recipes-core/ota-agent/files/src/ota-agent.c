#include "ota-agent.h"
#include "ota-download.h"
#include "ota-exec.h"
#include "ota-mqtt.h"
#include "ota-state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *readPathSetting(const char *name, const char *defaultPath)
{
    const char *path = getenv(name);

    if (path == NULL || path[0] == '\0') {
        return defaultPath;
    }
    return path;
}

static int loadStateForApply(const char *statePath, OtaState *state,
                             char *errorBuf, size_t errorBufSize)
{
    memset(state, 0, sizeof(*state));
    if (otaStateLoad(statePath, state) == 0 || errno == ENOENT) {
        return 0;
    }
    (void)snprintf(errorBuf, errorBufSize, "读取 OTA 状态失败: %s",
                   strerror(errno));
    return -1;
}

static int savePhase(const char *statePath, OtaState *state,
                     const char *phase, char *errorBuf, size_t errorBufSize)
{
    (void)snprintf(state->phase, sizeof(state->phase), "%s", phase);
    if (otaStateSave(statePath, state) == 0) {
        return 0;
    }
    (void)snprintf(errorBuf, errorBufSize, "保存 OTA 状态失败: %s",
                   strerror(errno));
    return -1;
}

static int executeApplyPipeline(const char *url, const char *sha256,
                                const char *swuPath, int autoReboot,
                                const char *statePath, OtaState *state,
                                char *errorBuf, size_t errorBufSize)
{
    if (otaCheckUpgradeAllowed(errorBuf, errorBufSize) != 0 ||
        savePhase(statePath, state, "downloading",
                  errorBuf, errorBufSize) != 0) {
        return -1;
    }
    if (otaDownloadPackage(url, swuPath, errorBuf, errorBufSize) != 0 ||
        savePhase(statePath, state, "verifying",
                  errorBuf, errorBufSize) != 0) {
        return -1;
    }
    if (otaVerifySha256(swuPath, sha256, errorBuf, errorBufSize) != 0 ||
        savePhase(statePath, state, "upgrading",
                  errorBuf, errorBufSize) != 0) {
        return -1;
    }
    return otaRunUpgrade(swuPath, autoReboot, errorBuf, errorBufSize);
}

static int otaAgentRunApply(int argc, char **argv)
{
    const char *lockPath = readPathSetting(
        "OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
    const char *statePath = readPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");
    char errorBuf[256];
    OtaState state;
    int lockFd;
    int result;

    if ((argc != 5 && argc != 6) ||
        (argc == 6 && strcmp(argv[5], "--reboot") != 0)) {
        fprintf(stderr, "用法: %s --apply <url> <sha256> <swu路径> [--reboot]\n",
                argv[0]);
        return 2;
    }
    lockFd = otaStateAcquireLock(lockPath);
    if (lockFd < 0) {
        fprintf(stderr, "ota-agent busy\n");
        return 1;
    }
    result = loadStateForApply(statePath, &state, errorBuf, sizeof(errorBuf));
    if (result == 0) {
        state.autoReboot = argc == 6;
        result = executeApplyPipeline(argv[2], argv[3], argv[4], argc == 6,
                                      statePath, &state, errorBuf,
                                      sizeof(errorBuf));
    }
    otaStateReleaseLock(lockFd, lockPath);
    if (result != 0) {
        fprintf(stderr, "ota-agent apply failed: %s\n", errorBuf);
        return 1;
    }
    return 0;
}

static void emitMqttStatus(OtaState *state, const char *phase,
                           const char *result, const char *detail)
{
    const char *topic = readPathSetting(
        "OTA_MQTT_STATUS_TOPIC", "device/ota/status");
    char payload[512];

    (void)snprintf(state->phase, sizeof(state->phase), "%s", phase);
    (void)snprintf(state->result, sizeof(state->result), "%s", result);
    (void)snprintf(state->detail, sizeof(state->detail), "%s", detail);
    if (otaMqttBuildStatusPayload(state, payload, sizeof(payload)) != 0) {
        fprintf(stderr, "生成 MQTT 状态失败: %s\n", strerror(errno));
        return;
    }
    puts(payload);
    if (otaMqttPublishStatus(topic, payload) != 0 && errno != ENOSYS) {
        fprintf(stderr, "发布 MQTT 状态失败: %s\n", strerror(errno));
    }
}

static int otaAgentRunMqttCommand(int argc, char **argv)
{
    const char *lockPath = readPathSetting(
        "OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
    const char *statePath = readPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");
    OtaMqttCommand command;
    OtaState state;
    char errorBuf[256];
    int lockFd;
    int result;

    if (argc != 4 || otaMqttParseCommandJson(argv[2], &state, &command) != 0) {
        fprintf(stderr, "MQTT OTA 命令 JSON 无效\n");
        return 2;
    }
    lockFd = otaStateAcquireLock(lockPath);
    if (lockFd < 0) {
        fprintf(stderr, "ota-agent busy\n");
        return 1;
    }
    emitMqttStatus(&state, "accepted", "running", "命令已解析");
    result = executeApplyPipeline(command.url, command.sha256, argv[3],
                                  state.autoReboot, statePath, &state,
                                  errorBuf, sizeof(errorBuf));
    emitMqttStatus(&state, result == 0 ? "completed" : "failed",
                   result == 0 ? "success" : "error",
                   result == 0 ? "升级命令已执行" : errorBuf);
    otaStateReleaseLock(lockFd, lockPath);
    return result == 0 ? 0 : 1;
}

static void initializeMqtt(void)
{
    const char *host = readPathSetting("OTA_MQTT_HOST", "127.0.0.1");
    const char *clientId = readPathSetting("OTA_MQTT_CLIENT_ID", "ota-agent");
    const char *topic = readPathSetting(
        "OTA_MQTT_COMMAND_TOPIC", "device/ota/command");
    const char *portText = readPathSetting("OTA_MQTT_PORT", "1883");
    char *end = NULL;
    long port = strtol(portText, &end, 10);

    if (end == portText || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "MQTT 端口配置无效: %s\n", portText);
        return;
    }
    if (otaMqttConnect(host, (int)port, clientId) != 0) {
        fprintf(stderr, "MQTT 接缝尚不可用: %s\n", strerror(errno));
        return;
    }
    if (otaMqttSubscribeCommand(topic) != 0) {
        fprintf(stderr, "订阅 MQTT 命令失败: %s\n", strerror(errno));
    }
}

int otaAgentRunForeground(void)
{
    const char *lockPath = readPathSetting(
        "OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
    int lockFd = otaStateAcquireLock(lockPath);

    if (lockFd < 0) {
        fprintf(stderr, "ota-agent busy\n");
        return 1;
    }
    puts("ota-agent foreground mode");
    initializeMqtt();

    /* 当前保持单实例常驻，真实 broker 事件循环由后续任务接入。 */
    for (;;) {
        (void)pause();
    }

    otaStateReleaseLock(lockFd, lockPath);
    return 0;
}

int otaAgentRunDaemon(void)
{
    return otaAgentRunForeground();
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "--apply") == 0) {
            return otaAgentRunApply(argc, argv);
        }
        if (strcmp(argv[1], "--mqtt-command") == 0) {
            return otaAgentRunMqttCommand(argc, argv);
        }
        fprintf(stderr, "未知参数: %s\n", argv[1]);
        return 2;
    }
    return otaAgentRunDaemon();
}
