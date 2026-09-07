#ifndef OTA_EXEC_H
#define OTA_EXEC_H

#include <stddef.h>

int otaCheckUpgradeAllowed(char *errorBuf, size_t errorBufSize);
int otaRunUpgrade(const char *swuPath, int autoReboot,
                  char *errorBuf, size_t errorBufSize);
int otaReadCurrentVersion(char *versionBuf, size_t versionBufSize);

#endif
