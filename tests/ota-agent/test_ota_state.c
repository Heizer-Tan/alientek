#include "ota-state.h"

#include <assert.h>
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

static void testSingleTaskLock(void)
{
    const char *lockPath = "tests/ota-agent/tmp-agent.lock";
    int lockFd = otaStateAcquireLock(lockPath);

    assert(lockFd >= 0);
    assert(otaStateAcquireLock(lockPath) < 0);
    otaStateReleaseLock(lockFd, lockPath);

    lockFd = otaStateAcquireLock(lockPath);
    assert(lockFd >= 0);
    otaStateReleaseLock(lockFd, lockPath);
}

int main(void)
{
    testStateRoundTrip();
    testSingleTaskLock();
    return 0;
}
