#include "ota-mqtt.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct JsonBuilder {
    char *buffer;
    size_t size;
    size_t length;
} JsonBuilder;

static const unsigned char *skipJsonSpace(const unsigned char *cursor)
{
    while (isspace(*cursor)) {
        ++cursor;
    }
    return cursor;
}

static int updateJsonStringState(unsigned char character, int *inString,
                                 int *escaped)
{
    if (character < 0x20) {
        return 0;
    }
    if (*escaped) {
        *escaped = 0;
    } else if (character == '\\') {
        *escaped = 1;
    } else if (character == '"') {
        *inString = 0;
    }
    return 1;
}

static int hasStrictObjectBoundary(const char *jsonText)
{
    const unsigned char *cursor =
        skipJsonSpace((const unsigned char *)jsonText);
    int depth = 0;
    int inString = 0;
    int escaped = 0;

    if (*cursor != '{') {
        return 0;
    }
    for (; *cursor != '\0'; ++cursor) {
        if (inString) {
            if (!updateJsonStringState(*cursor, &inString, &escaped)) {
                return 0;
            }
            continue;
        }
        if (*cursor == '"') {
            inString = 1;
        } else if (*cursor == '{') {
            ++depth;
        } else if (*cursor == '}' && --depth == 0) {
            cursor = skipJsonSpace(cursor + 1);
            return *cursor == '\0';
        }
    }
    return 0;
}

static const char *findJsonValue(const char *jsonText, const char *key)
{
    char pattern[64];
    const char *cursor;

    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) < 0) {
        return NULL;
    }
    cursor = strstr(jsonText, pattern);
    if (cursor == NULL) {
        return NULL;
    }
    cursor += strlen(pattern);
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor++ != ':') {
        return NULL;
    }
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    return cursor;
}

static int readJsonString(const char *jsonText, const char *key,
                          char *output, size_t outputSize)
{
    const char *cursor = findJsonValue(jsonText, key);
    size_t length = 0;

    if (cursor == NULL || *cursor++ != '"' || outputSize == 0) {
        errno = EINVAL;
        return -1;
    }
    while (*cursor != '\0' && *cursor != '"') {
        if (*cursor == '\\' || (unsigned char)*cursor < 0x20 ||
            length + 1 >= outputSize) {
            errno = EINVAL;
            return -1;
        }
        output[length++] = *cursor++;
    }
    if (*cursor != '"' || length == 0) {
        errno = EINVAL;
        return -1;
    }
    output[length] = '\0';
    return 0;
}

static int readJsonBoolean(const char *jsonText, const char *key, int *value)
{
    const char *cursor = findJsonValue(jsonText, key);
    const char *end;

    if (cursor != NULL && strncmp(cursor, "true", 4) == 0) {
        end = cursor + 4;
        *value = 1;
    } else if (cursor != NULL && strncmp(cursor, "false", 5) == 0) {
        end = cursor + 5;
        *value = 0;
    } else {
        errno = EINVAL;
        return -1;
    }
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end == ',' || *end == '}') {
        return 0;
    }
    errno = EINVAL;
    return -1;
}

static int isValidSha256(const char *sha256)
{
    size_t index;

    if (strlen(sha256) != 64) {
        return 0;
    }
    for (index = 0; index < 64; ++index) {
        if (!isxdigit((unsigned char)sha256[index])) {
            return 0;
        }
    }
    return 1;
}

