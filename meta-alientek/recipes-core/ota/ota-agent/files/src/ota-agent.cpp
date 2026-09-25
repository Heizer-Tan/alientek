#include "ota-agent.hpp"

#include "ota-defaults.hpp"
#include "ota-discover.hpp"
#include "ota-download.hpp"
#include "ota-exec.hpp"
#include "ota-mqtt.hpp"
#include "ota-state.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 直接运行 ota-agent 时也加载 /etc/default/ota-agent，避免每次手动 export。 */
static void loadAgentDefaultFile(void)
{
    otaLoadDefaultFile(otaReadPathSetting(
        "OTA_AGENT_CONFIG_FILE", "/etc/default/ota-agent"));
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

/* 实验室拉取：无远端 sha256，下载后直接刷写；stdout 输出 OTA_PROGRESS 供 UI */
static int g_pullProgressHighWater;

static void emitOtaProgress(int percent, const char *stage)
{
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	/* 整次 pull-latest 过程单调不减，避免 UI 回跳 */
	if (percent < g_pullProgressHighWater)
		percent = g_pullProgressHighWater;
	g_pullProgressHighWater = percent;
	printf("OTA_PROGRESS %d %s\n", percent, stage != NULL ? stage : "");
	fflush(stdout);
}

static void onPullDownloadProgress(int curlPct, void *userData)
{
	int mapped;

	(void)userData;
	/* 下载映射到整体进度 10%..90% */
	mapped = 10 + (curlPct * 80) / 100;
	emitOtaProgress(mapped, "downloading");
}

static int executePullLatestPipeline(const char *url, const char *swuPath,
				     const char *statePath, OtaState *state,
				     char *errorBuf, size_t errorBufSize)
{
	if (otaCheckUpgradeAllowed(errorBuf, errorBufSize) != 0 ||
	    otaPrepareTargetSlot(state, errorBuf, errorBufSize) != 0 ||
	    savePhase(statePath, state, "downloading", errorBuf,
		      errorBufSize) != 0) {
		return -1;
	}
	emitOtaProgress(10, "downloading");
	if (otaDownloadPackageWithProgress(url, swuPath, onPullDownloadProgress,
					   NULL, errorBuf, errorBufSize) != 0 ||
	    savePhase(statePath, state, "upgrading", errorBuf,
		      errorBufSize) != 0) {
		return -1;
	}
	emitOtaProgress(95, "upgrading");
	if (otaRunUpgrade(swuPath, 1, errorBuf, errorBufSize) != 0)
		return -1;
	emitOtaProgress(100, "done");
	return 0;
}

static int otaAgentRunPullLatest(int argc, char **argv)
{
	const char *lockPath = otaReadPathSetting(
		"OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
	const char *statePath = otaReadPathSetting(
		"OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");
	const char *swuPath = otaReadPathSetting(
		"OTA_DOWNLOAD_PATH", "/var/tmp/ota-download.swu");
	const char *baseUrl;
	char url[512];
	char name[256];
	char errorBuf[256];
	OtaState state;
	int lockFd;
	int result;

	if (argc != 2 && argc != 3) {
		fprintf(stderr,
			"用法: %s --pull-latest [固件目录URL]\n"
			"默认目录: OTA_FIRMWARE_BASE 或 http://192.168.5.13:8000\n",
			argv[0]);
		return 2;
	}
	baseUrl = (argc == 3) ? argv[2]
			      : otaReadPathSetting("OTA_FIRMWARE_BASE",
						   "http://192.168.5.13:8000");
	g_pullProgressHighWater = 0;
	emitOtaProgress(3, "resolving");
	if (otaResolveLatestSwuUrl(baseUrl, url, sizeof(url), name, sizeof(name),
				   errorBuf, sizeof(errorBuf)) != 0) {
		fprintf(stderr, "ota-agent pull-latest: %s\n", errorBuf);
		return 1;
	}
	fprintf(stderr, "选中固件: %s\nURL: %s\n", name, url);
	emitOtaProgress(8, "resolved");
	if (getenv("OTA_PULL_DRY_RUN") != NULL) {
		printf("%s\n", url);
		return 0;
	}
	lockFd = otaStateAcquireLock(lockPath);
	if (lockFd < 0) {
		fprintf(stderr, "ota-agent busy\n");
		return 1;
	}
	result = loadStateForApply(statePath, &state, errorBuf, sizeof(errorBuf));
	if (result == 0) {
		(void)snprintf(state.requestId, sizeof(state.requestId),
			       "pull-latest");
		(void)snprintf(state.detail, sizeof(state.detail), "%s", name);
		state.autoReboot = 1;
		result = executePullLatestPipeline(url, swuPath, statePath,
						   &state, errorBuf,
						   sizeof(errorBuf));
	}
	otaStateReleaseLock(lockFd, lockPath);
	if (result != 0) {
		fprintf(stderr, "ota-agent pull-latest failed: %s\n", errorBuf);
		return 1;
	}
	return 0;
}

static int otaAgentRunApply(int argc, char **argv)
{
    const char *lockPath = otaReadPathSetting(
        "OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
    const char *statePath = otaReadPathSetting(
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
    const char *topic = otaReadPathSetting(
        "OTA_MQTT_STATUS_TOPIC", "device/ota/status");
    char payload[512];

    (void)snprintf(state->phase, sizeof(state->phase), "%s", phase);
    (void)snprintf(state->result, sizeof(state->result), "%s", result);
    (void)snprintf(state->detail, sizeof(state->detail), "%s", detail);
    if (otaMqttBuildStatusPayload(state, payload, sizeof(payload)) != 0) {
        fprintf(stderr, "生成 MQTT 状态失败: %s\n", strerror(errno));
        return;
    }
    /* 状态交给 mqtt-agent 从 stdout 转发；stderr 同步打一份便于串口确认时机。 */
    (void)topic;
    puts(payload);
    fflush(stdout);
    fprintf(stderr, "ota-agent status: %s\n", payload);
    fflush(stderr);
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

/* 只做解析/校验并立刻退出：与「重复 requestId」相同，靠进程结束把 status 刷出管道。 */
static int executeMqttPrecheck(const char *jsonText)
{
    const char *statePath = otaReadPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");
    OtaMqttCommand command;
    OtaState state;
    OtaState previousState;
    char errorBuf[256];
    char currentVersion[32];

    memset(&state, 0, sizeof(state));
    if (jsonText == NULL ||
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
    emitMqttStatus(&state, "accepted", "running", "命令已解析");
    return 0;
}

static int executeMqttCommandJson(const char *jsonText,
                                  const char *downloadPath)
{
    const char *lockPath = otaReadPathSetting(
        "OTA_AGENT_LOCK_FILE", "/var/run/ota-agent.lock");
    const char *statePath = otaReadPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");
    const char *skipAccepted = getenv("OTA_MQTT_SKIP_ACCEPTED");
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
    /* mqtt-agent 已用 --mqtt-precheck 发过 accepted 时跳过，避免重复。 */
    if (skipAccepted == NULL || skipAccepted[0] == '\0') {
        emitMqttStatus(&state, "accepted", "running", "命令已解析");
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
    result = executeApplyPipeline(command.url, command.sha256, downloadPath,
                                  state.autoReboot, statePath, &state,
                                  errorBuf, sizeof(errorBuf));
    emitMqttStatus(&state, result == 0 ? "completed" : "failed",
                   result == 0 ? "success" : "error",
                   result == 0 ? "升级命令已执行" : errorBuf);
    otaStateReleaseLock(lockFd, lockPath);
    return result == 0 ? 0 : 1;
}

static int otaAgentRunMqttPrecheck(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "用法: %s --mqtt-precheck '<json>'\n", argv[0]);
        return 2;
    }
    return executeMqttPrecheck(argv[2]);
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
    const char *statePath = otaReadPathSetting(
        "OTA_AGENT_STATE_FILE", "/var/lib/ota-agent/state.json");

    puts("ota-agent recovery mode");
    completeStartupRecovery(statePath);
    return 0;
}

int main(int argc, char **argv)
{
    /* 被 mqtt-agent 管道拉起时须无缓冲，否则 status 会攒到进程结束才发出。 */
    setvbuf(stdout, NULL, _IONBF, 0);
    loadAgentDefaultFile();
    if (argc > 1) {
        if (strcmp(argv[1], "--apply") == 0) {
            return otaAgentRunApply(argc, argv);
        }
        if (strcmp(argv[1], "--mqtt-precheck") == 0) {
            return otaAgentRunMqttPrecheck(argc, argv);
        }
        if (strcmp(argv[1], "--mqtt-command") == 0) {
            return otaAgentRunMqttCommand(argc, argv);
        }
        if (strcmp(argv[1], "--pull-latest") == 0) {
            return otaAgentRunPullLatest(argc, argv);
        }
        fprintf(stderr,
                "用法: %s [--apply … | --mqtt-precheck … | --mqtt-command … | --pull-latest [url]]\n",
                argv[0]);
        return 2;
    }
    return otaAgentRunRecovery();
}
