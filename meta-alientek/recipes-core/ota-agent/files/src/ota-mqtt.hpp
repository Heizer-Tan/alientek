#pragma once

#include "ota-state.hpp"

#include <cstddef>

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
int otaReportCommittedState(const OtaState *state);
int otaReportFailureState(const OtaState *state, const char *reason);
