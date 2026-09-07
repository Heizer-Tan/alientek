#ifndef OTA_STATE_H
#define OTA_STATE_H

#include <stddef.h>

typedef struct OtaState {
    char requestId[64];
    char version[32];
    char targetSlot[8];
    char phase[32];
    char result[16];
    char detail[256];
    int autoReboot;
} OtaState;

int otaStateLoad(const char *path, OtaState *state);
int otaStateSave(const char *path, const OtaState *state);
int otaStateAcquireLock(const char *lockPath);
void otaStateReleaseLock(int lockFd, const char *lockPath);
int otaStateRequestSeen(const OtaState *state, const char *requestId);

#endif
