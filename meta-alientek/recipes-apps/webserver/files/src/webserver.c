/* SPDX-License-Identifier: MIT */
/* webserver：传感器历史查询 + Web 固件升级（调用 board-apply-update） */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define WEBSERVER_PORT 8080
#define WEBSERVER_DB_PATH "/var/lib/ap3216c/ap3216c.db"
#define WEBSERVER_INDEX "/usr/share/webserver/index.html"
#define UPGRADE_SWU_PATH "/var/tmp/web-upgrade.swu"
#define UPGRADE_MIME_PATH "/var/tmp/web-upgrade.mime"
#define UPGRADE_LOG_PATH "/var/tmp/web-upgrade.log"
#define BOARD_APPLY_UPDATE "/usr/sbin/board-apply-update"
#define HDR_BUF_SIZE 16384
#define IO_CHUNK 8192
#define DEFAULT_RANGE_SEC 86400
#define MAX_UPLOAD_BYTES (512UL * 1024UL * 1024UL)

static volatile sig_atomic_t gStopRequested = 0;
static volatile sig_atomic_t gUpgradeBusy = 0;

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

/* 发送 JSON 短响应 */
static int sendJson(int fd, int status, const char *reason, const char *json)
{
	return sendResponse(fd, status, reason, "application/json; charset=utf-8",
			    json, strlen(json));
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

/* 从 query 取键值；找到 0，未找到 1，错误 -1 */
static int getQueryValue(const char *query, const char *key, char *out,
			 size_t outSize)
{
	size_t keyLen;
	const char *p;

	if (!query || !key || !out || outSize == 0)
		return -1;
	keyLen = strlen(key);
	p = query;
	while (*p) {
		const char *eq = strchr(p, '=');
		const char *amp = strchr(p, '&');
		size_t nameLen;
		size_t valLen;

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

/* 解析 from/to；缺省最近 24h */
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

/* 解析请求行：支持 GET/POST；0 成功 */
static int parseRequestLine(const char *req, char *method, size_t methodSize,
			    char *path, size_t pathSize, char *query,
			    size_t querySize)
{
	const char *sp1;
	const char *sp2;
	const char *qmark;
	size_t methodLen;
	size_t pathLen;
	size_t queryLen;

	if (!req || !method || !path || !query || methodSize == 0 ||
	    pathSize == 0 || querySize == 0)
		return -1;
	method[0] = path[0] = query[0] = '\0';
	sp1 = strchr(req, ' ');
	if (!sp1 || sp1 == req)
		return -1;
	methodLen = (size_t)(sp1 - req);
	if (methodLen == 0 || methodLen >= methodSize)
		return -1;
	memcpy(method, req, methodLen);
	method[methodLen] = '\0';
	sp1++;
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

/* 不区分大小写查找头字段；找到写入 out，返回 0 */
static int getHttpHeader(const char *headers, const char *name, char *out,
			 size_t outSize)
{
	size_t nameLen;
	const char *p = headers;

	if (!headers || !name || !out || outSize == 0)
		return -1;
	nameLen = strlen(name);
	while (*p && !(p[0] == '\r' && p[1] == '\n' && p[2] == '\r' &&
		       p[3] == '\n')) {
		const char *eol = strstr(p, "\r\n");
		size_t lineLen;
		const char *colon;

		if (!eol)
			return -1;
		lineLen = (size_t)(eol - p);
		colon = memchr(p, ':', lineLen);
		if (colon && (size_t)(colon - p) == nameLen) {
			size_t i;
			int match = 1;

			for (i = 0; i < nameLen; i++) {
				if (tolower((unsigned char)p[i]) !=
				    tolower((unsigned char)name[i])) {
					match = 0;
					break;
				}
			}
			if (match) {
				const char *val = colon + 1;
				size_t valLen;

				while (val < eol &&
				       (*val == ' ' || *val == '\t'))
					val++;
				valLen = (size_t)(eol - val);
				if (valLen >= outSize)
					return -1;
				memcpy(out, val, valLen);
				out[valLen] = '\0';
				return 0;
			}
		}
		p = eol + 2;
	}
	return 1;
}

/* 从 Content-Type 取 boundary；0 成功 */
static int parseMultipartBoundary(const char *contentType, char *boundary,
				  size_t boundarySize)
{
	const char *p;
	size_t len;

	if (!contentType || !boundary || boundarySize < 3)
		return -1;
	p = strstr(contentType, "boundary=");
	if (!p)
		return -1;
	p += 9;
	if (*p == '"') {
		const char *end;

		p++;
		end = strchr(p, '"');
		if (!end)
			return -1;
		len = (size_t)(end - p);
	} else {
		len = strcspn(p, " \t\r\n;");
	}
	if (len == 0 || len + 3 >= boundarySize)
		return -1;
	boundary[0] = '-';
	boundary[1] = '-';
	memcpy(boundary + 2, p, len);
	boundary[len + 2] = '\0';
	return 0;
}

/* 收齐 HTTP 头；*hdrLen 含末尾 \\r\\n\\r\\n；*total 为缓冲已用 */
static int recvHttpHeaders(int fd, char *buf, size_t bufSize, size_t *hdrLen,
			   size_t *total)
{
	size_t have = 0;

	if (!buf || !hdrLen || !total || bufSize < 4)
		return -1;
	*hdrLen = 0;
	*total = 0;
	while (have + 1 < bufSize) {
		ssize_t n;
		char *end;

		n = recv(fd, buf + have, bufSize - have - 1, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		have += (size_t)n;
		buf[have] = '\0';
		end = strstr(buf, "\r\n\r\n");
		if (end) {
			*hdrLen = (size_t)(end - buf) + 4;
			*total = have;
			return 0;
		}
	}
	return -1;
}

/* 将请求体（已知长度）写入文件；pref 为头后已读前缀 */
static int recvBodyToFile(int fd, FILE *out, const char *pref, size_t prefLen,
			  unsigned long bodyLen)
{
	unsigned long got = 0;
	char chunk[IO_CHUNK];

	if (!out)
		return -1;
	if (prefLen > 0) {
		if (prefLen > bodyLen)
			return -1;
		if (fwrite(pref, 1, prefLen, out) != prefLen)
			return -1;
		got = (unsigned long)prefLen;
	}
	while (got < bodyLen) {
		size_t want = (size_t)(bodyLen - got);
		ssize_t n;

		if (want > sizeof(chunk))
			want = sizeof(chunk);
		n = recv(fd, chunk, want, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		if (fwrite(chunk, 1, (size_t)n, out) != (size_t)n)
			return -1;
		got += (unsigned long)n;
	}
	return 0;
}

/* 从 multipart 临时文件抽出 name=\"swu\"（整文件扫描，上限见 MAX_UPLOAD_BYTES） */
static int extractSwuFromMime(const char *mimePath, const char *boundary,
			      const char *outPath)
{
	char *data = NULL;
	size_t dataLen = 0;
	size_t boundLen;
	const char *cursor;
	const char *end;
	FILE *out;

	if (readWholeFile(mimePath, &data, &dataLen) != 0)
		return -1;
	boundLen = strlen(boundary);
	cursor = data;
	end = data + dataLen;
	while (cursor + boundLen < end) {
		const char *part;
		const char *hdrEnd;
		const char *nextBound;
		const char *body;
		size_t bodyLen;

		part = memmem(cursor, (size_t)(end - cursor), boundary, boundLen);
		if (!part)
			break;
		part += boundLen;
		if (part + 2 <= end && part[0] == '-' && part[1] == '-')
			break;
		if (part + 2 <= end && part[0] == '\r' && part[1] == '\n')
			part += 2;
		hdrEnd = memmem(part, (size_t)(end - part), "\r\n\r\n", 4);
		if (!hdrEnd)
			break;
		body = hdrEnd + 4;
		nextBound = memmem(body, (size_t)(end - body), boundary, boundLen);
		if (!nextBound)
			break;
		bodyLen = (size_t)(nextBound - body);
		if (bodyLen >= 2 && body[bodyLen - 2] == '\r' &&
		    body[bodyLen - 1] == '\n')
			bodyLen -= 2;
		if (memmem(part, (size_t)(hdrEnd - part), "name=\"swu\"", 10)) {
			out = fopen(outPath, "wb");
			if (!out) {
				free(data);
				return -1;
			}
			if (bodyLen > 0 &&
			    fwrite(body, 1, bodyLen, out) != bodyLen) {
				fclose(out);
				free(data);
				return -1;
			}
			fclose(out);
			free(data);
			return bodyLen > 0 ? 0 : -1;
		}
		cursor = nextBound;
	}
	free(data);
	return -1;
}

/* 把日志文件尾部拷入 errOut，便于页面展示真实失败原因 */
static void loadLogTail(const char *path, char *errOut, size_t errSize)
{
	FILE *fp;
	long size;
	size_t want;
	size_t n;

	if (!path || !errOut || errSize < 8)
		return;
	errOut[0] = '\0';
	fp = fopen(path, "rb");
	if (!fp)
		return;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return;
	}
	size = ftell(fp);
	if (size < 0) {
		fclose(fp);
		return;
	}
	want = errSize - 1;
	if ((unsigned long)size > want) {
		if (fseek(fp, size - (long)want, SEEK_SET) != 0) {
			fclose(fp);
			return;
		}
	} else if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return;
	}
	n = fread(errOut, 1, want, fp);
	fclose(fp);
	errOut[n] = '\0';
}

/* 执行 board-apply-update；成功 0，失败把日志尾部写入 errOut */
static int runBoardApplyUpdate(const char *swuPath, char *errOut, size_t errSize)
{
	pid_t pid;
	int status;
	int logFd;

	if (!swuPath || !errOut || errSize == 0)
		return -1;
	errOut[0] = '\0';
	logFd = open(UPGRADE_LOG_PATH, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
		     0644);
	if (logFd < 0) {
		snprintf(errOut, errSize, "无法创建升级日志: %s", strerror(errno));
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		close(logFd);
		snprintf(errOut, errSize, "fork 失败: %s", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		dup2(logFd, STDOUT_FILENO);
		dup2(logFd, STDERR_FILENO);
		if (logFd > STDERR_FILENO)
			close(logFd);
		setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
		if (chdir("/tmp") != 0)
			dprintf(STDERR_FILENO, "chdir /tmp 失败: %s\n",
				strerror(errno));
		execl(BOARD_APPLY_UPDATE, "board-apply-update", swuPath,
		      (char *)NULL);
		dprintf(STDERR_FILENO, "无法执行 %s: %s\n", BOARD_APPLY_UPDATE,
			strerror(errno));
		_exit(127);
	}
	close(logFd);
	if (waitpid(pid, &status, 0) < 0) {
		snprintf(errOut, errSize, "waitpid 失败: %s", strerror(errno));
		return -1;
	}
	loadLogTail(UPGRADE_LOG_PATH, errOut, errSize);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	if (errOut[0] == '\0') {
		if (WIFEXITED(status))
			snprintf(errOut, errSize,
				 "board-apply-update 退出码 %d",
				 WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			snprintf(errOut, errSize,
				 "board-apply-update 被信号 %d 终止",
				 WTERMSIG(status));
		else
			snprintf(errOut, errSize, "board-apply-update 失败");
	}
	return -1;
}

/* 转义 JSON 字符串到 out；0 成功 */
static int jsonEscape(const char *in, char *out, size_t outSize)
{
	size_t oi = 0;

	if (!in || !out || outSize < 3)
		return -1;
	out[oi++] = '"';
	while (*in) {
		char c = *in++;
		const char *esc = NULL;

		if (c == '"' || c == '\\') {
			if (oi + 2 >= outSize)
				return -1;
			out[oi++] = '\\';
			out[oi++] = c;
			continue;
		}
		if (c == '\n')
			esc = "\\n";
		else if (c == '\r')
			esc = "\\r";
		else if (c == '\t')
			esc = "\\t";
		else if ((unsigned char)c < 0x20)
			continue;
		if (esc) {
			size_t el = strlen(esc);

			if (oi + el >= outSize)
				return -1;
			memcpy(out + oi, esc, el);
			oi += el;
		} else {
			if (oi + 1 >= outSize)
				return -1;
			out[oi++] = c;
		}
	}
	if (oi + 1 >= outSize)
		return -1;
	out[oi++] = '"';
	out[oi] = '\0';
	return 0;
}

static void handleUpgrade(int clientFd, const char *headers, const char *pref,
			  size_t prefLen, int sockFd)
{
	char ctype[256];
	char clenBuf[64];
	char boundary[128];
	char errBuf[4096];
	char msgEsc[4600];
	char json[5120];
	unsigned long bodyLen = 0;
	FILE *mime = NULL;
	struct stat st;

	if (gUpgradeBusy) {
		sendJson(clientFd, 409, "Conflict",
			 "{\"ok\":false,\"message\":\"升级进行中，请稍候\"}");
		return;
	}
	if (getHttpHeader(headers, "Content-Type", ctype, sizeof(ctype)) != 0 ||
	    getHttpHeader(headers, "Content-Length", clenBuf,
			  sizeof(clenBuf)) != 0 ||
	    parseULong(clenBuf, &bodyLen) != 0) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"缺少 Content-Type/Length\"}");
		return;
	}
	if (bodyLen == 0 || bodyLen > MAX_UPLOAD_BYTES) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"上传大小非法\"}");
		return;
	}
	if (parseMultipartBoundary(ctype, boundary, sizeof(boundary)) != 0) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"需要 multipart/form-data\"}");
		return;
	}
	gUpgradeBusy = 1;
	mime = fopen(UPGRADE_MIME_PATH, "wb");
	if (!mime) {
		gUpgradeBusy = 0;
		sendJson(clientFd, 500, "Internal Server Error",
			 "{\"ok\":false,\"message\":\"无法创建临时文件\"}");
		return;
	}
	if (recvBodyToFile(sockFd, mime, pref, prefLen, bodyLen) != 0) {
		fclose(mime);
		unlink(UPGRADE_MIME_PATH);
		gUpgradeBusy = 0;
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"接收上传失败\"}");
		return;
	}
	fclose(mime);
	unlink(UPGRADE_SWU_PATH);
	if (extractSwuFromMime(UPGRADE_MIME_PATH, boundary, UPGRADE_SWU_PATH) !=
	    0) {
		unlink(UPGRADE_MIME_PATH);
		gUpgradeBusy = 0;
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"未找到 swu 文件字段\"}");
		return;
	}
	unlink(UPGRADE_MIME_PATH);
	if (stat(UPGRADE_SWU_PATH, &st) != 0 || st.st_size <= 0) {
		gUpgradeBusy = 0;
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"升级包为空\"}");
		return;
	}
	if (runBoardApplyUpdate(UPGRADE_SWU_PATH, errBuf, sizeof(errBuf)) != 0) {
		syslog(LOG_ERR, "web upgrade apply failed: %s",
		       errBuf[0] ? errBuf : "(no detail)");
		if (jsonEscape(errBuf[0] ? errBuf : "board-apply-update 失败",
			       msgEsc, sizeof(msgEsc)) != 0)
			snprintf(msgEsc, sizeof(msgEsc), "\"升级失败\"");
		snprintf(json, sizeof(json),
			 "{\"ok\":false,\"message\":%s}", msgEsc);
		gUpgradeBusy = 0;
		sendJson(clientFd, 500, "Internal Server Error", json);
		return;
	}
	sendJson(clientFd, 200, "OK",
		 "{\"ok\":true,\"message\":\"升级完成，设备即将重启\"}");
	sync();
	syslog(LOG_NOTICE, "web upgrade ok, rebooting");
	if (system("reboot") != 0)
		syslog(LOG_ERR, "reboot failed: %s", strerror(errno));
	gUpgradeBusy = 0;
}

