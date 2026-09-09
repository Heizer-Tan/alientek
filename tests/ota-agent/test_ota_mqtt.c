#include "ota-mqtt.hpp"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void testValidCommand(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};
    OtaMqttCommand command = {};

    assert(otaMqttParseCommandJson(jsonText, &state, &command) == 0);
    assert(strcmp(state.requestId, "req-42") == 0);
    assert(strcmp(state.version, "2.0.1") == 0);
    assert(strcmp(command.url, "https://example.com/update.swu") == 0);
    assert(strcmp(command.sha256,
                  "0123456789abcdef0123456789abcdef"
                  "0123456789abcdef0123456789abcdef") == 0);
    assert(state.autoReboot == 1);
}

static void testValidEscapedSlashUrl(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https:\\/\\/example.com\\/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};
    OtaMqttCommand command = {};

    assert(otaMqttParseCommandJson(jsonText, &state, &command) == 0);
    assert(strcmp(command.url, "https://example.com/update.swu") == 0);
}

static void testRejectsInvalidUtf8InCommandString(void)
{
    const char jsonText[] =
        "{\"requestId\":\"req-\x80\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testRejectsVerticalTabWhitespace(void)
{
    const char *jsonText =
        "{\v\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testRejectsTrailingGarbage(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}garbage";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testMissingField(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"autoReboot\":false}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == ENOMSG);
}

static void testInvalidSha256Length(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"abcd\",\"autoReboot\":false}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EINVAL);
}

static void testInvalidBooleanToken(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":truex}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testRejectsTrailingComma(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true,}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testRejectsMissingComma(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\" \"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testRejectsRequiredFieldInNestedObject(void)
{
    const char *jsonText =
        "{\"metadata\":{\"requestId\":\"req-42\"},\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EBADMSG);
}

static void testRejectsRepeatedField(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"requestId\":\"req-43\","
        "\"version\":\"2.0.1\",\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef\",\"autoReboot\":true}";
    OtaState state = {};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EEXIST);
}

static void testRejectsTrailingBackslashEscape(void)
{
    OtaState state;
    OtaMqttCommand command;
    const char *jsonText = "{\"requestId\":\"abc\\";

    memset(&state, 0, sizeof(state));
    memset(&command, 0, sizeof(command));
    errno = 0;
    assert(otaMqttParseCommandJson(jsonText, &state, &command) == -1);
    assert(errno == EBADMSG);
}

static void testStatusPayload(void)
{
    OtaState state = {};
    char payload[512];

    (void)snprintf(state.requestId, sizeof(state.requestId), "%s", "req-42");
    (void)snprintf(state.phase, sizeof(state.phase), "%s", "verifying");
    (void)snprintf(state.result, sizeof(state.result), "%s", "running");
    (void)snprintf(state.detail, sizeof(state.detail), "%s", "校验\"\r\t中");
    assert(otaMqttBuildStatusPayload(&state, payload, sizeof(payload)) == 0);
    assert(strstr(payload, "\"requestId\":\"req-42\"") != NULL);
    assert(strstr(payload, "\"phase\":\"verifying\"") != NULL);
    assert(strstr(payload, "\"result\":\"running\"") != NULL);
    assert(strstr(payload, "\"detail\":\"校验\\\"\\r\\t中\"") != NULL);
}

static void assertControlCharacterEscaped(unsigned char control,
                                          const char *payload)
{
    char expected[7];

    if (control == '\n') {
        assert(strstr(payload, "\\n") != NULL);
    } else if (control == '\r') {
        assert(strstr(payload, "\\r") != NULL);
    } else if (control == '\t') {
        assert(strstr(payload, "\\t") != NULL);
    } else {
        (void)snprintf(expected, sizeof(expected), "\\u%04x", control);
        assert(strstr(payload, expected) != NULL);
    }
}

static void testStatusPayloadEscapesAllControlCharacters(void)
{
    OtaState nulState = {};
    char nulPayload[512];
    unsigned char control;

    assert(otaMqttBuildStatusPayload(&nulState, nulPayload,
                                     sizeof(nulPayload)) == 0);
    assert(strstr(nulPayload, "\"detail\":\"\"") != NULL);
    for (control = 1; control <= 0x1f; ++control) {
        OtaState state = {};
        char payload[512];

        state.detail[0] = (char)control;
        assert(otaMqttBuildStatusPayload(&state, payload, sizeof(payload)) == 0);
        assertControlCharacterEscaped(control, payload);
    }
}

static void testStatusPayloadRejectsInvalidUtf8(void)
{
    OtaState state = {};
    char payload[512];

    state.detail[0] = (char)0x80;
    errno = 0;
    assert(otaMqttBuildStatusPayload(&state, payload, sizeof(payload)) == -1);
    assert(errno == EBADMSG);
    assert(payload[0] == '\0');
}

int main(void)
{
    testStatusPayloadRejectsInvalidUtf8();
    testValidCommand();
    testRejectsInvalidUtf8InCommandString();
    testRejectsVerticalTabWhitespace();
    testValidEscapedSlashUrl();
    testRejectsTrailingBackslashEscape();
    testMissingField();
    testInvalidSha256Length();
    testInvalidBooleanToken();
    testRejectsRepeatedField();
    testRejectsRequiredFieldInNestedObject();
    testRejectsMissingComma();
    testRejectsTrailingComma();
    testStatusPayload();
    testStatusPayloadEscapesAllControlCharacters();
    testRejectsTrailingGarbage();
    puts("ota mqtt tests passed");
    return 0;
}
