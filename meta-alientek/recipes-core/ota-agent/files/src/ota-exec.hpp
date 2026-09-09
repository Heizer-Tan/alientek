#pragma once

#include "ota-state.hpp"

#include <cstddef>

int otaCheckUpgradeAllowed(char *errorBuf, size_t errorBufSize);
int otaPrepareTargetSlot(OtaState *state, char *errorBuf, size_t errorBufSize);
int otaRecoverPendingState(OtaState *state);
int otaRunUpgrade(const char *swuPath, int autoReboot,
                  char *errorBuf, size_t errorBufSize);
int otaReadCurrentVersion(char *versionBuf, size_t versionBufSize);
