#ifndef OTA_DOWNLOAD_H
#define OTA_DOWNLOAD_H

#include <stddef.h>

int otaDownloadPackage(const char *url, const char *outputPath,
                       char *errorBuf, size_t errorBufSize);
int otaVerifySha256(const char *path, const char *expectedSha256,
                    char *errorBuf, size_t errorBufSize);

#endif
