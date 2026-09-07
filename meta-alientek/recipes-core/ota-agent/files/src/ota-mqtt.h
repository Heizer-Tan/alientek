#ifndef OTA_MQTT_H
#define OTA_MQTT_H

#include "ota-state.h"

#include <stddef.h>

typedef struct OtaMqttCommand {
    char url[512];
    char sha256[65];
} OtaMqttCommand;

int otaMqttConnect(const char *host, int port, const char *clientId);
int otaMqttSubscribeCommand(const char *topic);
int otaMqttPublishStatus(const char *topic, const char *payload);
int otaMqttParseCommandJson(const char *jsonText, OtaState *state,
                            OtaMqttCommand *command);
int otaHandleCommandJson(const char *jsonText, OtaState *state);
int otaMqttBuildStatusPayload(const OtaState *state, char *payload,
                              size_t payloadSize);

#endif
