#include "ota-agent.h"

#include <stdio.h>
#include <unistd.h>

int otaAgentRunForeground(void)
{
    puts("ota-agent foreground mode");

    /* Task 1 仅提供常驻骨架，后续任务再加入实际事件处理。 */
    for (;;) {
        (void)pause();
    }
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
