#include "ota-agent.h"

#include <stdio.h>

int otaAgentRunForeground(void)
{
    puts("ota-agent foreground mode");
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
    return otaAgentRunForeground();
}
