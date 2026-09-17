/* SPDX-License-Identifier: MIT */
/* HTTP 收发与解析辅助 */

#include "webserver.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

/* 向客户端发送完整缓冲；0 成功，非 0 失败 */
int sendAll(int fd, const void *buf, size_t len)
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
int sendResponse(int fd, int status, const char *reason, const char *ctype,
		 const char *body, size_t bodyLen)
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

int sendJson(int fd, int status, const char *reason, const char *json)
{
	return sendResponse(fd, status, reason, "application/json; charset=utf-8",
			    json, strlen(json));
}

/* 解析无符号长整型；0 成功，非 0 失败 */
int parseULong(const char *s, unsigned long *out)
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
int getQueryValue(const char *query, const char *key, char *out, size_t outSize)
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
int parseQueryTime(const char *query, time_t *fromTs, time_t *toTs)
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

int readWholeFile(const char *path, char **out, size_t *outLen)
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
int parseRequestLine(const char *req, char *method, size_t methodSize,
		     char *path, size_t pathSize, char *query, size_t querySize)
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

/* 不区分大小写查找头字段；找到写入 out，返回 0；未找到 1 */
int getHttpHeader(const char *headers, const char *name, char *out,
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

/* 收齐 HTTP 头；*hdrLen 含末尾 \\r\\n\\r\\n；*total 为缓冲已用 */
int recvHttpHeaders(int fd, char *buf, size_t bufSize, size_t *hdrLen,
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
