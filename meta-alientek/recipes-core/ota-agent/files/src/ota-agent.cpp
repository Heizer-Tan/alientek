#include "ota-agent.hpp"

#include "ota-download.hpp"
#include "ota-exec.hpp"
#include "ota-mqtt.hpp"
#include "ota-state.hpp"

#include <ctype.h>
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

static char *trimInPlace(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }
    return text;
}

static int applyDefaultAssignment(char *line)
{
    char *equals;
    char *key;
    char *value;
    const char *existing;

    line = trimInPlace(line);
    if (line[0] == '\0' || line[0] == '#') {
        return 0;
    }
    if (strncmp(line, "export ", 7) == 0) {
        line = trimInPlace(line + 7);
    }
    equals = strchr(line, '=');
    if (equals == NULL || equals == line) {
        return 0;
    }
    *equals = '\0';
    key = trimInPlace(line);
    value = trimInPlace(equals + 1);
    if (value[0] == '"' || value[0] == '\'') {
        char quote = value[0];
        size_t length = strlen(value);

        if (length >= 2U && value[length - 1U] == quote) {
            value[length - 1U] = '\0';
            ++value;
        }
    }
    if (key[0] == '\0') {
        return 0;
    }
    existing = getenv(key);
    if (existing != NULL && existing[0] != '\0') {
        return 0;
    }
    return setenv(key, value, 0) == 0 ? 0 : -1;
}

/* 直接运行 ota-agent 时也加载 /etc/default/ota-agent，避免每次手动 export。 */
static void loadAgentDefaultFile(void)
{
    const char *configPath = readPathSetting(
        "OTA_AGENT_CONFIG_FILE", "/etc/default/ota-agent");
    char line[512];
    FILE *file = fopen(configPath, "r");

    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        (void)applyDefaultAssignment(line);
    }
    (void)fclose(file);
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
    /* 状态交给 mqtt-agent 从 stdout 转发；此处只打印。 */
    (void)topic;
    puts(payload);
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

static int executeMqttCommandJson(const char *jsonText,
                                  const char *downloadPath)
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
    if (jsonText == NULL || downloadPath == NULL ||
        otaMqttParseCommandJson(jsonText, &state, &command) != 0) {
        (void)snprintf(state.detail, sizeof(state.detail), "%s",
                       "MQTT OTA 命令 JSON 无效");
        return rejectMqttCommand(&state, state.detail, 2);
    }
    if (loadStateForApply(statePath, &previousState, errorBuf,
                          sizeof(errorBuf)) != 0) {
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
    if (savePhase(statePath, &state, "received", errorBuf, sizeof(errorBuf)) !=
        0) {
        otaStateReleaseLock(lockFd, lockPath);
        return rejectMqttCommand(&state, errorBuf, 1);
    }
    emitMqttStatus(&state, "accepted", "running", "命令已解析");
    result = executeApplyPipeline(command.url, command.sha256, downloadPath,
                                  state.autoReboot, statePath, &state,
                                  errorBuf, sizeof(errorBuf));
    emitMqttStatus(&state, result == 0 ? "completed" : "failed",
                   result == 0 ? "success" : "error",
                   result == 0 ? "升级命令已执行" : errorBuf);
    otaStateReleaseLock(lockFd, lockPath);
    return result == 0 ? 0 : 1;
}

static int otaAgentRunMqttCommand(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
                "用法: %s --mqtt-command '<json>' <swu落盘路径>\n",
                argv[0]);
        return 2;
    }
    return executeMqttCommandJson(argv[2], argv[3]);
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

static int isPendingRecovery(const OtaState *state)
{
    return strcmp(state->phase, "upgrading") == 0 ||
           strcmp(state->phase, "installing") == 0;
}

static int isFinalRecoveryPhase(const char *phase)
{
    return strcmp(phase, "committed") == 0 || strcmp(phase, "failed") == 0;
}

/*
 * 旧槽残留 phase=upgrading，而 U-Boot 环境已是最终态时：以环境为准回写文件。
 * 否则会盖过 committed，导致恢复死循环卡启动（A 写状态、B 提交后回 A 即复现）。
 */
static int clearStalePendingFromEnvironment(const char *statePath,
                                            OtaState *fileState)
{
    OtaState envState;

    if (!isPendingRecovery(fileState)) {
        return 0;
    }
    memset(&envState, 0, sizeof(envState));
    if (otaStateLoadFromEnvironment(&envState) != 0) {
        return 0;
    }
    if (!isFinalRecoveryPhase(envState.phase)) {
        return 0;
    }
    fprintf(stderr,
            "检测到残留 pending 状态，环境已是 %s，回写并跳过恢复\n",
            envState.phase);
    *fileState = envState;
    if (otaStateSave(statePath, fileState) != 0) {
        fprintf(stderr, "回写 OTA 状态失败: %s\n", strerror(errno));
        return -1;
    }
    return 1;
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

/* 返回值：0 完成/跳过；1 可重试。 */
static int recoverPendingAtStartup(const char *statePath)
{
    OtaState state;
    int loadResult;
    int savedErrno;
    int staleResult;

    loadResult = loadStateForRecovery(statePath, &state);
    if (loadResult < 0) {
        fprintf(stderr, "加载待恢复 OTA 状态失败: %s\n", strerror(errno));
        return 1;
    }
    if (loadResult == 1) {
        return 0;
    }
    if (loadResult == 2) {
        staleResult = clearStalePendingFromEnvironment(statePath, &state);
        if (staleResult < 0) {
            return 1;
        }
        if (staleResult > 0) {
            return 0;
        }
    }
    if (!isPendingRecovery(&state)) {
        return 0;
    }
    if (otaRecoverPendingState(&state) != 0) {
        savedErrno = errno;
        /* NFS/无 mmc 根时无法判定槽位，跳过以免卡死 SysV 启动。 */
        if (savedErrno == ENODEV) {
            fprintf(stderr,
                    "当前非 A/B 根分区启动，跳过 OTA 恢复（待 mmc 启动再判定）\n");
            return 0;
        }
        if (savedErrno != EAGAIN) {
            fprintf(stderr, "判定 OTA 恢复结果失败: %s\n",
                    strerror(savedErrno));
        }
        return 1;
    }
    return reportAndSaveRecovery(statePath, &state) == 0 ? 0 : 1;
}

static void completeStartupRecovery(const char *statePath)
{
    /* 限次重试：提交竞态或短暂 I/O 失败可恢复；禁止无限循环堵开机。 */
    const int maxAttempts = 30;
    int attempt = 0;
    int recoveryResult = recoverPendingAtStartup(statePath);

    while (recoveryResult == 1 && attempt < maxAttempts) {
        sleep(1);
        ++attempt;
        recoveryResult = recoverPendingAtStartup(statePath);
    }
    if (recoveryResult == 1) {
        fprintf(stderr, "OTA 恢复仍未完成，稍后再试（不阻塞启动）\n");
    }
}


int otaAgentRunRecovery(void)
{
    const char *statePath = readPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");

    puts("ota-agent recovery mode");
    completeStartupRecovery(statePath);
    return 0;
}

int main(int argc, char **argv)
{
    loadAgentDefaultFile();
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
    return otaAgentRunRecovery();
}
