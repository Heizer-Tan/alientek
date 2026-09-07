#include "ota-agent.h"
#include "ota-state.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int otaAgentRunForeground(void)
{
    const char *lockPath = getenv("OTA_AGENT_LOCK_FILE");
    int lockFd;

    if (lockPath == NULL || lockPath[0] == '\0') {
        lockPath = "/var/run/ota-agent.lock";
    }
    lockFd = otaStateAcquireLock(lockPath);
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
    (void)argc;
    (void)argv;
    return otaAgentRunDaemon();
}
