#include "ota-download.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
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

int otaDownloadPackage(const char *url, const char *outputPath,
                       char *errorBuf, size_t errorBufSize)
{
    pid_t pid;
    char *const args[] = {"curl", "--fail", "--location", "--silent",
                          "--show-error", "--output", (char *)outputPath,
                          "--", (char *)url, NULL};

    if (url == NULL || url[0] == '\0' ||
        outputPath == NULL || outputPath[0] == '\0') {
        setError(errorBuf, errorBufSize, "下载参数不能为空");
        errno = EINVAL;
        return -1;
    }
    pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        _exit(127);
    }
    if (pid < 0) {
        setError(errorBuf, errorBufSize, "启动 curl 失败: %s", strerror(errno));
        return -1;
    }
    if (waitForCommand(pid, "curl", errorBuf, errorBufSize) != 0) {
        (void)unlink(outputPath);
        return -1;
    }
    return 0;
}

static int readCommandOutput(int fd, char *output, size_t outputSize)
{
    size_t used = 0U;
    ssize_t count;

    while (used + 1U < outputSize) {
        count = read(fd, output + used, outputSize - used - 1U);
        if (count > 0) {
            used += (size_t)count;
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            return -1;
        }
    }
    output[used] = '\0';
    return 0;
}

static int calculateSha256(const char *path, char *actualSha256,
                           char *errorBuf, size_t errorBufSize)
{
    int outputPipe[2];
    pid_t pid;

    if (pipe(outputPipe) != 0) {
        setError(errorBuf, errorBufSize, "创建 sha256sum 管道失败: %s",
                 strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid == 0) {
        (void)dup2(outputPipe[1], STDOUT_FILENO);
        close(outputPipe[0]);
        close(outputPipe[1]);
        execlp("sha256sum", "sha256sum", "--", path, (char *)NULL);
        _exit(127);
    }
    close(outputPipe[1]);
    if (pid < 0 || readCommandOutput(outputPipe[0], actualSha256, 80U) != 0) {
        int savedErrno = errno;

        close(outputPipe[0]);
        setError(errorBuf, errorBufSize, "执行 sha256sum 失败: %s",
                 strerror(savedErrno));
        return -1;
    }
    close(outputPipe[0]);
    return waitForCommand(pid, "sha256sum", errorBuf, errorBufSize);
}

static int isValidSha256(const char *sha256)
{
    size_t index;

    if (sha256 == NULL || strlen(sha256) != 64U) {
        return 0;
    }
    for (index = 0U; index < 64U; ++index) {
        if (!isxdigit((unsigned char)sha256[index])) {
            return 0;
        }
    }
    return 1;
}

int otaVerifySha256(const char *path, const char *expectedSha256,
                    char *errorBuf, size_t errorBufSize)
{
    char actualSha256[80];

    if (path == NULL || path[0] == '\0' || !isValidSha256(expectedSha256)) {
        setError(errorBuf, errorBufSize, "路径不能为空且 SHA256 必须为 64 位十六进制");
        errno = EINVAL;
        return -1;
    }
    if (calculateSha256(path, actualSha256, errorBuf, errorBufSize) != 0) {
        return -1;
    }
    if (strncasecmp(actualSha256, expectedSha256, 64U) != 0) {
        setError(errorBuf, errorBufSize, "SHA256 校验失败: 期望=%s，实际=%.64s",
                 expectedSha256, actualSha256);
        errno = EBADMSG;
        return -1;
    }
    return 0;
}
