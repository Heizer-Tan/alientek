#include "ota-state.hpp"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void testStateRoundTrip(void)
{
    const char *statePath = "tests/ota-agent/tmp-state.json";
    OtaState state;

    memset(&state, 0, sizeof(state));
    strcpy(state.requestId, "req-001");
    strcpy(state.version, "5.0.20");
    strcpy(state.targetSlot, "B");
    strcpy(state.phase, "received");
    strcpy(state.result, "running");
    strcpy(state.detail, "failed\n\"checksum\"");
    state.autoReboot = 1;
    assert(otaStateSave(statePath, &state) == 0);

    memset(&state, 0, sizeof(state));
    assert(otaStateLoad(statePath, &state) == 0);
    assert(strcmp(state.requestId, "req-001") == 0);
    assert(strcmp(state.version, "5.0.20") == 0);
    assert(strcmp(state.targetSlot, "B") == 0);
    assert(strcmp(state.phase, "received") == 0);
    assert(strcmp(state.result, "running") == 0);
    assert(strcmp(state.detail, "failed\n\"checksum\"") == 0);
    assert(state.autoReboot == 1);
    assert(otaStateRequestSeen(&state, "req-001") == 1);
    assert(otaStateRequestSeen(&state, "req-002") == 0);
    assert(unlink(statePath) == 0);
}

static void testLoadPreservesReadError(void)
{
    OtaState state;

    errno = 0;
    assert(otaStateLoad("tests/ota-agent", &state) < 0);
    assert(errno == EISDIR);
}

static void testSavePreservesWriteError(void)
{
    const char *statePath = "tests/ota-agent/tmp-write-failure.json";
    char tempPath[128];
    OtaState state;

    memset(&state, 0, sizeof(state));
    assert(snprintf(tempPath, sizeof(tempPath), "%s.tmp.%ld", statePath,
                    (long)getpid()) > 0);
    (void)unlink(tempPath);
    assert(symlink("/dev/full", tempPath) == 0);
    errno = 0;
    assert(otaStateSave(statePath, &state) < 0);
    assert(errno == ENOSPC);
    assert(access(tempPath, F_OK) < 0);
}

static void testRejectsInvalidJsonEscape(void)
{
    const char *statePath = "tests/ota-agent/tmp-invalid-state.json";
    FILE *file = fopen(statePath, "w");
    OtaState state;

    assert(file != NULL);
    assert(fputs("{\n"
                 "\"requestId\":\"bad\\q\",\n"
                 "\"version\":\"\",\n"
                 "\"targetSlot\":\"\",\n"
                 "\"phase\":\"\",\n"
                 "\"result\":\"\",\n"
                 "\"detail\":\"\",\n"
                 "\"autoReboot\":0\n"
                 "}\n",
                 file) >= 0);
    assert(fclose(file) == 0);
    assert(otaStateLoad(statePath, &state) < 0);
    assert(errno == EINVAL);
    assert(unlink(statePath) == 0);
}

static void testSingleTaskLock(void)
{
    const char *lockPath = "tests/ota-agent/tmp-agent.lock";
    int lockFd = otaStateAcquireLock(lockPath);

    assert(lockFd >= 0);
    assert(otaStateAcquireLock(lockPath) < 0);
    otaStateReleaseLock(lockFd, lockPath);
    assert(access(lockPath, F_OK) == 0);

    lockFd = otaStateAcquireLock(lockPath);
    assert(lockFd >= 0);
    otaStateReleaseLock(lockFd, lockPath);
    assert(access(lockPath, F_OK) == 0);
    assert(unlink(lockPath) == 0);
}

int main(void)
{
    testStateRoundTrip();
    testLoadPreservesReadError();
    testSavePreservesWriteError();
    testRejectsInvalidJsonEscape();
    testSingleTaskLock();
    return 0;
}
