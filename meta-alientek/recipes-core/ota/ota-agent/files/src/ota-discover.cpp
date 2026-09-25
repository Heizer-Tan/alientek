/* SPDX-License-Identifier: MIT */
/*
 * 从 python -m http.server 一类目录页中挑选最新 .swu。
 * 优先带时间戳的真实包 …rootfs-YYYYMMDDHHMMSS.swu；
 * …rootfs.swu 仅为 Yocto 软链，仅在没有时间戳包时回退。
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ota-discover.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { kMaxSwuNames = 64, kMaxNameLen = 256 };

static int endsWith(const char *s, const char *suffix)
{
	size_t sl;
	size_t su;

	if (s == NULL || suffix == NULL)
		return 0;
	sl = strlen(s);
	su = strlen(suffix);
	if (sl < su)
		return 0;
	return strcmp(s + (sl - su), suffix) == 0;
}

/* 是否为 Yocto 无版本号软链名：…alpha.rootfs.swu（非 …rootfs-14位.swu） */
static int isUnversionedUpdateSwu(const char *name)
{
	const char *p;

	if (strstr(name, "alientek-image-update") == NULL)
		return 0;
	if (!endsWith(name, ".rootfs.swu"))
		return 0;
	p = strstr(name, "rootfs-");
	if (p == NULL)
		return 1;
	p += 7;
	for (int i = 0; i < 14; ++i) {
		if (!isdigit((unsigned char)p[i]))
			return 1;
	}
	return p[14] != '.';
}

/* 从文件名提取 rootfs- 后 14 位时间戳；失败返回 -1 */
static long long extractBuildStamp(const char *name)
{
	const char *p = strstr(name, "rootfs-");
	char buf[16];
	char *end = NULL;
	long long v;

	if (p == NULL)
		return -1;
	p += 7;
	for (int i = 0; i < 14; ++i) {
		if (!isdigit((unsigned char)p[i]))
			return -1;
		buf[i] = p[i];
	}
	buf[14] = '\0';
	v = strtoll(buf, &end, 10);
	if (end == NULL || *end != '\0')
		return -1;
	return v;
}

static int isUpdateSwu(const char *name)
{
	return strstr(name, "alientek-image-update") != NULL &&
	       endsWith(name, ".swu");
}

static void trimUrlBase(const char *baseUrl, char *out, size_t outSize)
{
	size_t n;

	(void)snprintf(out, outSize, "%s", baseUrl != NULL ? baseUrl : "");
	n = strlen(out);
	while (n > 0 && out[n - 1] == '/') {
		out[n - 1] = '\0';
		--n;
	}
}

/* 从 href= 引号内容取文件名（去掉查询串与目录前缀） */
static int extractHrefName(const char *href, char *nameOut, size_t nameOutSize)
{
	const char *slash;
	const char *q;
	size_t len;

	if (href == NULL || href[0] == '\0' || nameOutSize == 0)
		return -1;
	slash = strrchr(href, '/');
	if (slash != NULL)
		href = slash + 1;
	q = strchr(href, '?');
	len = (q != NULL) ? (size_t)(q - href) : strlen(href);
	if (len == 0 || len >= nameOutSize)
		return -1;
	memcpy(nameOut, href, len);
	nameOut[len] = '\0';
	return 0;
}

static int appendUniqueName(char names[][kMaxNameLen], int *count,
			    const char *name)
{
	int i;

	if (!isUpdateSwu(name) || *count >= kMaxSwuNames)
		return 0;
	for (i = 0; i < *count; ++i) {
		if (strcmp(names[i], name) == 0)
			return 0;
	}
	(void)snprintf(names[*count], kMaxNameLen, "%s", name);
	++(*count);
	return 1;
}

static void parseListingHtml(const char *html, char names[][kMaxNameLen],
			     int *count)
{
	const char *p = html;

	*count = 0;
	while (p != NULL && *p != '\0' && *count < kMaxSwuNames) {
		const char *hrefKey = strcasestr(p, "href=");
		char quote;
		const char *start;
		const char *end;
		char href[kMaxNameLen];
		char name[kMaxNameLen];
		size_t n;

		if (hrefKey == NULL)
			break;
		p = hrefKey + 5;
		while (*p == ' ' || *p == '\t')
			++p;
		quote = *p;
		if (quote != '"' && quote != '\'')
			continue;
		start = p + 1;
		end = strchr(start, quote);
		if (end == NULL)
			break;
		n = (size_t)(end - start);
		if (n >= sizeof(href)) {
			p = end + 1;
			continue;
		}
		memcpy(href, start, n);
		href[n] = '\0';
		if (extractHrefName(href, name, sizeof(name)) == 0)
			(void)appendUniqueName(names, count, name);
		p = end + 1;
	}
}

/*
 * 选包规则：
 * 1) 优先带构建时间戳的真实包 …rootfs-YYYYMMDDHHMMSS.swu（取 stamp 最大）
 * 2) 若目录里只有无时间戳名（Yocto 软链 …rootfs.swu），再退回该快捷方式
 */