static int validateCommand(const OtaMqttCommand *command)
{
    if ((strncmp(command->url, "https://", 8) != 0 &&
         strncmp(command->url, "http://", 7) != 0) ||
        !isValidSha256(command->sha256)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int otaMqttParseCommandJson(const char *jsonText, OtaState *state,
                            OtaMqttCommand *command)
{
    OtaState parsedState = {0};
    OtaMqttCommand parsedCommand = {0};

    if (jsonText == NULL || state == NULL || command == NULL ||
        !hasStrictObjectBoundary(jsonText) ||
        readJsonString(jsonText, "requestId", parsedState.requestId,
                       sizeof(parsedState.requestId)) != 0 ||
        readJsonString(jsonText, "version", parsedState.version,
                       sizeof(parsedState.version)) != 0 ||
        readJsonString(jsonText, "url", parsedCommand.url,
                       sizeof(parsedCommand.url)) != 0 ||
        readJsonString(jsonText, "sha256", parsedCommand.sha256,
                       sizeof(parsedCommand.sha256)) != 0 ||
        readJsonBoolean(jsonText, "autoReboot", &parsedState.autoReboot) != 0 ||
        validateCommand(&parsedCommand) != 0) {
        errno = EINVAL;
        return -1;
    }
    *state = parsedState;
    *command = parsedCommand;
    return 0;
}

int otaHandleCommandJson(const char *jsonText, OtaState *state)
{
    OtaMqttCommand command;

    return otaMqttParseCommandJson(jsonText, state, &command);
}

static int appendText(JsonBuilder *builder, const char *text)
{
    size_t textLength = strlen(text);

    if (builder->length + textLength >= builder->size) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(builder->buffer + builder->length, text, textLength);
    builder->length += textLength;
    builder->buffer[builder->length] = '\0';
    return 0;
}

static int appendControlCharacter(JsonBuilder *builder,
                                  unsigned char character)
{
    char escaped[7];
    int length = snprintf(escaped, sizeof(escaped), "\\u%04x", character);

    if (length != 6) {
        errno = EINVAL;
        return -1;
    }
    return appendText(builder, escaped);
}

static int appendEscaped(JsonBuilder *builder, const char *text)
{
    for (; *text != '\0'; ++text) {
        const char *escaped = NULL;
        char character[2] = {*text, '\0'};

        if (*text == '"') {
            escaped = "\\\"";
        } else if (*text == '\\') {
            escaped = "\\\\";
        } else if (*text == '\n') {
            escaped = "\\n";
        } else if (*text == '\r') {
            escaped = "\\r";
        } else if (*text == '\t') {
            escaped = "\\t";
        }
        if ((unsigned char)*text < 0x20 && escaped == NULL) {
            if (appendControlCharacter(builder, (unsigned char)*text) != 0) {
                return -1;
            }
        } else if (appendText(builder,
                              escaped != NULL ? escaped : character) != 0) {
            return -1;
        }
    }
    return 0;
}

static int appendStatusField(JsonBuilder *builder, const char *key,
                             const char *value, const char *suffix)
{
    char prefix[64];

    if (snprintf(prefix, sizeof(prefix), "\"%s\":\"", key) < 0 ||
        appendText(builder, prefix) != 0 ||
        appendEscaped(builder, value) != 0 ||
        appendText(builder, suffix) != 0) {
        return -1;
    }
    return 0;
}

int otaMqttBuildStatusPayload(const OtaState *state, char *payload,
                              size_t payloadSize)
{
    JsonBuilder builder = {payload, payloadSize, 0};

    if (state == NULL || payload == NULL || payloadSize == 0) {
        errno = EINVAL;
        return -1;
    }
    payload[0] = '\0';
    if (appendText(&builder, "{") != 0 ||
        appendStatusField(&builder, "requestId", state->requestId, "\",") != 0 ||
        appendStatusField(&builder, "phase", state->phase, "\",") != 0 ||
        appendStatusField(&builder, "result", state->result, "\",") != 0 ||
        appendStatusField(&builder, "detail", state->detail, "\"}") != 0) {
        return -1;
    }
    return 0;
}

int otaMqttConnect(const char *host, int port, const char *clientId)
{
    if (host == NULL || host[0] == '\0' || port < 1 || port > 65535 ||
        clientId == NULL || clientId[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    errno = ENOSYS;
    return -1;
}

int otaMqttSubscribeCommand(const char *topic)
{
    if (topic == NULL || topic[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    errno = ENOSYS;
    return -1;
}

int otaMqttPublishStatus(const char *topic, const char *payload)
{
    if (topic == NULL || topic[0] == '\0' ||
        payload == NULL || payload[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    errno = ENOSYS;
    return -1;
}
