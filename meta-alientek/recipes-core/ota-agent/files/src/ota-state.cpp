#include "ota-state.hpp"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *getEscapeSequence(unsigned char character)
{
    switch (character) {
    case '"':
        return "\\\"";
    case '\\':
        return "\\\\";
    case '\n':
        return "\\n";
    case '\r':
        return "\\r";
    case '\t':
        return "\\t";
    default:
        return NULL;
    }
}

static int writeEscapedString(FILE *file, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    for (; *cursor != '\0'; ++cursor) {
        const char *escapeSequence = getEscapeSequence(*cursor);

        if ((escapeSequence != NULL && fputs(escapeSequence, file) == EOF) ||
            (escapeSequence == NULL && fputc(*cursor, file) == EOF)) {
            return -1;
        }
    }
    return 0;
}

static int writeStringField(
    FILE *file, const char *key, const char *value, int trailingComma)
{
    if (fprintf(file, "  \"%s\": \"", key) < 0 ||
        writeEscapedString(file, value) < 0 ||
        fprintf(file, "\"%s\n", trailingComma ? "," : "") < 0) {
        return -1;
    }
    return 0;
}

static int writeState(FILE *file, const OtaState *state)
{
    if (fputs("{\n", file) == EOF ||
        writeStringField(file, "requestId", state->requestId, 1) < 0 ||
        writeStringField(file, "version", state->version, 1) < 0 ||
        writeStringField(file, "targetSlot", state->targetSlot, 1) < 0 ||
        writeStringField(file, "phase", state->phase, 1) < 0 ||
        writeStringField(file, "result", state->result, 1) < 0 ||
        writeStringField(file, "detail", state->detail, 1) < 0 ||
        fprintf(file, "  \"autoReboot\": %d\n}\n", state->autoReboot) < 0) {
        return -1;
    }
    return 0;
}

static int decodeEscapedCharacter(char character, char *decoded)
{
    switch (character) {
    case '"':
    case '\\':
        *decoded = character;
        return 0;
    case 'n':
        *decoded = '\n';
        return 0;
    case 'r':
        *decoded = '\r';
        return 0;
    case 't':
        *decoded = '\t';
        return 0;
    default:
        return -1;
    }
}

static int decodeString(const char *encoded, char *output, size_t outputSize)
{
    size_t outputLength = 0;

    if (*encoded++ != '"') {
        return -1;
    }
    while (*encoded != '\0' && *encoded != '"') {
        if (*encoded == '\\' && encoded[1] != '\0') {
            char decoded;

            ++encoded;
            if (decodeEscapedCharacter(*encoded++, &decoded) < 0 ||
                outputLength + 1 >= outputSize) {
                return -1;
            }
            output[outputLength++] = decoded;
            continue;
        }
        if (outputLength + 1 >= outputSize) {
            return -1;
        }
        output[outputLength++] = *encoded++;
    }
    if (*encoded != '"') {
        return -1;
    }
    output[outputLength] = '\0';
    return 0;
}

static int readStringField(
    FILE *file, const char *key, char *output, size_t outputSize)
{
    char line[1024];
    char expectedKey[96];
    char *value;

    if (fgets(line, sizeof(line), file) == NULL ||
        snprintf(expectedKey, sizeof(expectedKey), "\"%s\"", key) < 0 ||
        strstr(line, expectedKey) == NULL ||
        (value = strchr(line, ':')) == NULL) {
        return -1;
    }
    do {
        ++value;
    } while (isspace((unsigned char)*value));
    return decodeString(value, output, outputSize);
}

static int readAutoReboot(FILE *file, int *autoReboot)
{
    char line[128];
    char extra;

    if (fgets(line, sizeof(line), file) == NULL ||
        sscanf(line, " \"autoReboot\" : %d %c", autoReboot, &extra) != 1) {
        return -1;
    }
    return 0;
}

static int readState(FILE *file, OtaState *state)
{
    char line[32];

    if (fgets(line, sizeof(line), file) == NULL || strchr(line, '{') == NULL ||
        readStringField(file, "requestId", state->requestId,
                        sizeof(state->requestId)) < 0 ||
        readStringField(file, "version", state->version,
                        sizeof(state->version)) < 0 ||
        readStringField(file, "targetSlot", state->targetSlot,
                        sizeof(state->targetSlot)) < 0 ||
        readStringField(file, "phase", state->phase, sizeof(state->phase)) < 0 ||
        readStringField(file, "result", state->result,
                        sizeof(state->result)) < 0 ||
        readStringField(file, "detail", state->detail, sizeof(state->detail)) <
            0 ||
        readAutoReboot(file, &state->autoReboot) < 0 ||
        fgets(line, sizeof(line), file) == NULL || strchr(line, '}') == NULL) {
        return -1;
    }
    return 0;
}

static char *createTempPath(const char *path)
{
    size_t pathSize = strlen(path) + 32;
    char *tempPath = (char *)malloc(pathSize);

    if (tempPath != NULL) {
        (void)snprintf(tempPath, pathSize, "%s.tmp.%ld", path, (long)getpid());
    }
    return tempPath;
}

static int writeAndCloseState(FILE *file, const OtaState *state)
{
    int result = writeState(file, state);
    int savedError = result < 0 ? errno : 0;

    if (fclose(file) != 0 && result == 0) {
        result = -1;
        savedError = errno;
    }
    if (result < 0) {
        errno = savedError != 0 ? savedError : EIO;
    }
    return result;
}