static int pickBestSwuName(char names[][kMaxNameLen], int count,
			   char *bestOut, size_t bestOutSize)
{
	int i;
	int bestStamped = -1;
	int fallbackLink = -1;
	long long bestStamp = -1;

	if (count <= 0 || bestOutSize == 0)
		return -1;
	for (i = 0; i < count; ++i) {
		long long stamp = extractBuildStamp(names[i]);

		if (stamp >= 0) {
			if (stamp > bestStamp) {
				bestStamp = stamp;
				bestStamped = i;
			}
			continue;
		}
		if (fallbackLink < 0 && isUnversionedUpdateSwu(names[i]))
			fallbackLink = i;
	}
	if (bestStamped >= 0) {
		(void)snprintf(bestOut, bestOutSize, "%s", names[bestStamped]);
		return 0;
	}
	if (fallbackLink >= 0) {
		(void)snprintf(bestOut, bestOutSize, "%s", names[fallbackLink]);
		return 0;
	}
	(void)snprintf(bestOut, bestOutSize, "%s", names[0]);
	return 0;
}

static int downloadListing(const char *listUrl, char **htmlOut, char *errorBuf,
			   size_t errorBufSize)
{
	char tmpPath[] = "/var/tmp/ota-listing-XXXXXX";
	int fd;
	FILE *fp;
	long sz;
	char *buf;
	char cmd[512];
	int st;

	fd = mkstemp(tmpPath);
	if (fd < 0) {
		(void)snprintf(errorBuf, errorBufSize, "创建临时文件失败");
		return -1;
	}
	close(fd);
	(void)snprintf(cmd, sizeof(cmd),
		       "curl --fail --silent --show-error --max-time 30 "
		       "--output '%s' -- '%s'",
		       tmpPath, listUrl);
	st = system(cmd);
	if (st != 0) {
		unlink(tmpPath);
		(void)snprintf(errorBuf, errorBufSize,
			       "下载目录页失败（curl exit=%d）", st);
		return -1;
	}
	fp = fopen(tmpPath, "rb");
	if (fp == NULL) {
		unlink(tmpPath);
		(void)snprintf(errorBuf, errorBufSize, "打开目录页失败");
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		unlink(tmpPath);
		(void)snprintf(errorBuf, errorBufSize, "读取目录页失败");
		return -1;
	}
	sz = ftell(fp);
	if (sz < 0 || sz > 2 * 1024 * 1024) {
		fclose(fp);
		unlink(tmpPath);
		(void)snprintf(errorBuf, errorBufSize, "目录页过大或无效");
		return -1;
	}
	rewind(fp);
	buf = (char *)malloc((size_t)sz + 1);
	if (buf == NULL) {
		fclose(fp);
		unlink(tmpPath);
		(void)snprintf(errorBuf, errorBufSize, "内存不足");
		return -1;
	}
	if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
		free(buf);
		fclose(fp);
		unlink(tmpPath);
		(void)snprintf(errorBuf, errorBufSize, "读目录页不完整");
		return -1;
	}
	buf[sz] = '\0';
	fclose(fp);
	unlink(tmpPath);
	*htmlOut = buf;
	return 0;
}

int otaResolveLatestSwuUrl(const char *baseUrl, char *urlOut, size_t urlOutSize,
			   char *nameOut, size_t nameOutSize, char *errorBuf,
			   size_t errorBufSize)
{
	char base[256];
	char listUrl[288];
	char *html = NULL;
	char names[kMaxSwuNames][kMaxNameLen];
	char best[kMaxNameLen];
	int count = 0;

	if (baseUrl == NULL || baseUrl[0] == '\0' || urlOut == NULL ||
	    urlOutSize == 0) {
		(void)snprintf(errorBuf, errorBufSize, "固件目录 URL 无效");
		return -1;
	}
	trimUrlBase(baseUrl, base, sizeof(base));
	(void)snprintf(listUrl, sizeof(listUrl), "%s/", base);
	if (downloadListing(listUrl, &html, errorBuf, errorBufSize) != 0)
		return -1;
	parseListingHtml(html, names, &count);
	free(html);
	if (count == 0) {
		(void)snprintf(errorBuf, errorBufSize,
			       "目录中未找到 alientek-image-update*.swu");
		return -1;
	}
	if (pickBestSwuName(names, count, best, sizeof(best)) != 0) {
		(void)snprintf(errorBuf, errorBufSize, "无法选择最新固件");
		return -1;
	}
	if (nameOut != NULL && nameOutSize > 0)
		(void)snprintf(nameOut, nameOutSize, "%s", best);
	if ((size_t)snprintf(urlOut, urlOutSize, "%s/%s", base, best) >=
	    urlOutSize) {
		(void)snprintf(errorBuf, errorBufSize, "固件 URL 过长");
		return -1;
	}
	return 0;
}
