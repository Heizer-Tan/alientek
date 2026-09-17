/* SPDX-License-Identifier: MIT */
/* mqtt-agent：MQTT 会话（探测/心跳/订命令），升级交给 ota-agent */

#include "ota-defaults.hpp"
#include "ota-mqtt.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stopRequested = 0;
static int g_mqttPort = 1883;
static int g_mqttAutoMode = 1;
static int g_heartbeatSec = 30;
static int g_otaBusy = 0;
static int g_otaPipeFd = -1;
static pid_t g_otaPid = -1;
static char g_mqttClientId[96] = "ota-agent";
static char g_commandTopic[192] = "device/ota/command";
static char g_statusTopic[192] = "device/ota/status";
static char g_heartbeatTopic[192] = "device/heartbeat";
static char g_fixedHost[64];
static char g_otaBin[256] = "/usr/bin/ota-agent";
static char g_downloadPath[256] = "/var/tmp/ota-download.swu";
static char g_otaLineBuf[1024];
static size_t g_otaLineLen = 0;
static time_t g_lastDiscoverAt = 0;
static time_t g_lastHeartbeatAt = 0;

static void onSignal(int signo)
{
    (void)signo;
    g_stopRequested = 1;
}

static void loadConfigs(void)
{
    otaLoadDefaultFile(otaReadPathSetting(
        "MQTT_AGENT_CONFIG_FILE", "/etc/default/mqtt-agent"));
    otaLoadDefaultFile(otaReadPathSetting(
        "OTA_AGENT_CONFIG_FILE", "/etc/default/ota-agent"));
}


static int isAutoMqttHost(const char *host)
{
    return host == NULL || host[0] == '\0' ||
           strcmp(host, "auto") == 0 || strcmp(host, "AUTO") == 0;
}

static int parsePort(const char *text, int *portOut)
{
    char *end = NULL;
    long port = strtol(text, &end, 10);

    if (text == NULL || end == text || *end != '\0' || port < 1 ||
        port > 65535) {
        errno = EINVAL;
        return -1;
    }
    *portOut = (int)port;
    return 0;
}

static const char *brokerCachePath(void)
{
    return otaReadPathSetting(
        "OTA_MQTT_BROKER_CACHE", "/var/lib/mqtt-agent/mqtt-broker.host");
}

static int loadCachedHost(char *hostOut, size_t hostOutSize)
{
    FILE *file = fopen(brokerCachePath(), "r");
    size_t length;

    if (file == NULL) {
        return -1;
    }
    if (fgets(hostOut, (int)hostOutSize, file) == NULL) {
        (void)fclose(file);
        return -1;
    }
    (void)fclose(file);
    length = strcspn(hostOut, "\r\n");
    hostOut[length] = '\0';
    return hostOut[0] == '\0' ? -1 : 0;
}

static void saveCachedHost(const char *host)
{
    FILE *file;

    if (host == NULL || host[0] == '\0') {
        return;
    }
    file = fopen(brokerCachePath(), "w");
    if (file == NULL) {
        return;
    }
    (void)fprintf(file, "%s\n", host);
    (void)fclose(file);
}

static int setNonBlock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int probeTcpPort(const char *ip, int port, int timeoutMs)
{
    struct sockaddr_in addr;
    struct timeval timeout;
    fd_set writeSet;
    int fd;
    int soError = 0;
    socklen_t soLen = sizeof(soError);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setNonBlock(fd) != 0) {
        if (fd >= 0) {
            (void)close(fd);
        }
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        (void)close(fd);
        return 0;
    }
    if (errno != EINPROGRESS) {
        (void)close(fd);
        return -1;
    }
    FD_ZERO(&writeSet);
    FD_SET(fd, &writeSet);
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    if (select(fd + 1, NULL, &writeSet, NULL, &timeout) <= 0 ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0 ||
        soError != 0) {
        (void)close(fd);
        return -1;
    }
    (void)close(fd);
    return 0;
}

static int readIfaceIpv4(uint32_t *addrOut, uint32_t *maskOut)
{
    const char *iface = otaReadPathSetting("OTA_MQTT_DISCOVER_IFACE", "eth0");
    struct ifaddrs *list = NULL;
    struct ifaddrs *cursor;
    int found = 0;

    if (getifaddrs(&list) != 0) {
        return -1;
    }
    for (cursor = list; cursor != NULL; cursor = cursor->ifa_next) {
        struct sockaddr_in *addr;
        struct sockaddr_in *mask;

        if (cursor->ifa_addr == NULL || cursor->ifa_netmask == NULL ||
            cursor->ifa_addr->sa_family != AF_INET ||
            strcmp(cursor->ifa_name, iface) != 0) {
            continue;
        }
        addr = (struct sockaddr_in *)cursor->ifa_addr;
        mask = (struct sockaddr_in *)cursor->ifa_netmask;
        *addrOut = ntohl(addr->sin_addr.s_addr);
        *maskOut = ntohl(mask->sin_addr.s_addr);
        found = 1;
        break;
    }
    freeifaddrs(list);
    return found ? 0 : -1;
}

