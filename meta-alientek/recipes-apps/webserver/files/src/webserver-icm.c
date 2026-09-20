/* SPDX-License-Identifier: MIT */
/* ICM20608 历史查询：独立页与 JSON API */

#include "webserver.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

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

static int openIcmDb(sqlite3 **db)
{
	if (!db)
		return -1;
	*db = NULL;
	if (sqlite3_open_v2(webserverCfg()->icmDbPath, db, SQLITE_OPEN_READONLY,
			    NULL) != SQLITE_OK) {
		if (*db) {
			syslog(LOG_ERR, "icm sqlite3_open: %s",
			       sqlite3_errmsg(*db));
			sqlite3_close(*db);
			*db = NULL;
		}
		return -1;
	}
	return 0;
}

static int queryIcmSamplesJson(sqlite3 *db, time_t fromTs, time_t toTs,
			       char **outJson, size_t *outLen)
{
	sqlite3_stmt *stmt = NULL;
	char *json = NULL;
	size_t len = 0;
	size_t cap = 0;
	int rc;
	int first = 1;
	char item[384];

	if (!db || !outJson || !outLen)
		return -1;
	*outJson = NULL;
	*outLen = 0;
	rc = sqlite3_prepare_v2(
		db,
		"SELECT ts,ax,ay,az,gx,gy,gz,temp_raw,"
		"ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c "
		"FROM samples WHERE ts >= ? AND ts <= ? ORDER BY ts ASC;",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)fromTs);
	sqlite3_bind_int64(stmt, 2, (sqlite3_int64)toTs);
	if (appendStr(&json, &len, &cap, "{\"ok\":true,\"samples\":[") != 0) {
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
			 "{\"ts\":%lld,\"ax\":%d,\"ay\":%d,\"az\":%d,"
			 "\"gx\":%d,\"gy\":%d,\"gz\":%d,\"temp_raw\":%d,"
			 "\"ax_g\":%.4f,\"ay_g\":%.4f,\"az_g\":%.4f,"
			 "\"gx_dps\":%.3f,\"gy_dps\":%.3f,\"gz_dps\":%.3f,"
			 "\"temp_c\":%.2f}",
			 (long long)sqlite3_column_int64(stmt, 0),
			 sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2),
			 sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4),
			 sqlite3_column_int(stmt, 5), sqlite3_column_int(stmt, 6),
			 sqlite3_column_int(stmt, 7),
			 sqlite3_column_double(stmt, 8),
			 sqlite3_column_double(stmt, 9),
			 sqlite3_column_double(stmt, 10),
			 sqlite3_column_double(stmt, 11),
			 sqlite3_column_double(stmt, 12),
			 sqlite3_column_double(stmt, 13),
			 sqlite3_column_double(stmt, 14));
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
	if (appendStr(&json, &len, &cap, "]}") != 0) {
		free(json);
		return -1;
	}
	*outJson = json;
	*outLen = len;
	return 0;
}

void handleIcmPageRequest(int clientFd)
{
	char *body = NULL;
	size_t bodyLen = 0;

	if (readWholeFile(webserverCfg()->icmIndexPath, &body, &bodyLen) != 0) {
		sendResponse(clientFd, 500, "Internal Server Error", "text/plain",
			     "internal error", 14);
		return;
	}
	sendResponse(clientFd, 200, "OK", "text/html; charset=utf-8", body,
		     bodyLen);
	free(body);
}

void handleIcmSamplesRequest(int clientFd, const char *query)
{
	time_t fromTs = 0;
	time_t toTs = 0;
	sqlite3 *db = NULL;
	char *body = NULL;
	size_t bodyLen = 0;

	if (parseQueryTime(query, &fromTs, &toTs) != 0) {
		sendJson(clientFd, 400, "Bad Request",
			 "{\"ok\":false,\"message\":\"时间范围无效\"}");
		return;
	}
	if (openIcmDb(&db) != 0) {
		sendJson(clientFd, 500, "Internal Server Error",
			 "{\"ok\":false,\"message\":\"无法打开 ICM20608 数据库\"}");
		return;
	}
	if (queryIcmSamplesJson(db, fromTs, toTs, &body, &bodyLen) != 0) {
		sqlite3_close(db);
		sendJson(clientFd, 500, "Internal Server Error",
			 "{\"ok\":false,\"message\":\"查询失败\"}");
		return;
	}
	sqlite3_close(db);
	sendResponse(clientFd, 200, "OK", "application/json", body, bodyLen);
	free(body);
}