int otaStateSave(const char *path, const OtaState *state)
{
    char *tempPath;
    FILE *file;
    int savedError;

    if (path == NULL || state == NULL) {
        errno = EINVAL;
        return -1;
    }
    tempPath = createTempPath(path);
    if (tempPath == NULL) {
        return -1;
    }
    file = fopen(tempPath, "w");
    if (file == NULL) {
        savedError = errno;
    } else if (writeAndCloseState(file, state) < 0) {
        savedError = errno;
    } else if (rename(tempPath, path) < 0) {
        savedError = errno;
    } else {
        free(tempPath);
        return 0;
    }
    (void)unlink(tempPath);
    free(tempPath);
    errno = savedError;
    return -1;
}

int otaStateLoad(const char *path, OtaState *state)
{
    OtaState loadedState;
    FILE *file;
    int result;
    int savedError = 0;

    if (path == NULL || state == NULL) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    memset(&loadedState, 0, sizeof(loadedState));
    errno = 0;
    result = readState(file, &loadedState);
    if (result < 0) {
        savedError = ferror(file) ? (errno != 0 ? errno : EIO) : EINVAL;
    }
    if (fclose(file) != 0 && result == 0) {
        result = -1;
        savedError = errno;
    }
    if (result < 0) {
        errno = savedError;
        return -1;
    }
    *state = loadedState;
    return 0;
}

static int readEnvValue(const char *name, char *value, size_t valueSize)
{
    int outputPipe[2];
    pid_t pid;
    ssize_t count;

    if (name == NULL || value == NULL || valueSize < 2U) {
        errno = EINVAL;
        return -1;
    }
    if (pipe(outputPipe) != 0) {
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
        return -1;
    }
    value[count] = '\0';
    while (count > 0 && (value[count - 1] == '\n' || value[count - 1] == '\r')) {
        value[--count] = '\0';
    }
    if (waitpid(pid, NULL, 0) < 0 || value[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int writeEnvValue(const char *name, const char *value)
{
    pid_t pid;
    int status;

    if (name == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    pid = fork();
    if (pid == 0) {
        execlp("fw_setenv", "fw_setenv", name, value, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        return -1;
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int readRequiredEnvField(
    const char *name, char *value, size_t valueSize, OtaState *stateFieldOwner)
{
    (void)stateFieldOwner;
    return readEnvValue(name, value, valueSize);
}

int otaStateLoadFromEnvironment(OtaState *state)
{
    if (state == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(state, 0, sizeof(*state));
    if (readRequiredEnvField("ota_request_id", state->requestId,
                             sizeof(state->requestId), state) != 0 ||
        readRequiredEnvField("ota_version", state->version,
                             sizeof(state->version), state) != 0 ||
        readRequiredEnvField("ota_target_slot", state->targetSlot,
                             sizeof(state->targetSlot), state) != 0 ||
        readRequiredEnvField("ota_phase", state->phase,
                             sizeof(state->phase), state) != 0) {
        return -1;
    }
    if (readEnvValue("ota_result", state->result, sizeof(state->result)) != 0) {
        state->result[0] = '\0';
        errno = 0;
    }
    if (readEnvValue("ota_detail", state->detail, sizeof(state->detail)) != 0) {
        state->detail[0] = '\0';
        errno = 0;
    }
    return 0;
}

int otaStateSaveToEnvironment(const OtaState *state)
{
    char autoReboot[16];

    if (state == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf(autoReboot, sizeof(autoReboot), "%d", state->autoReboot) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (writeEnvValue("ota_request_id", state->requestId) != 0 ||
        writeEnvValue("ota_version", state->version) != 0 ||
        writeEnvValue("ota_target_slot", state->targetSlot) != 0 ||
        writeEnvValue("ota_phase", state->phase) != 0 ||
        writeEnvValue("ota_result", state->result) != 0 ||
        writeEnvValue("ota_detail", state->detail) != 0 ||
        writeEnvValue("ota_auto_reboot", autoReboot) != 0) {
        return -1;
    }
    return 0;
}

int otaStateAcquireLock(const char *lockPath)
{
    int lockFd;
    int savedError;

    if (lockPath == NULL) {
        errno = EINVAL;
        return -1;
    }
    lockFd = open(lockPath, O_RDWR | O_CREAT, 0644);
    if (lockFd >= 0 && flock(lockFd, LOCK_EX | LOCK_NB) == 0) {
        return lockFd;
    }
    savedError = errno;
    if (lockFd >= 0) {
        (void)close(lockFd);
    }
    errno = savedError;
    return -1;
}

void otaStateReleaseLock(int lockFd, const char *lockPath)
{
    (void)lockPath;
    if (lockFd < 0) {
        return;
    }
    (void)flock(lockFd, LOCK_UN);
    (void)close(lockFd);
}

int otaStateRequestSeen(const OtaState *state, const char *requestId)
{
    if (state == NULL || requestId == NULL || requestId[0] == '\0') {
        return 0;
    }
    return strcmp(state->requestId, requestId) == 0;
}

int otaStateSetResult(OtaState *state, const char *phase,
                      const char *result, const char *detail)
{
    if (state == NULL || phase == NULL || result == NULL || detail == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (strlen(phase) >= sizeof(state->phase) ||
        strlen(result) >= sizeof(state->result) ||
        strlen(detail) >= sizeof(state->detail)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (phase != state->phase) {
        (void)snprintf(state->phase, sizeof(state->phase), "%s", phase);
    }
    if (result != state->result) {
        (void)snprintf(state->result, sizeof(state->result), "%s", result);
    }
    if (detail != state->detail) {
        (void)snprintf(state->detail, sizeof(state->detail), "%s", detail);
    }
    return 0;
}
