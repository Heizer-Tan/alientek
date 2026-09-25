#pragma once

#include <cstddef>

int otaDownloadPackage(const char *url, const char *outputPath,
		       char *errorBuf, size_t errorBufSize);

/* 下载并回调进度 0..100（curl --progress-bar 解析） */
typedef void (*OtaDownloadProgressFn)(int percent, void *userData);
int otaDownloadPackageWithProgress(const char *url, const char *outputPath,
				   OtaDownloadProgressFn onProgress,
				   void *userData, char *errorBuf,
				   size_t errorBufSize);

int otaVerifySha256(const char *path, const char *expectedSha256,
		    char *errorBuf, size_t errorBufSize);
