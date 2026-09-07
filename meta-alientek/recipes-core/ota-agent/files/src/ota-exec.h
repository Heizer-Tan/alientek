#ifndef OTA_EXEC_H
#define OTA_EXEC_H

#include "ota-state.h"

#include <stddef.h>

int otaCheckUpgradeAllowed(char *errorBuf, size_t errorBufSize);
int otaPrepareTargetSlot(OtaState *state, char *errorBuf, size_t errorBufSize);
int otaRecoverPendingState(OtaState *state);
int otaRunUpgrade(const char *swuPath, int autoReboot,
                  char *errorBuf, size_t errorBufSize);
int otaReadCurrentVersion(char *versionBuf, size_t versionBufSize);

#endif