static int openSamplesDb(sqlite3 **db)
{
	if (!db)
		return -1;
	*db = NULL;
	if (sqlite3_open_v2(WEBSERVER_DB_PATH, db, SQLITE_OPEN_READONLY,
			    NULL) != SQLITE_OK) {
		if (*db) {
			syslog(LOG_ERR, "sqlite3_open: %s", sqlite3_errmsg(*db));
			sqlite3_close(*db);
			*db = NULL;
		}
		return -1;
	}
	return 0;
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
	time_t fromTs = 0;
	time_t toTs = 0;
	sqlite3 *db = NULL;

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
		if (readWholeFile(WEBSERVER_INDEX, &body, &bodyLen) != 0) {
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
		if (parseQueryTime(query, &fromTs, &toTs) != 0) {
			sendResponse(clientFd, 400, "Bad Request", "text/plain",
				     "bad request", 11);
			return;
		}
		if (openSamplesDb(&db) != 0) {
			sendResponse(clientFd, 500, "Internal Server Error",
				     "text/plain", "internal error", 14);
			return;
		}
		if (querySamplesJson(db, fromTs, toTs, &body, &bodyLen) != 0) {
			sqlite3_close(db);
			sendResponse(clientFd, 500, "Internal Server Error",
				     "text/plain", "internal error", 14);
			return;
		}
		sqlite3_close(db);
		sendResponse(clientFd, 200, "OK", "application/json", body,
			     bodyLen);
		free(body);
		return;
	}
	sendResponse(clientFd, 404, "Not Found", "text/plain", "not found", 9);
}

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
	addr.sin_port = htons(WEBSERVER_PORT);
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
	syslog(LOG_INFO, "listening on 0.0.0.0:%d", WEBSERVER_PORT);

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
