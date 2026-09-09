#include "ota-exec.hpp"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void setError(char *errorBuf, size_t errorBufSize,
                     const char *format, ...)
{
    va_list args;

    if (errorBuf == NULL || errorBufSize == 0U) {
        return;
    }
    va_start(args, format);
    (void)vsnprintf(errorBuf, errorBufSize, format, args);
    va_end(args);
}

static int waitForCommand(pid_t pid, const char *name,
                          char *errorBuf, size_t errorBufSize)
{
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            setError(errorBuf, errorBufSize, "等待 %s 失败: %s",
                     name, strerror(errno));
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        setError(errorBuf, errorBufSize, "%s 执行失败，退出状态=%d",
                 name, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    return 0;
}

static int runCommand(char *const args[], const char *name,
                      char *errorBuf, size_t errorBufSize)
{
    pid_t pid = fork();

    if (pid == 0) {
        execvp(args[0], args);
        _exit(127);
    }
    if (pid < 0) {
        setError(errorBuf, errorBufSize, "启动 %s 失败: %s",
                 name, strerror(errno));
        return -1;
    }
    return waitForCommand(pid, name, errorBuf, errorBufSize);
}

static int readEnvironmentValue(const char *name, char *value,
                                size_t valueSize, char *errorBuf,
                                size_t errorBufSize)
{
    int outputPipe[2];
    pid_t pid;
    ssize_t count;

    if (name == NULL || value == NULL || valueSize < 2U) {
        errno = EINVAL;
        setError(errorBuf, errorBufSize, "读取 U-Boot 环境变量参数无效");
        return -1;
    }
    if (pipe(outputPipe) != 0) {
        setError(errorBuf, errorBufSize, "创建 fw_printenv 管道失败: %s",
                 strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid == 0) {
        (void)dup2(outputPipe[1], STDOUT_FILENO);
        close(outputPipe[0]);
        close(outputPipe[1]);
        execlp("fw_printenv", "fw_printenv", "-n", name, (char *)NULL);
        _exit(127);
    }
    close(outputPipe[1]);
    count = pid < 0 ? -1 : read(outputPipe[0], value, valueSize - 1U);
    close(outputPipe[0]);
    if (pid < 0 || count < 0) {
        setError(errorBuf, errorBufSize, "读取 %s 失败: %s", name,
                 strerror(errno));
        return -1;
    }
    value[count] = '\0';
    return waitForCommand(pid, "fw_printenv", errorBuf, errorBufSize);
}

static int readUpgradeAvailable(char *value, size_t valueSize,
                                char *errorBuf, size_t errorBufSize)
{
    return readEnvironmentValue("upgrade_available", value, valueSize,
                                errorBuf, errorBufSize);
}

int otaPrepareTargetSlot(OtaState *state, char *errorBuf, size_t errorBufSize)
{
    char activeSlot[8];
    size_t length;

    if (state == NULL) {
        errno = EINVAL;
        setError(errorBuf, errorBufSize, "OTA 状态不能为空");
        return -1;
    }
    if (readEnvironmentValue("active_slot", activeSlot, sizeof(activeSlot),
                             errorBuf, errorBufSize) != 0) {
        return -1;
    }
    length = strcspn(activeSlot, "\r\n");
    activeSlot[length] = '\0';
    if (strcmp(activeSlot, "A") != 0 && strcmp(activeSlot, "B") != 0) {
        errno = EINVAL;
        setError(errorBuf, errorBufSize, "active_slot 值无效: %s", activeSlot);
        return -1;
    }
    (void)snprintf(state->targetSlot, sizeof(state->targetSlot), "%s",
                   strcmp(activeSlot, "A") == 0 ? "B" : "A");
    return 0;
}

int otaCheckUpgradeAllowed(char *errorBuf, size_t errorBufSize)
{
    char value[16];
    size_t length;

    if (readUpgradeAvailable(value, sizeof(value),
                             errorBuf, errorBufSize) != 0) {
        return -1;
    }
    length = strcspn(value, "\r\n");
    value[length] = '\0';
    if (strcmp(value, "1") == 0) {
        setError(errorBuf, errorBufSize,
                 "upgrade_available=1，上次升级尚未提交，拒绝再次升级");
        errno = EBUSY;
        return -1;
    }
    if (strcmp(value, "0") != 0) {
        setError(errorBuf, errorBufSize,
                 "upgrade_available 值无效: %s", value);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int otaRunUpgrade(const char *swuPath, int autoReboot,
                  char *errorBuf, size_t errorBufSize)
{
    char *normalArgs[] = {(char *)"board-apply-update", (char *)swuPath, NULL};
    char *rebootArgs[] = {(char *)"board-apply-update", (char *)"--reboot",
                          (char *)swuPath, NULL};

    if (swuPath == NULL || swuPath[0] == '\0') {
        setError(errorBuf, errorBufSize, "升级包路径不能为空");
        errno = EINVAL;
        return -1;
    }
    if (otaCheckUpgradeAllowed(errorBuf, errorBufSize) != 0) {
        return -1;
    }
    return runCommand(autoReboot ? rebootArgs : normalArgs,
                      "board-apply-update", errorBuf, errorBufSize);
}

static int copyVersionValue(const char *line, char *versionBuf,
                            size_t versionBufSize)
{
    const char *value = line + strlen("VERSION_ID=");
    size_t length = strcspn(value, "\r\n");

    if (length >= 2U && value[0] == '"' && value[length - 1U] == '"') {
        ++value;
        length -= 2U;
    }
    if (length == 0U || length >= versionBufSize) {
        errno = length == 0U ? EINVAL : ERANGE;
        return -1;
    }
    memcpy(versionBuf, value, length);
    versionBuf[length] = '\0';
    return 0;
}

int otaReadCurrentVersion(char *versionBuf, size_t versionBufSize)
{
    const char *path = getenv("OTA_AGENT_OS_RELEASE_FILE");
    char line[256];
    FILE *file;

    if (versionBuf == NULL || versionBufSize == 0U) {
        errno = EINVAL;
        return -1;
    }
    if (path == NULL || path[0] == '\0') {
        path = "/etc/os-release";
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "VERSION_ID=", strlen("VERSION_ID=")) == 0) {
            int result = copyVersionValue(line, versionBuf, versionBufSize);

            (void)fclose(file);
            return result;
        }
    }
    errno = ferror(file) ? EIO : EINVAL;
    (void)fclose(file);
    return -1;
}

static int normalizeEnvironmentValue(char *value, const char *name)
{
    size_t length = strcspn(value, "\r\n");

    value[length] = '\0';
    if (value[0] == '\0') {
        errno = ENODATA;
        return -1;
    }
    if ((strcmp(name, "upgrade_available") == 0 &&
         strcmp(value, "0") != 0 && strcmp(value, "1") != 0) ||
        (strcmp(name, "upgrade_available") != 0 &&
         strcmp(value, "A") != 0 && strcmp(value, "B") != 0)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int readRecoveryEnvironment(char *activeSlot, char *lastGoodSlot,
                                   char *upgradeAvailable)
{
    char errorBuf[128];

    if (readEnvironmentValue("active_slot", activeSlot, 8,
                             errorBuf, sizeof(errorBuf)) != 0 ||
        normalizeEnvironmentValue(activeSlot, "active_slot") != 0 ||
        readEnvironmentValue("last_good_slot", lastGoodSlot, 8,
                             errorBuf, sizeof(errorBuf)) != 0 ||
        normalizeEnvironmentValue(lastGoodSlot, "last_good_slot") != 0 ||
        readEnvironmentValue("upgrade_available", upgradeAvailable, 8,
                             errorBuf, sizeof(errorBuf)) != 0 ||
        normalizeEnvironmentValue(upgradeAvailable, "upgrade_available") != 0) {
        return -1;
    }
    return 0;
}

static int hasCmdlineToken(const char *cmdline, const char *token)
{
    const char *match = cmdline;
    size_t tokenLength = strlen(token);

    while ((match = strstr(match, token)) != NULL) {
        int startsToken = match == cmdline || match[-1] == ' ';
        char next = match[tokenLength];

        if (startsToken && (next == '\0' || isspace((unsigned char)next))) {
            return 1;
        }
        match += tokenLength;
    }
    return 0;
}

static int readCurrentSlot(char *currentSlot, size_t currentSlotSize)
{
    const char *path = getenv("BOARD_CMDLINE_FILE");
    char cmdline[2048];
    FILE *file;

    if (currentSlot == NULL || currentSlotSize < 2U) {
        errno = EINVAL;
        return -1;
    }
    path = path == NULL || path[0] == '\0' ? "/proc/cmdline" : path;
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    if (fgets(cmdline, sizeof(cmdline), file) == NULL) {
        int savedError = ferror(file) ? EIO : ENODATA;

        (void)fclose(file);
        errno = savedError;
        return -1;
    }
    (void)fclose(file);
    if (hasCmdlineToken(cmdline, "root=/dev/mmcblk0p2") ||
        hasCmdlineToken(cmdline, "root=PARTLABEL=rootfsA")) {
        return snprintf(currentSlot, currentSlotSize, "A") == 1 ? 0 : -1;
    }
    if (hasCmdlineToken(cmdline, "root=/dev/mmcblk0p3") ||
        hasCmdlineToken(cmdline, "root=PARTLABEL=rootfsB")) {
        return snprintf(currentSlot, currentSlotSize, "B") == 1 ? 0 : -1;
    }
    errno = ENODEV;
    return -1;
}

int otaRecoverPendingState(OtaState *state)
{
    char currentSlot[8];
    char activeSlot[8];
    char lastGoodSlot[8];
    char upgradeAvailable[8];

    if (state == NULL ||
        (strcmp(state->targetSlot, "A") != 0 &&
         strcmp(state->targetSlot, "B") != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (readCurrentSlot(currentSlot, sizeof(currentSlot)) != 0 ||
        readRecoveryEnvironment(activeSlot, lastGoodSlot,
                                upgradeAvailable) != 0) {
        return -1;
    }
    if (strcmp(currentSlot, state->targetSlot) == 0 &&
        strcmp(activeSlot, state->targetSlot) == 0 &&
        strcmp(lastGoodSlot, state->targetSlot) == 0 &&
        strcmp(upgradeAvailable, "0") == 0) {
        return otaStateSetResult(state, "committed", "success", "升级已提交");
    }
    if (strcmp(currentSlot, state->targetSlot) == 0 &&
        strcmp(activeSlot, state->targetSlot) == 0) {
        /* 提交脚本分步写环境时，等待 last_good_slot 与标志状态一致。 */
        errno = EAGAIN;
        return -1;
    }
    return otaStateSetResult(state, "failed", "error",
                             "升级后未运行在目标槽位");
}
