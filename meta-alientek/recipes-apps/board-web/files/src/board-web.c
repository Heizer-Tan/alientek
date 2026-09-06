/* SPDX-License-Identifier: MIT */
/* board-web：最小 HTTP 服务，查询 AP3216C SQLite 历史 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define BOARD_WEB_PORT 8080
#define BOARD_WEB_DB_PATH "/var/lib/ap3216c/ap3216c.db"
#define BOARD_WEB_INDEX "/usr/share/board-web/index.html"
#define REQ_BUF_SIZE 4096
#define SEND_CHUNK 4096
#define DEFAULT_RANGE_SEC 86400

static volatile sig_atomic_t gStopRequested = 0;

static void handleSignal(int sig)
{
	(void)sig;
	gStopRequested = 1;
}

/* 向客户端发送完整缓冲；0 成功，非 0 失败 */
static int sendAll(int fd, const void *buf, size_t len)
{
	const char *p = (const char *)buf;
	size_t left = len;

	while (left > 0) {
		ssize_t n = send(fd, p, left, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		p += (size_t)n;
		left -= (size_t)n;
	}
	return 0;
}

/* 发送固定状态与正文；0 成功，非 0 失败 */
static int sendResponse(int fd, int status, const char *reason,
			const char *ctype, const char *body, size_t bodyLen)
{
	char hdr[256];
	int n;

	n = snprintf(hdr, sizeof(hdr),
		     "HTTP/1.1 %d %s\r\n"
		     "Content-Type: %s\r\n"
		     "Content-Length: %zu\r\n"
		     "Connection: close\r\n"
		     "\r\n",
		     status, reason, ctype, bodyLen);
	if (n < 0 || (size_t)n >= sizeof(hdr))
		return -1;
	if (sendAll(fd, hdr, (size_t)n) != 0)
		return -1;
	if (bodyLen > 0 && body && sendAll(fd, body, bodyLen) != 0)
		return -1;
	return 0;
}

/* 解析无符号长整型；0 成功，非 0 失败 */
static int parseULong(const char *s, unsigned long *out)
{
	char *end = NULL;
	unsigned long v;

	if (!s || !out || !*s)
		return -1;
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0')
		return -1;
	*out = v;
	return 0;
}

/* 从 query 取键值（写入 out，最多 outSize-1）；找到返回 0，未找到返回 1，错误 -1 */
static int getQueryValue(const char *query, const char *key, char *out, size_t outSize)
{
	size_t keyLen;
	const char *p;

	if (!query || !key || !out || outSize == 0)
		return -1;
	keyLen = strlen(key);
	p = query;
	while (*p) {
		const char *eq;
		const char *amp;
		size_t nameLen;
		size_t valLen;

		eq = strchr(p, '=');
		amp = strchr(p, '&');
		if (!eq || (amp && eq > amp))
			return -1;
		nameLen = (size_t)(eq - p);
		if (nameLen == keyLen && strncmp(p, key, keyLen) == 0) {
			valLen = amp ? (size_t)(amp - eq - 1) : strlen(eq + 1);
			if (valLen >= outSize)
				return -1;
			memcpy(out, eq + 1, valLen);
			out[valLen] = '\0';
			return 0;
		}
		if (!amp)
			break;
		p = amp + 1;
	}
	return 1;
}

/* 解析 from/to；缺省最近 24h；0 成功，非 0 非法 */
static int parseQueryTime(const char *query, time_t *fromTs, time_t *toTs)
{
	char fromBuf[32];
	char toBuf[32];
	unsigned long fromVal = 0;
	unsigned long toVal = 0;
	time_t now;
	int rcFrom;
	int rcTo;

	if (!fromTs || !toTs)
		return -1;
	now = time(NULL);
	if (now == (time_t)-1)
		return -1;
	*toTs = now;
	*fromTs = now - DEFAULT_RANGE_SEC;
	if (!query || !*query)
		return 0;

	rcFrom = getQueryValue(query, "from", fromBuf, sizeof(fromBuf));
	rcTo = getQueryValue(query, "to", toBuf, sizeof(toBuf));
	if (rcFrom < 0 || rcTo < 0)
		return -1;
	if (rcFrom == 0) {
		if (parseULong(fromBuf, &fromVal) != 0)
			return -1;
		*fromTs = (time_t)fromVal;
	}
	if (rcTo == 0) {
		if (parseULong(toBuf, &toVal) != 0)
			return -1;
		*toTs = (time_t)toVal;
	}
	if (*fromTs > *toTs)
		return -1;
	return 0;
}

/* 动态字符串追加；0 成功，非 0 失败 */
static int appendStr(char **buf, size_t *len, size_t *cap, const char *s)
{
	size_t add;
	char *nbuf;

	if (!buf || !len || !cap || !s)
		return -1;
	add = strlen(s);
	if (*len + add + 1 > *cap) {
		size_t ncap = (*cap == 0) ? 256 : *cap;

		while (ncap < *len + add + 1)
			ncap *= 2;
		nbuf = realloc(*buf, ncap);
		if (!nbuf)
			return -1;
		*buf = nbuf;
		*cap = ncap;
	}
	memcpy(*buf + *len, s, add + 1);
	*len += add;
	return 0;
}

/* 查询 samples 生成 JSON 数组；调用方 free(*outJson)；0 成功，非 0 失败 */
static int querySamplesJson(sqlite3 *db, time_t fromTs, time_t toTs,
			    char **outJson, size_t *outLen)
{
	sqlite3_stmt *stmt = NULL;
	char *json = NULL;
	size_t len = 0;
	size_t cap = 0;
	int rc;
	int first = 1;
	char item[128];

	if (!db || !outJson || !outLen)
		return -1;
	*outJson = NULL;
	*outLen = 0;
	rc = sqlite3_prepare_v2(db,
			       "SELECT ts, ir, als, ps FROM samples"
			       " WHERE ts >= ? AND ts <= ? ORDER BY ts ASC;",
			       -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)fromTs);
	sqlite3_bind_int64(stmt, 2, (sqlite3_int64)toTs);
	if (appendStr(&json, &len, &cap, "[") != 0) {
		sqlite3_finalize(stmt);
		free(json);
		return -1;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (!first && appendStr(&json, &len, &cap, ",") != 0) {
			sqlite3_finalize(stmt);
			free(json);
			return -1;
		}
		first = 0;
		snprintf(item, sizeof(item),
			 "{\"ts\":%lld,\"ir\":%d,\"als\":%d,\"ps\":%d}",
			 (long long)sqlite3_column_int64(stmt, 0),
			 sqlite3_column_int(stmt, 1),
			 sqlite3_column_int(stmt, 2),
			 sqlite3_column_int(stmt, 3));
		if (appendStr(&json, &len, &cap, item) != 0) {
			sqlite3_finalize(stmt);
			free(json);
			return -1;
		}
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		free(json);
		return -1;
	}
	if (appendStr(&json, &len, &cap, "]") != 0) {
		free(json);
		return -1;
	}
	*outJson = json;
	*outLen = len;
	return 0;
}

