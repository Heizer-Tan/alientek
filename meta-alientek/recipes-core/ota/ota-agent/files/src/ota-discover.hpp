#pragma once

#include <cstddef>

/*
 * 从 HTTP 目录列表解析最新 alientek-image-update*.swu，写出完整 URL（及可选文件名）。
 * baseUrl 例：http://192.168.5.13:8000
 */
int otaResolveLatestSwuUrl(const char *baseUrl, char *urlOut, size_t urlOutSize,
			   char *nameOut, size_t nameOutSize, char *errorBuf,
			   size_t errorBufSize);
