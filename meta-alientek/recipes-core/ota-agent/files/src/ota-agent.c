#include "ota-agent.h"
#include "ota-download.h"
#include "ota-exec.h"
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

static int executeApplyPipeline(char **argv, int autoReboot,
                                const char *statePath, OtaState *state,
                                char *errorBuf, size_t errorBufSize)
{
    if (otaCheckUpgradeAllowed(errorBuf, errorBufSize) != 0 ||
        savePhase(statePath, state, "downloading",
                  errorBuf, errorBufSize) != 0) {
        return -1;
    }
    if (otaDownloadPackage(argv[2], argv[4], errorBuf, errorBufSize) != 0 ||
        savePhase(statePath, state, "verifying",
                  errorBuf, errorBufSize) != 0) {
        return -1;
    }
    if (otaVerifySha256(argv[4], argv[3], errorBuf, errorBufSize) != 0 ||
        savePhase(statePath, state, "upgrading",
                  errorBuf, errorBufSize) != 0) {
        return -1;
    }
    return otaRunUpgrade(argv[4], autoReboot, errorBuf, errorBufSize);
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
        result = executeApplyPipeline(argv, argc == 6, statePath, &state,
                                      errorBuf, sizeof(errorBuf));
    }
    otaStateReleaseLock(lockFd, lockPath);
    if (result != 0) {
        fprintf(stderr, "ota-agent apply failed: %s\n", errorBuf);
        return 1;
    }
    return 0;
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

    /* 当前仅保持单实例常驻，后续任务再加入实际事件处理。 */
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
        fprintf(stderr, "未知参数: %s\n", argv[1]);
        return 2;
    }
    return otaAgentRunDaemon();
}
