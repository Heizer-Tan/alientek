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
/* 驱动收包/保活；timeoutMs 为建议等待时长，实际由库决定。 */
int otaMqttYield(int timeoutMs);
/* 取出一条待处理命令 JSON；无命令时返回 -1 且 errno=EAGAIN。 */
int otaMqttPopCommand(char *buffer, size_t bufferSize);
int otaMqttParseCommandJson(const char *jsonText, OtaState *state,
                            OtaMqttCommand *command);
int otaHandleCommandJson(const char *jsonText, OtaState *state);
int otaMqttBuildStatusPayload(const OtaState *state, char *payload,
                              size_t payloadSize);
int otaReportCommittedState(const OtaState *state);
int otaReportFailureState(const OtaState *state, const char *reason);
