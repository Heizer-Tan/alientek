#include "ota-exec.h"

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

static int readUpgradeAvailable(char *value, size_t valueSize,
                                char *errorBuf, size_t errorBufSize)
{
    int outputPipe[2];
    pid_t pid;
    ssize_t count;

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
        execlp("fw_printenv", "fw_printenv", "-n", "upgrade_available",
               (char *)NULL);
        _exit(127);
    }
    close(outputPipe[1]);
    count = pid < 0 ? -1 : read(outputPipe[0], value, valueSize - 1U);
    close(outputPipe[0]);
    if (pid < 0 || count < 0) {
        setError(errorBuf, errorBufSize, "读取 upgrade_available 失败: %s",
                 strerror(errno));
        return -1;
    }
    value[count] = '\0';
    return waitForCommand(pid, "fw_printenv", errorBuf, errorBufSize);
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
    char *normalArgs[] = {"board-apply-update", (char *)swuPath, NULL};
    char *rebootArgs[] = {"board-apply-update", "--reboot",
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
