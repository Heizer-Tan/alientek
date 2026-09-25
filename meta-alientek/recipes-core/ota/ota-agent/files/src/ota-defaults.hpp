/* SPDX-License-Identifier: MIT */
#ifndef OTA_DEFAULTS_HPP
#define OTA_DEFAULTS_HPP

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 读环境变量；空则回退 defaultPath */
const char *otaReadPathSetting(const char *name, const char *defaultPath);

/* 原地去空白；返回新起点 */
char *otaTrimInPlace(char *text);

/* 解析 KEY=VALUE / export KEY=VALUE；未设置时 setenv；0 成功 */
int otaApplyDefaultAssignment(char *line);

/* 加载 defaults 文件（不存在则静默跳过） */
void otaLoadDefaultFile(const char *path);

#ifdef __cplusplus
}
#endif

#endif
