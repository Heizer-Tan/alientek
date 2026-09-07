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

enum CommandField {
    FIELD_REQUEST_ID = 1U << 0,
    FIELD_VERSION = 1U << 1,
    FIELD_URL = 1U << 2,
    FIELD_SHA256 = 1U << 3,
    FIELD_AUTO_REBOOT = 1U << 4,
    FIELD_ALL = (1U << 5) - 1
};

static const unsigned char *skipJsonSpace(const unsigned char *cursor)
{
    while (*cursor == ' ' || *cursor == '\t' ||
           *cursor == '\n' || *cursor == '\r') {
        ++cursor;
    }
    return cursor;
}

static int failWithErrno(int errorNumber)
{
    errno = errorNumber;
    return -1;
}

static int hexDigitValue(unsigned char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static int parseUnicodeCodeUnit(const unsigned char **cursor,
                                unsigned int *codeUnit)
{
    unsigned int value = 0;
    int index;

    for (index = 0; index < 4; ++index) {
        int digit = hexDigitValue((*cursor)[index]);

        if (digit < 0) {
            return failWithErrno(EBADMSG);
        }
        value = (value << 4) | (unsigned int)digit;
    }
    *cursor += 4;
    *codeUnit = value;
    return 0;
}

static int parseUnicodeScalar(const unsigned char **cursor,
                              unsigned int *scalar)
{
    unsigned int high;
    unsigned int low;

    if (parseUnicodeCodeUnit(cursor, &high) != 0) {
        return -1;
    }
    if (high < 0xd800 || high > 0xdfff) {
        *scalar = high;
        return 0;
    }
    if (high > 0xdbff || (*cursor)[0] != '\\' || (*cursor)[1] != 'u') {
        return failWithErrno(EBADMSG);
    }
    *cursor += 2;
    if (parseUnicodeCodeUnit(cursor, &low) != 0 ||
        low < 0xdc00 || low > 0xdfff) {
        return failWithErrno(EBADMSG);
    }
    *scalar = 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00);
    return 0;
}

static size_t encodeUtf8(unsigned int scalar, unsigned char *bytes)
{
    if (scalar <= 0x7f) {
        bytes[0] = (unsigned char)scalar;
        return 1;
    }
    if (scalar <= 0x7ff) {
        bytes[0] = 0xc0 | (scalar >> 6);
        bytes[1] = 0x80 | (scalar & 0x3f);
        return 2;
    }
    if (scalar <= 0xffff) {
        bytes[0] = 0xe0 | (scalar >> 12);
        bytes[1] = 0x80 | ((scalar >> 6) & 0x3f);
        bytes[2] = 0x80 | (scalar & 0x3f);
        return 3;
    }
    bytes[0] = 0xf0 | (scalar >> 18);
    bytes[1] = 0x80 | ((scalar >> 12) & 0x3f);
    bytes[2] = 0x80 | ((scalar >> 6) & 0x3f);
    bytes[3] = 0x80 | (scalar & 0x3f);
    return 4;
}

static int appendUtf8(unsigned int scalar, char *output, size_t outputSize,
                      size_t *length)
{
    unsigned char bytes[4];
    size_t count = encodeUtf8(scalar, bytes);
    size_t index;

    if (*length + count >= outputSize) {
        return failWithErrno(EOVERFLOW);
    }
    for (index = 0; index < count; ++index) {
        output[(*length)++] = (char)bytes[index];
    }
    return 0;
}

static int parseJsonEscape(const unsigned char **cursor, char *output,
                           size_t outputSize, size_t *length)
{
    static const char escapes[] = "\"\\/bfnrt";
    static const unsigned char values[] = {'"', '\\', '/', '\b', '\f',
                                           '\n', '\r', '\t'};
    const char *match;
    unsigned int scalar;

    ++*cursor;
    match = strchr(escapes, **cursor);
    if (match != NULL) {
        scalar = values[match - escapes];
        ++*cursor;
    } else if (*(*cursor)++ == 'u') {
        if (parseUnicodeScalar(cursor, &scalar) != 0) {
            return -1;
        }
    } else {
        return failWithErrno(EBADMSG);
    }
    if (scalar == 0) {
        return failWithErrno(EINVAL);
    }
    return appendUtf8(scalar, output, outputSize, length);
}