/* 读取整文件到堆缓冲；调用方 free；0 成功，非 0 失败 */
static int readWholeFile(const char *path, char **out, size_t *outLen)
{
	FILE *fp;
	struct stat st;
	char *buf;
	size_t n;

	if (!path || !out || !outLen)
		return -1;
	*out = NULL;
	*outLen = 0;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0)
		return -1;
	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	buf = malloc((size_t)st.st_size + 1);
	if (!buf) {
		fclose(fp);
		return -1;
	}
	n = fread(buf, 1, (size_t)st.st_size, fp);
	fclose(fp);
	if (n != (size_t)st.st_size) {
		free(buf);
		return -1;
	}
	buf[n] = '\0';
	*out = buf;
	*outLen = n;
	return 0;
}

/* 从请求行提取 path 与 query；0 成功，非 0 失败 */
static int parseRequestLine(const char *req, char *path, size_t pathSize,
			    char *query, size_t querySize)
{
	const char *sp1;
	const char *sp2;
	const char *qmark;
	size_t pathLen;
	size_t queryLen;

	if (!req || !path || !query || pathSize == 0 || querySize == 0)
		return -1;
	path[0] = '\0';
	query[0] = '\0';
	if (strncmp(req, "GET ", 4) != 0)
		return -1;
	sp1 = req + 4;
	sp2 = strchr(sp1, ' ');
	if (!sp2 || sp2 == sp1)
		return -1;
	qmark = memchr(sp1, '?', (size_t)(sp2 - sp1));
	if (qmark) {
		pathLen = (size_t)(qmark - sp1);
		queryLen = (size_t)(sp2 - qmark - 1);
	} else {
		pathLen = (size_t)(sp2 - sp1);
		queryLen = 0;
	}
	if (pathLen == 0 || pathLen >= pathSize || queryLen >= querySize)
		return -1;
	memcpy(path, sp1, pathLen);
	path[pathLen] = '\0';
	if (queryLen > 0)
		memcpy(query, qmark + 1, queryLen);
	query[queryLen] = '\0';
	return 0;
}

