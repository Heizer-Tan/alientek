#pragma once

#include <cstddef>

int otaDownloadPackage(const char *url, const char *outputPath,
                       char *errorBuf, size_t errorBufSize);
int otaVerifySha256(const char *path, const char *expectedSha256,
                    char *errorBuf, size_t errorBufSize);
