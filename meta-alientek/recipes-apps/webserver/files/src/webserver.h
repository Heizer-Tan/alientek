/* SPDX-License-Identifier: MIT */
#ifndef WEBSERVER_H
#define WEBSERVER_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/* sqlite3 仅 samples 模块链接；此处前向声明即可 */
typedef struct sqlite3 sqlite3;

/* 默认路径与容量；实际运行值由环境变量覆盖 */
#define WEBSERVER_DEFAULT_PORT 8080
#define WEBSERVER_DEFAULT_DB_PATH "/var/lib/ap3216c/ap3216c.db"
#define WEBSERVER_DEFAULT_INDEX "/usr/share/webserver/index.html"
#define WEBSERVER_DEFAULT_APPLY "/usr/sbin/board-apply-update"
#define WEBSERVER_DEFAULT_SWU_PATH "/var/tmp/web-upgrade.swu"
#define WEBSERVER_DEFAULT_MIME_PATH "/var/tmp/web-upgrade.mime"
#define WEBSERVER_DEFAULT_LOG_PATH "/var/tmp/web-upgrade.log"
#define WEBSERVER_DEFAULT_REBOOT_CMD "reboot"

#define HDR_BUF_SIZE 16384
#define IO_CHUNK 8192
#define DEFAULT_RANGE_SEC 86400
#define MAX_UPLOAD_BYTES (512UL * 1024UL * 1024UL)

typedef struct WebserverCfg {
	int port;
	const char *dbPath;
	const char *indexPath;
	const char *applyPath;
	const char *swuPath;
	const char *mimePath;
	const char *logPath;
	const char *rebootCmd;
	/* 空或未设置则升级接口开放（实验室默认） */
	const char *upgradeToken;
} WebserverCfg;

void webserverCfgInit(void);
const WebserverCfg *webserverCfg(void);

extern volatile sig_atomic_t gStopRequested;
extern volatile sig_atomic_t gUpgradeBusy;

/* ---------- HTTP 辅助 ---------- */
int sendAll(int fd, const void *buf, size_t len);
int sendResponse(int fd, int status, const char *reason, const char *ctype,
		 const char *body, size_t bodyLen);
int sendJson(int fd, int status, const char *reason, const char *json);
int parseULong(const char *s, unsigned long *out);
int getQueryValue(const char *query, const char *key, char *out,
		  size_t outSize);
int parseQueryTime(const char *query, time_t *fromTs, time_t *toTs);
int parseRequestLine(const char *req, char *method, size_t methodSize,
		     char *path, size_t pathSize, char *query,
		     size_t querySize);
int getHttpHeader(const char *headers, const char *name, char *out,
		  size_t outSize);
int recvHttpHeaders(int fd, char *buf, size_t bufSize, size_t *hdrLen,
		    size_t *total);
int readWholeFile(const char *path, char **out, size_t *outLen);

/* ---------- 传感器采样 ---------- */
int openSamplesDb(sqlite3 **db);
int querySamplesJson(sqlite3 *db, time_t fromTs, time_t toTs, char **outJson,
		     size_t *outLen);
void handleSamplesRequest(int clientFd, const char *query);

/* ---------- 固件升级 ---------- */
void handleUpgrade(int clientFd, const char *headers, const char *pref,
		   size_t prefLen, int sockFd);

#endif /* WEBSERVER_H */
