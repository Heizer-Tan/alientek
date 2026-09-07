#include "ota-mqtt.h"

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
    OtaState state = {0};

    assert(otaHandleCommandJson(jsonText, &state) == 0);
    assert(strcmp(state.requestId, "req-42") == 0);
    assert(strcmp(state.version, "2.0.1") == 0);
    assert(state.autoReboot == 1);
}

static void testMissingField(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"autoReboot\":false}";
    OtaState state = {0};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EINVAL);
}

static void testInvalidSha256Length(void)
{
    const char *jsonText =
        "{\"requestId\":\"req-42\",\"version\":\"2.0.1\","
        "\"url\":\"https://example.com/update.swu\","
        "\"sha256\":\"abcd\",\"autoReboot\":false}";
    OtaState state = {0};

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
    OtaState state = {0};

    errno = 0;
    assert(otaHandleCommandJson(jsonText, &state) == -1);
    assert(errno == EINVAL);
}

static void testStatusPayload(void)
{
    OtaState state = {0};
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

int main(void)
{
    testValidCommand();
    testMissingField();
    testInvalidSha256Length();
    testInvalidBooleanToken();
    testStatusPayload();
    puts("ota mqtt tests passed");
    return 0;
}
