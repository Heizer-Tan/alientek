/* SPDX-License-Identifier: MIT */
/* 传感器历史查询：SQLite samples API */

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

int querySamplesJson(sqlite3 *db, time_t fromTs, time_t toTs, char **outJson,
		     size_t *outLen)
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

int openSamplesDb(sqlite3 **db)
{
	if (!db)
		return -1;
	*db = NULL;
	if (sqlite3_open_v2(webserverCfg()->dbPath, db, SQLITE_OPEN_READONLY,
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

void handleSamplesRequest(int clientFd, const char *query)
{
	time_t fromTs = 0;
	time_t toTs = 0;
	sqlite3 *db = NULL;
	char *body = NULL;
	size_t bodyLen = 0;

	if (parseQueryTime(query, &fromTs, &toTs) != 0) {
		sendResponse(clientFd, 400, "Bad Request", "text/plain",
			     "bad request", 11);
		return;
	}
	if (openSamplesDb(&db) != 0) {
		sendResponse(clientFd, 500, "Internal Server Error", "text/plain",
			     "internal error", 14);
		return;
	}
	if (querySamplesJson(db, fromTs, toTs, &body, &bodyLen) != 0) {
		sqlite3_close(db);
		sendResponse(clientFd, 500, "Internal Server Error", "text/plain",
			     "internal error", 14);
		return;
	}
	sqlite3_close(db);
	sendResponse(clientFd, 200, "OK", "application/json", body, bodyLen);
	free(body);
}