static int formatIpv4(uint32_t hostOrderIp, char *out, size_t outSize)
{
    struct in_addr addr;

    addr.s_addr = htonl(hostOrderIp);
    return inet_ntop(AF_INET, &addr, out, (socklen_t)outSize) == NULL ? -1 : 0;
}

static int scanLanForBroker(int port, char *hostOut, size_t hostOutSize)
{
    uint32_t selfIp = 0;
    uint32_t mask = 0;
    uint32_t network;
    uint32_t hostBits;
    uint32_t host;

    if (readIfaceIpv4(&selfIp, &mask) != 0 || mask == 0U) {
        return -1;
    }
    network = selfIp & mask;
    hostBits = ~mask;
    if (hostBits == 0U || hostBits > 0xFFFFU) {
        errno = ERANGE;
        return -1;
    }
    for (host = 1U; host < hostBits; ++host) {
        uint32_t candidate = network | host;
        char ipText[INET_ADDRSTRLEN];

        if (candidate == selfIp ||
            formatIpv4(candidate, ipText, sizeof(ipText)) != 0) {
            continue;
        }
        if (probeTcpPort(ipText, port, 40) == 0) {
            if (snprintf(hostOut, hostOutSize, "%s", ipText) < 0 ||
                strlen(ipText) >= hostOutSize) {
                return -1;
            }
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int resolveHost(char *hostOut, size_t hostOutSize)
{
    char cached[64];

    if (g_mqttAutoMode == 0) {
        return snprintf(hostOut, hostOutSize, "%s", g_fixedHost) < 0 ? -1 : 0;
    }
    if (loadCachedHost(cached, sizeof(cached)) == 0 &&
        probeTcpPort(cached, g_mqttPort, 200) == 0) {
        return snprintf(hostOut, hostOutSize, "%s", cached) < 0 ? -1 : 0;
    }
    if (scanLanForBroker(g_mqttPort, hostOut, hostOutSize) != 0) {
        return -1;
    }
    saveCachedHost(hostOut);
    return 0;
}

static void connectSession(const char *host)
{
    fprintf(stderr, "mqtt-agent 连接 %s:%d\n", host, g_mqttPort);
    if (otaMqttConnect(host, g_mqttPort, g_mqttClientId) != 0) {
        fprintf(stderr, "MQTT 连接失败: %s\n", strerror(errno));
        return;
    }
    if (otaMqttSubscribeCommand(g_commandTopic) != 0 &&
        errno != ENOTCONN && errno != ENOSYS) {
        fprintf(stderr, "订阅失败: %s\n", strerror(errno));
    }
}

static void ensureSession(void)
{
    char host[64];
    time_t now = time(NULL);

    if (otaMqttIsConnected() != 0) {
        return;
    }
    if (g_mqttAutoMode != 0 && g_lastDiscoverAt != 0 &&
        now - g_lastDiscoverAt < 10) {
        return;
    }
    g_lastDiscoverAt = now;
    if (resolveHost(host, sizeof(host)) != 0) {
        fprintf(stderr, "MQTT broker 未探测到\n");
        return;
    }
    if (g_mqttAutoMode != 0) {
        saveCachedHost(host);
    }
    connectSession(host);
}

static int looksLikeStatusJson(const char *line)
{
    return line != NULL && line[0] == '{' && strstr(line, "\"phase\"") != NULL;
}

static void forwardStatusLine(const char *line)
{
    if (!looksLikeStatusJson(line)) {
        return;
    }
    if (otaMqttPublishStatus(g_statusTopic, line) != 0) {
        fprintf(stderr, "转发 status 失败: %s\n", strerror(errno));
    }
}

/* 短跑 precheck：fork/exec，子进程 emit 后退出；与「重复 requestId」同样靠 EOF 立刻转发。 */
static int runOtaPrecheck(const char *jsonText)
{
    int pipefds[2];
    pid_t pid;
    FILE *pipeFile;
    char line[1024];
    int status;
    int fd;
    long openMax;

    fprintf(stderr, "mqtt-agent: PRECHECK begin\n");
    if (pipe(pipefds) != 0) {
        fprintf(stderr, "precheck 创建管道失败: %s\n", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "precheck fork 失败: %s\n", strerror(errno));
        (void)close(pipefds[0]);
        (void)close(pipefds[1]);
        return -1;
    }
    if (pid == 0) {
        (void)close(pipefds[0]);
        if (dup2(pipefds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        (void)close(pipefds[1]);
        openMax = sysconf(_SC_OPEN_MAX);
        if (openMax < 0 || openMax > 1024) {
            openMax = 1024;
        }
        for (fd = 3; fd < (int)openMax; ++fd) {
            (void)close(fd);
        }
        execl(g_otaBin, g_otaBin, "--mqtt-precheck", jsonText, (char *)NULL);
        _exit(127);
    }
    (void)close(pipefds[1]);
    pipeFile = fdopen(pipefds[0], "r");
    if (pipeFile == NULL) {
        fprintf(stderr, "precheck fdopen 失败: %s\n", strerror(errno));
        (void)close(pipefds[0]);
        (void)kill(pid, SIGTERM);
        (void)waitpid(pid, NULL, 0);
        return -1;
    }
    while (fgets(line, sizeof(line), pipeFile) != NULL) {
        size_t length = strcspn(line, "\r\n");

        line[length] = '\0';
        puts(line);
        fflush(stdout);
        fprintf(stderr, "mqtt-agent: PRECHECK line: %s\n", line);
        fflush(stderr);
        forwardStatusLine(line);
    }
    (void)fclose(pipeFile);
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "precheck waitpid 失败: %s\n", strerror(errno));
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "mqtt-agent: PRECHECK fail exit=%d\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    fprintf(stderr, "mqtt-agent: PRECHECK ok\n");
    return 0;
}

static void finishOtaChild(void)
{
    int status = 0;

    if (g_otaPipeFd >= 0) {
        (void)close(g_otaPipeFd);
        g_otaPipeFd = -1;
    }
    if (g_otaPid > 0) {
        (void)waitpid(g_otaPid, &status, 0);
        g_otaPid = -1;
    }
    g_otaLineLen = 0;
    g_otaBusy = 0;
}

static void emitOtaLine(void)
{
    if (g_otaLineLen == 0) {
        return;
    }
    g_otaLineBuf[g_otaLineLen] = '\0';
    puts(g_otaLineBuf);
    fflush(stdout);
    forwardStatusLine(g_otaLineBuf);
    g_otaLineLen = 0;
}

static void consumeOtaChunk(const char *chunk, size_t chunkLen)
{
    size_t index;

    for (index = 0; index < chunkLen; ++index) {
        char character = chunk[index];

        if (character == '\n' || character == '\r') {
            emitOtaLine();
            continue;
        }
        if (g_otaLineLen + 1 < sizeof(g_otaLineBuf)) {
            g_otaLineBuf[g_otaLineLen++] = character;
        }
    }
}

static void pollOtaOutput(void)
{
    char buffer[256];
    ssize_t readSize;

    if (g_otaPipeFd < 0) {
        return;
    }
    for (;;) {
        readSize = read(g_otaPipeFd, buffer, sizeof(buffer));
        if (readSize > 0) {
            consumeOtaChunk(buffer, (size_t)readSize);
            continue;
        }
        if (readSize == 0) {
            emitOtaLine();
            finishOtaChild();
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        fprintf(stderr, "读 ota-agent 输出失败: %s\n", strerror(errno));
        finishOtaChild();
        return;
    }
}

static int startOtaChild(const char *jsonText)
{
    int pipefds[2];
    pid_t pid;

    if (pipe(pipefds) != 0) {
        fprintf(stderr, "创建管道失败: %s\n", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork ota-agent 失败: %s\n", strerror(errno));
        (void)close(pipefds[0]);
        (void)close(pipefds[1]);
        return -1;
    }
    if (pid == 0) {
        int fd;
        long openMax;

        (void)close(pipefds[0]);
        if (dup2(pipefds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        (void)close(pipefds[1]);
        /* 关掉继承的 MQTT 套接字等，避免子进程拖垮父进程会话。 */
        openMax = sysconf(_SC_OPEN_MAX);
        if (openMax < 0 || openMax > 1024) {
            openMax = 1024;
        }
        for (fd = 3; fd < (int)openMax; ++fd) {
            (void)close(fd);
        }
        /* 已在 precheck 发过 accepted，升级进程不再重复发。 */
        (void)setenv("OTA_MQTT_SKIP_ACCEPTED", "1", 1);
        execl(g_otaBin, g_otaBin, "--mqtt-command", jsonText, g_downloadPath,
              (char *)NULL);
        _exit(127);
    }
    (void)close(pipefds[1]);
    if (setNonBlock(pipefds[0]) != 0) {
        fprintf(stderr, "设置管道非阻塞失败: %s\n", strerror(errno));
        (void)close(pipefds[0]);
        (void)kill(pid, SIGTERM);
        (void)waitpid(pid, NULL, 0);
        return -1;
    }
    g_otaPipeFd = pipefds[0];
    g_otaPid = pid;
    g_otaLineLen = 0;
    g_otaBusy = 1;
    fprintf(stderr, "mqtt-agent: 异步启动 ota-agent 升级 pid=%ld\n",
            (long)pid);
    return 0;
}

static void startOtaCommand(const char *jsonText)
{
    if (g_otaBusy != 0) {
        fprintf(stderr, "ota-agent 忙，丢弃命令\n");
        return;
    }
    if (runOtaPrecheck(jsonText) != 0) {
        return;
    }
    (void)startOtaChild(jsonText);
}

static void processCommand(void)
{
    char jsonText[4096];

    if (g_otaBusy != 0 || otaMqttPopCommand(jsonText, sizeof(jsonText)) != 0) {
        return;
    }
    startOtaCommand(jsonText);
}

static void maybeHeartbeat(void)
{
    char payload[256];
    time_t now;

    if (g_heartbeatSec <= 0 || otaMqttIsConnected() == 0) {
        return;
    }
    now = time(NULL);
    if (g_lastHeartbeatAt != 0 && now - g_lastHeartbeatAt < g_heartbeatSec) {
        return;
    }
    if (snprintf(payload, sizeof(payload),
                 "{\"clientId\":\"%s\",\"phase\":\"heartbeat\",\"ts\":%ld}",
                 g_mqttClientId, (long)now) < 0) {
        return;
    }
    if (otaMqttPublishStatus(g_heartbeatTopic, payload) == 0) {
        g_lastHeartbeatAt = now;
    }
}

static int loadRuntimeSettings(void)
{
    const char *host = otaReadPathSetting("OTA_MQTT_HOST", "auto");
    long heartbeat;

    if (parsePort(otaReadPathSetting("OTA_MQTT_PORT", "1883"), &g_mqttPort) !=
        0) {
        return -1;
    }
    (void)snprintf(g_mqttClientId, sizeof(g_mqttClientId), "%s",
                   otaReadPathSetting("OTA_MQTT_CLIENT_ID", "ota-agent"));
    (void)snprintf(g_commandTopic, sizeof(g_commandTopic), "%s",
                   otaReadPathSetting("OTA_MQTT_COMMAND_TOPIC",
                                   "device/ota/command"));
    (void)snprintf(g_statusTopic, sizeof(g_statusTopic), "%s",
                   otaReadPathSetting("OTA_MQTT_STATUS_TOPIC",
                                   "device/ota/status"));
    (void)snprintf(g_heartbeatTopic, sizeof(g_heartbeatTopic), "%s",
                   otaReadPathSetting("OTA_MQTT_HEARTBEAT_TOPIC",
                                   "device/heartbeat"));
    (void)snprintf(g_otaBin, sizeof(g_otaBin), "%s",
                   otaReadPathSetting("OTA_AGENT_BIN", "/usr/bin/ota-agent"));
    (void)snprintf(g_downloadPath, sizeof(g_downloadPath), "%s",
                   otaReadPathSetting("OTA_DOWNLOAD_PATH",
                                   "/var/tmp/ota-download.swu"));
    heartbeat = strtol(otaReadPathSetting("OTA_MQTT_HEARTBEAT_SEC", "30"), NULL,
                       10);
    g_heartbeatSec = heartbeat < 0 ? 0 : (int)heartbeat;
    g_mqttAutoMode = isAutoMqttHost(host) ? 1 : 0;
    g_fixedHost[0] = '\0';
    if (g_mqttAutoMode == 0) {
        (void)snprintf(g_fixedHost, sizeof(g_fixedHost), "%s", host);
    }
    return 0;
}

int main(void)
{
    struct sigaction action;

    loadConfigs();
    if (loadRuntimeSettings() != 0) {
        fprintf(stderr, "mqtt-agent 配置无效\n");
        return 1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = onSignal;
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);

    puts("mqtt-agent foreground mode");
    fprintf(stderr, "mqtt-agent: precheck+async-ota enabled\n");
    ensureSession();
    while (g_stopRequested == 0) {
        processCommand();
        pollOtaOutput();
        maybeHeartbeat();
        if (g_mqttAutoMode != 0) {
            ensureSession();
        }
        (void)otaMqttYield(200);
    }
    return 0;
}
