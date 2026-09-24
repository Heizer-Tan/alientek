/* SPDX-License-Identifier: MIT */
/* webserver：传感器历史查询 + Web 固件升级（调用 board-apply-update） */

#include "webserver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

volatile sig_atomic_t gStopRequested = 0;
volatile sig_atomic_t gUpgradeBusy = 0;

static WebserverCfg gCfg;

/* 环境变量非空则用之，否则回退默认值 */
static const char *envOrDefault(const char *name, const char *fallback)
{
	const char *v = getenv(name);

	if (v && *v)
		return v;
	return fallback;
}

void webserverCfgInit(void)
{
	const char *portStr;
	unsigned long portVal = 0;

	gCfg.port = WEBSERVER_DEFAULT_PORT;
	portStr = getenv("WEBSERVER_PORT");
	if (portStr && *portStr && parseULong(portStr, &portVal) == 0 &&
	    portVal > 0 && portVal <= 65535)
		gCfg.port = (int)portVal;

	gCfg.dbPath = envOrDefault("WEBSERVER_DB_PATH",
				   WEBSERVER_DEFAULT_DB_PATH);
	gCfg.indexPath = envOrDefault("WEBSERVER_INDEX",
				     WEBSERVER_DEFAULT_INDEX);
	gCfg.icmDbPath = envOrDefault("WEBSERVER_ICM_DB_PATH",
				     WEBSERVER_DEFAULT_ICM_DB_PATH);
	gCfg.icmIndexPath = envOrDefault("WEBSERVER_ICM_INDEX",
					WEBSERVER_DEFAULT_ICM_INDEX);
	gCfg.applyPath = envOrDefault("WEBSERVER_APPLY",
				     WEBSERVER_DEFAULT_APPLY);
	gCfg.swuPath = envOrDefault("WEBSERVER_SWU_PATH",
				    WEBSERVER_DEFAULT_SWU_PATH);
	gCfg.mimePath = envOrDefault("WEBSERVER_MIME_PATH",
				     WEBSERVER_DEFAULT_MIME_PATH);
	gCfg.logPath = envOrDefault("WEBSERVER_LOG_PATH",
				    WEBSERVER_DEFAULT_LOG_PATH);
	gCfg.rebootCmd = envOrDefault("WEBSERVER_REBOOT_CMD",
				      WEBSERVER_DEFAULT_REBOOT_CMD);
	/* 允许空串：显式清空即开放升级口 */
	{
		const char *tok = getenv("WEBSERVER_UPGRADE_TOKEN");

		gCfg.upgradeToken = tok ? tok : "";
	}
}

const WebserverCfg *webserverCfg(void)
{
	return &gCfg;
}

static void handleSignal(int sig)
{
	(void)sig;
	gStopRequested = 1;
}

static void handleClient(int clientFd)
{
	char buf[HDR_BUF_SIZE];
	char method[16];
	char path[256];
	char query[256];
	size_t hdrLen = 0;
	size_t total = 0;
	char *body = NULL;
	size_t bodyLen = 0;

	if (recvHttpHeaders(clientFd, buf, sizeof(buf), &hdrLen, &total) != 0) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"请求头无效\"}");
		return;
	}
	if (parseRequestLine(buf, method, sizeof(method), path, sizeof(path),
			     query, sizeof(query)) != 0) {
		sendResponse(clientFd, 400, "Bad Request", "text/plain",
			     "bad request", 11);
		return;
	}
	if (strcmp(method, "POST") == 0 && strcmp(path, "/api/upgrade") == 0) {
		handleUpgrade(clientFd, buf, buf + hdrLen, total - hdrLen,
			      clientFd);
		return;
	}
	if (strcmp(method, "GET") != 0) {
		sendResponse(clientFd, 405, "Method Not Allowed", "text/plain",
			     "method not allowed", 18);
		return;
	}
	if (strcmp(path, "/") == 0) {
		if (readWholeFile(webserverCfg()->indexPath, &body, &bodyLen) !=
		    0) {
			sendResponse(clientFd, 500, "Internal Server Error",
				     "text/plain", "internal error", 14);
			return;
		}
		sendResponse(clientFd, 200, "OK", "text/html; charset=utf-8",
			     body, bodyLen);
		free(body);
		return;
	}
	if (strcmp(path, "/api/samples") == 0) {
		handleSamplesRequest(clientFd, query);
		return;
	}
	if (strcmp(path, "/icm20608") == 0) {
		handleIcmPageRequest(clientFd);
		return;
	}
	if (strcmp(path, "/api/icm20608/samples") == 0) {
		handleIcmSamplesRequest(clientFd, query);
		return;
	}
	sendResponse(clientFd, 404, "Not Found", "text/plain", "not found", 9);
}

static int createListenSocket(void)
{
	int fd;
	int on = 1;
	struct sockaddr_in addr;
	int port = webserverCfg()->port;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		return -1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
		syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
		close(fd);
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, 8) != 0) {
		syslog(LOG_ERR, "listen: %s", strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

int main(void)
{
	struct sigaction sa;
	int listenFd;
	int clientFd;

	webserverCfgInit();
	openlog("webserver", LOG_PID, LOG_DAEMON);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	listenFd = createListenSocket();
	if (listenFd < 0) {
		closelog();
		return 1;
	}
	syslog(LOG_INFO, "listening on 0.0.0.0:%d", webserverCfg()->port);

	while (!gStopRequested) {
		clientFd = accept(listenFd, NULL, NULL);
		if (clientFd < 0) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "accept: %s", strerror(errno));
			break;
		}
		handleClient(clientFd);
		close(clientFd);
	}

	close(listenFd);
	syslog(LOG_INFO, "stopped");
	closelog();
	return 0;
}
