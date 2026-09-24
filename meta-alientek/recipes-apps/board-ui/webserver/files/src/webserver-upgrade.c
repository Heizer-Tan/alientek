/* SPDX-License-Identifier: MIT */
/* Web 固件升级：multipart 接收 + board-apply-update + token 鉴权 */

#include "webserver.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

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

/* 从 multipart 临时文件抽出 name=\"swu\" */
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
	const WebserverCfg *cfg = webserverCfg();

	if (!swuPath || !errOut || errSize == 0)
		return -1;
	errOut[0] = '\0';
	logFd = open(cfg->logPath, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
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
		execl(cfg->applyPath, "board-apply-update", swuPath, (char *)NULL);
		dprintf(STDERR_FILENO, "无法执行 %s: %s\n", cfg->applyPath,
			strerror(errno));
		_exit(127);
	}
	close(logFd);
	if (waitpid(pid, &status, 0) < 0) {
		snprintf(errOut, errSize, "waitpid 失败: %s", strerror(errno));
		return -1;
	}
	loadLogTail(cfg->logPath, errOut, errSize);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	if (errOut[0] == '\0') {
		if (WIFEXITED(status))
			snprintf(errOut, errSize, "board-apply-update 退出码 %d",
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

/* token 未配置则放行；否则要求 X-Upgrade-Token 或 Bearer 精确匹配 */
static int checkUpgradeAuth(const char *headers)
{
	const char *token = webserverCfg()->upgradeToken;
	char hdr[512];

	if (!token || !*token)
		return 0;
	if (getHttpHeader(headers, "X-Upgrade-Token", hdr, sizeof(hdr)) == 0 &&
	    strcmp(hdr, token) == 0)
		return 0;
	if (getHttpHeader(headers, "Authorization", hdr, sizeof(hdr)) == 0 &&
	    strncmp(hdr, "Bearer ", 7) == 0 && strcmp(hdr + 7, token) == 0)
		return 0;
	return -1;
}

static int parseUpgradeHeaders(const char *headers, char *ctype, size_t ctypeSize,
			       char *boundary, size_t boundarySize,
			       unsigned long *bodyLen)
{
	char clenBuf[64];

	if (getHttpHeader(headers, "Content-Type", ctype, ctypeSize) != 0 ||
	    getHttpHeader(headers, "Content-Length", clenBuf,
			  sizeof(clenBuf)) != 0 ||
	    parseULong(clenBuf, bodyLen) != 0)
		return -1;
	if (*bodyLen == 0 || *bodyLen > MAX_UPLOAD_BYTES)
		return -2;
	if (parseMultipartBoundary(ctype, boundary, boundarySize) != 0)
		return -3;
	return 0;
}

void handleUpgrade(int clientFd, const char *headers, const char *pref,
		   size_t prefLen, int sockFd)
{
	char ctype[256];
	char boundary[128];
	char errBuf[4096];
	char msgEsc[4600];
	char json[5120];
	unsigned long bodyLen = 0;
	FILE *mime = NULL;
	struct stat st;
	const WebserverCfg *cfg = webserverCfg();
	int hdrRc;

	if (checkUpgradeAuth(headers) != 0) {
		sendJson(clientFd, 401, "Unauthorized",
			 "{\"ok\":false,\"message\":\"未授权\"}");
		return;
	}
	if (gUpgradeBusy) {
		sendJson(clientFd, 409, "Conflict",
			 "{\"ok\":false,\"message\":\"升级进行中，请稍候\"}");
		return;
	}
	hdrRc = parseUpgradeHeaders(headers, ctype, sizeof(ctype), boundary,
				    sizeof(boundary), &bodyLen);
	if (hdrRc == -1) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"缺少 Content-Type/Length\"}");
		return;
	}
	if (hdrRc == -2) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"上传大小非法\"}");
		return;
	}
	if (hdrRc == -3) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"需要 multipart/form-data\"}");
		return;
	}
	gUpgradeBusy = 1;
	mime = fopen(cfg->mimePath, "wb");
	if (!mime) {
		gUpgradeBusy = 0;
		sendJson(clientFd, 500, "Internal Server Error",
			 "{\"ok\":false,\"message\":\"无法创建临时文件\"}");
		return;
	}
	if (recvBodyToFile(sockFd, mime, pref, prefLen, bodyLen) != 0) {
		fclose(mime);
		unlink(cfg->mimePath);
		gUpgradeBusy = 0;
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"接收上传失败\"}");
		return;
	}
	fclose(mime);
	unlink(cfg->swuPath);
	if (extractSwuFromMime(cfg->mimePath, boundary, cfg->swuPath) != 0) {
		unlink(cfg->mimePath);
		gUpgradeBusy = 0;
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"未找到 swu 文件字段\"}");
		return;
	}
	unlink(cfg->mimePath);
	if (stat(cfg->swuPath, &st) != 0 || st.st_size <= 0) {
		gUpgradeBusy = 0;
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"升级包为空\"}");
		return;
	}
	if (runBoardApplyUpdate(cfg->swuPath, errBuf, sizeof(errBuf)) != 0) {
		syslog(LOG_ERR, "web upgrade apply failed: %s",
		       errBuf[0] ? errBuf : "(no detail)");
		if (jsonEscape(errBuf[0] ? errBuf : "board-apply-update 失败",
			       msgEsc, sizeof(msgEsc)) != 0)
			snprintf(msgEsc, sizeof(msgEsc), "\"升级失败\"");
		snprintf(json, sizeof(json), "{\"ok\":false,\"message\":%s}",
			 msgEsc);
		gUpgradeBusy = 0;
		sendJson(clientFd, 500, "Internal Server Error", json);
		return;
	}
	sendJson(clientFd, 200, "OK",
		 "{\"ok\":true,\"message\":\"升级完成，设备即将重启\"}");
	sync();
	syslog(LOG_NOTICE, "web upgrade ok, rebooting");
	if (system(cfg->rebootCmd) != 0)
		syslog(LOG_ERR, "reboot failed: %s", strerror(errno));
	gUpgradeBusy = 0;
}
