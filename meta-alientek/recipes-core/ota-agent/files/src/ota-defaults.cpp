/* SPDX-License-Identifier: MIT */
/* 共享：解析 /etc/default 下的 agent 配置（mqtt-agent / ota-agent） */

#include "ota-defaults.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *otaReadPathSetting(const char *name, const char *defaultPath)
{
    const char *path = getenv(name);

    if (path == NULL || path[0] == '\0') {
        return defaultPath;
    }
    return path;
}

char *otaTrimInPlace(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }
    return text;
}

int otaApplyDefaultAssignment(char *line)
{
    char *equals;
    char *key;
    char *value;
    const char *existing;

    line = otaTrimInPlace(line);
    if (line[0] == '\0' || line[0] == '#') {
        return 0;
    }
    if (strncmp(line, "export ", 7) == 0) {
        line = otaTrimInPlace(line + 7);
    }
    equals = strchr(line, '=');
    if (equals == NULL || equals == line) {
        return 0;
    }
    *equals = '\0';
    key = otaTrimInPlace(line);
    value = otaTrimInPlace(equals + 1);
    if ((value[0] == '"' || value[0] == '\'') && strlen(value) >= 2U) {
        char quote = value[0];
        size_t length = strlen(value);

        if (value[length - 1U] == quote) {
            value[length - 1U] = '\0';
            ++value;
        }
    }
    if (key[0] == '\0') {
        return 0;
    }
    existing = getenv(key);
    if (existing != NULL && existing[0] != '\0') {
        return 0;
    }
    return setenv(key, value, 0) == 0 ? 0 : -1;
}

void otaLoadDefaultFile(const char *path)
{
    char line[512];
    FILE *file;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        (void)otaApplyDefaultAssignment(line);
    }
    (void)fclose(file);
}