static int parseJsonString(const unsigned char **cursor, char *output,
                           size_t outputSize)
{
    size_t length = 0;

    if (**cursor != '"' || outputSize == 0) {
        return failWithErrno(EBADMSG);
    }
    ++*cursor;
    while (**cursor != '\0' && **cursor != '"') {
        if (**cursor < 0x20) {
            return failWithErrno(EBADMSG);
        }
        if (**cursor == '\\') {
            if (parseJsonEscape(cursor, output, outputSize, &length) != 0) {
                return -1;
            }
            continue;
        }
        if (length + 1 >= outputSize) {
            return failWithErrno(EOVERFLOW);
        }
        output[length++] = (char)*(*cursor)++;
    }
    if (**cursor != '"') {
        return failWithErrno(EBADMSG);
    }
    if (length == 0) {
        return failWithErrno(EINVAL);
    }
    output[length] = '\0';
    ++*cursor;
    return 0;
}

static int parseJsonBoolean(const unsigned char **cursor, int *value)
{
    if (strncmp((const char *)*cursor, "true", 4) == 0) {
        *cursor += 4;
        *value = 1;
        return 0;
    }
    if (strncmp((const char *)*cursor, "false", 5) == 0) {
        *cursor += 5;
        *value = 0;
        return 0;
    }
    return failWithErrno(EBADMSG);
}

static int parseCommandStringField(const char *key,
                                   const unsigned char **cursor,
                                   OtaState *state, OtaMqttCommand *command)
{
    if (strcmp(key, "requestId") == 0) {
        return parseJsonString(cursor, state->requestId,
                               sizeof(state->requestId));
    }
    if (strcmp(key, "version") == 0) {
        return parseJsonString(cursor, state->version, sizeof(state->version));
    }
    if (strcmp(key, "url") == 0) {
        return parseJsonString(cursor, command->url, sizeof(command->url));
    }
    return parseJsonString(cursor, command->sha256, sizeof(command->sha256));
}

static unsigned int commandFieldForKey(const char *key)
{
    static const char *const keys[] = {
        "requestId", "version", "url", "sha256", "autoReboot"
    };
    unsigned int index;

    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        if (strcmp(key, keys[index]) == 0) {
            return 1U << index;
        }
    }
    return 0;
}

static int parseCommandMember(const unsigned char **cursor,
                              OtaState *state, OtaMqttCommand *command,
                              unsigned int *fields)
{
    char key[32];
    unsigned int field;

    if (parseJsonString(cursor, key, sizeof(key)) != 0) {
        return -1;
    }
    *cursor = skipJsonSpace(*cursor);
    if (*(*cursor)++ != ':') {
        return failWithErrno(EBADMSG);
    }
    *cursor = skipJsonSpace(*cursor);
    field = commandFieldForKey(key);
    if (field == 0) {
        return failWithErrno(EBADMSG);
    }
    if ((*fields & field) != 0) {
        return failWithErrno(EEXIST);
    }
    *fields |= field;
    if (field == FIELD_AUTO_REBOOT) {
        return parseJsonBoolean(cursor, &state->autoReboot);
    }
    return parseCommandStringField(key, cursor, state, command);
}

static int parseCommandObject(const char *jsonText, OtaState *state,
                              OtaMqttCommand *command)
{
    const unsigned char *cursor =
        skipJsonSpace((const unsigned char *)jsonText);
    unsigned int fields = 0;

    if (*cursor++ != '{') {
        return failWithErrno(EBADMSG);
    }
    for (;;) {
        cursor = skipJsonSpace(cursor);
        if (*cursor == '}') {
            return failWithErrno(ENOMSG);
        }
        if (parseCommandMember(&cursor, state, command, &fields) != 0) {
            return -1;
        }
        cursor = skipJsonSpace(cursor);
        if (*cursor == '}') {
            cursor = skipJsonSpace(cursor + 1);
            if (*cursor != '\0') {
                return failWithErrno(EBADMSG);
            }
            return fields == FIELD_ALL ? 0 : failWithErrno(ENOMSG);
        }
        if (*cursor++ != ',' || *skipJsonSpace(cursor) == '}') {
            return failWithErrno(EBADMSG);
        }
    }
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

    if (jsonText == NULL || state == NULL || command == NULL) {
        return failWithErrno(EINVAL);
    }
    if (parseCommandObject(jsonText, &parsedState, &parsedCommand) != 0) {
        return -1;
    }
    if (validateCommand(&parsedCommand) != 0) {
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
