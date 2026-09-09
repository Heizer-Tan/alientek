#include "ota-agent.hpp"

#include "ota-download.hpp"
#include "ota-exec.hpp"
#include "ota-mqtt.hpp"
#include "ota-state.hpp"

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
    if (otaStateLoad(statePath, state) == 0) {
        return 0;
    }
    if (errno == ENOENT && otaStateLoadFromEnvironment(state) == 0) {
        return 0;
    }
    if (errno == ENOENT) {
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
    if (otaStateSave(statePath, state) == 0 &&
        otaStateSaveToEnvironment(state) == 0) {
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
        otaPrepareTargetSlot(state, errorBuf, errorBufSize) != 0 ||
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

static int compareVersionToken(const char **versionText)
{
    const char *cursor = *versionText;
    int value = 0;

    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (*cursor - '0');
        ++cursor;
    }
    if (*cursor == '.') {
        ++cursor;
    }
    *versionText = cursor;
    return value;
}

static int compareVersions(const char *leftVersion, const char *rightVersion)
{
    const char *leftCursor = leftVersion;
    const char *rightCursor = rightVersion;

    while (*leftCursor != '\0' || *rightCursor != '\0') {
        int leftValue = compareVersionToken(&leftCursor);
        int rightValue = compareVersionToken(&rightCursor);

        if (leftValue != rightValue) {
            return leftValue < rightValue ? -1 : 1;
        }
    }
    return 0;
}

static int rejectMqttCommand(OtaState *state, const char *detail, int exitCode)
{
    emitMqttStatus(state, "failed", "error", detail);
    return exitCode;
}

static int otaAgentRunMqttCommand(int argc, char **argv)
{
    const char *lockPath = readPathSetting(
        "OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
    const char *statePath = readPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");
    OtaMqttCommand command;
    OtaState state;
    OtaState previousState;
    char errorBuf[256];
    char currentVersion[32];
    int lockFd;
    int result;

    memset(&state, 0, sizeof(state));
    if (argc != 4 || otaMqttParseCommandJson(argv[2], &state, &command) != 0) {
        (void)snprintf(state.detail, sizeof(state.detail), "%s",
                       "MQTT OTA 命令 JSON 无效");
        return rejectMqttCommand(&state, state.detail, 2);
    }
    if (loadStateForApply(statePath, &previousState, errorBuf, sizeof(errorBuf)) !=
        0) {
        return rejectMqttCommand(&state, errorBuf, 1);
    }
    if (otaStateRequestSeen(&previousState, state.requestId) != 0) {
        return rejectMqttCommand(&state, "重复 requestId，拒绝执行", 1);
    }
    if (otaReadCurrentVersion(currentVersion, sizeof(currentVersion)) == 0 &&
        compareVersions(state.version, currentVersion) <= 0) {
        return rejectMqttCommand(&state, "目标版本不高于当前版本，拒绝执行", 1);
    }
    lockFd = otaStateAcquireLock(lockPath);
    if (lockFd < 0) {
        return rejectMqttCommand(&state, "ota-agent busy", 1);
    }
    if (savePhase(statePath, &state, "received", errorBuf, sizeof(errorBuf)) != 0) {
        otaStateReleaseLock(lockFd, lockPath);
        return rejectMqttCommand(&state, errorBuf, 1);
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

static int loadStateForRecovery(const char *statePath, OtaState *state)
{
    memset(state, 0, sizeof(*state));
    if (otaStateLoad(statePath, state) == 0) {
        return 2;
    }
    if (errno == ENOENT && otaStateLoadFromEnvironment(state) == 0) {
        return 0;
    }
    return errno == ENOENT ? 1 : -1;
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

static int isPendingRecovery(const OtaState *state)
{
    return strcmp(state->phase, "upgrading") == 0 ||
           strcmp(state->phase, "installing") == 0;
}

static int reportAndSaveRecovery(const char *statePath, OtaState *state)
{
    int reportResult = strcmp(state->phase, "committed") == 0
                           ? otaReportCommittedState(state)
                           : otaReportFailureState(state, state->detail);

    if (reportResult != 0) {
        fprintf(stderr, "回报 OTA 最终状态失败: %s\n", strerror(errno));
        return 1;
    }
    if (otaStateSave(statePath, state) != 0 ||
        otaStateSaveToEnvironment(state) != 0) {
        fprintf(stderr, "保存 OTA 最终状态失败: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

static int recoverPendingAtStartup(const char *statePath)
{
    OtaState state;
    int loadResult;

    loadResult = loadStateForRecovery(statePath, &state);
    if (loadResult < 0) {
        fprintf(stderr, "加载待恢复 OTA 状态失败: %s\n", strerror(errno));
        return 1;
    }
    if (loadResult == 1) {
        return 0;
    }
    if (!isPendingRecovery(&state)) {
        return 0;
    }
    if (otaRecoverPendingState(&state) != 0) {
        if (errno != EAGAIN) {
            fprintf(stderr, "判定 OTA 恢复结果失败: %s\n", strerror(errno));
        }
        return 1;
    }
    return reportAndSaveRecovery(statePath, &state);
}

static void completeStartupRecovery(const char *statePath)
{
    int recoveryResult = recoverPendingAtStartup(statePath);

    /* 提交尚未完成或恢复 I/O 暂时失败时，持续重试避免永久搁置。 */
    while (recoveryResult == 1) {
        sleep(1);
        recoveryResult = recoverPendingAtStartup(statePath);
    }
}

int otaAgentRunForeground(void)
{
    const char *statePath = readPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");

    puts("ota-agent foreground mode");
    initializeMqtt();
    completeStartupRecovery(statePath);

    /* 常驻进程不再长期占用任务锁，避免阻塞 --apply 等执行入口。 */
    for (;;) {
        (void)pause();
    }
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