/* 只读打开采样库；0 成功，非 0 失败 */
static int openSamplesDb(sqlite3 **db)
{
	if (!db)
		return -1;
	*db = NULL;
	if (sqlite3_open_v2(BOARD_WEB_DB_PATH, db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		if (*db) {
			syslog(LOG_ERR, "sqlite3_open: %s", sqlite3_errmsg(*db));
			sqlite3_close(*db);
			*db = NULL;
		} else {
			syslog(LOG_ERR, "sqlite3_open failed");
		}
		return -1;
	}
	return 0;
}

/* 处理单个客户端连接 */
static void handleClient(int clientFd)
{
	char req[REQ_BUF_SIZE];
	char path[256];
	char query[256];
	ssize_t n;
	char *body = NULL;
	size_t bodyLen = 0;
	time_t fromTs = 0;
	time_t toTs = 0;
	sqlite3 *db = NULL;
	const char *bad = "bad request";
	const char *err500 = "internal error";
	const char *notFound = "not found";

	n = recv(clientFd, req, sizeof(req) - 1, 0);
	if (n <= 0)
		return;
	req[n] = '\0';
	if (parseRequestLine(req, path, sizeof(path), query, sizeof(query)) != 0) {
		sendResponse(clientFd, 400, "Bad Request", "text/plain",
			     bad, strlen(bad));
		return;
	}
	if (strcmp(path, "/") == 0) {
		if (readWholeFile(BOARD_WEB_INDEX, &body, &bodyLen) != 0) {
			sendResponse(clientFd, 500, "Internal Server Error",
				     "text/plain", err500, strlen(err500));
			return;
		}
		sendResponse(clientFd, 200, "OK", "text/html; charset=utf-8",
			     body, bodyLen);
		free(body);
		return;
	}
	if (strcmp(path, "/api/samples") == 0) {
		if (parseQueryTime(query, &fromTs, &toTs) != 0) {
			sendResponse(clientFd, 400, "Bad Request", "text/plain",
				     bad, strlen(bad));
			return;
		}
		if (openSamplesDb(&db) != 0) {
			sendResponse(clientFd, 500, "Internal Server Error",
				     "text/plain", err500, strlen(err500));
			return;
		}
		if (querySamplesJson(db, fromTs, toTs, &body, &bodyLen) != 0) {
			sqlite3_close(db);
			sendResponse(clientFd, 500, "Internal Server Error",
				     "text/plain", err500, strlen(err500));
			return;
		}
		sqlite3_close(db);
		sendResponse(clientFd, 200, "OK", "application/json", body, bodyLen);
		free(body);
		return;
	}
	sendResponse(clientFd, 404, "Not Found", "text/plain",
		     notFound, strlen(notFound));
}

/* 创建并绑定监听套接字；失败返回 -1 */
static int createListenSocket(void)
{
	int fd;
	int on = 1;
	struct sockaddr_in addr;

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
	addr.sin_port = htons(BOARD_WEB_PORT);
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

	openlog("board-web", LOG_PID, LOG_DAEMON);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	listenFd = createListenSocket();
	if (listenFd < 0) {
		closelog();
		return 1;
	}
	syslog(LOG_INFO, "listening on 0.0.0.0:%d", BOARD_WEB_PORT);

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
