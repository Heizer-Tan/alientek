/* SPDX-License-Identifier: MIT */
/* icm20608-logger：定时读 IIO 并写入 SQLite */

#include "iio-icm.h"

#include <errno.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define ICM20608_DB_PATH "/var/lib/icm20608/icm20608.db"
#define ICM20608_DB_DIR "/var/lib/icm20608"
#define ICM20608_DEFAULT_NAME "icm20608"
#define DEFAULT_SAMPLE_INTERVAL_SEC 5
#define DEFAULT_RETAIN_SECONDS (7 * 86400)

static volatile sig_atomic_t gStopRequested = 0;
static int gSampleIntervalSec = DEFAULT_SAMPLE_INTERVAL_SEC;
static int gRetainSeconds = DEFAULT_RETAIN_SECONDS;
static const char *gDbPath = ICM20608_DB_PATH;
static const char *gDbDir = ICM20608_DB_DIR;
static const char *gIioName = ICM20608_DEFAULT_NAME;
static char *gSysfsDir = NULL;

static void handleSignal(int sig)
{
	(void)sig;
	gStopRequested = 1;
}

/* 从环境变量加载可调参数（由 /etc/default/icm20608-logger 注入） */
static void loadRuntimeConfig(void)
{
	const char *v;
	char *end;
	long n;

	v = getenv("ICM20608_SAMPLE_INTERVAL_SEC");
	if (v && *v) {
		n = strtol(v, &end, 10);
		if (end != v && *end == '\0' && n > 0 && n < 86400)
			gSampleIntervalSec = (int)n;
	}
	v = getenv("ICM20608_RETAIN_SECONDS");
	if (v && *v) {
		n = strtol(v, &end, 10);
		if (end != v && *end == '\0' && n > 0)
			gRetainSeconds = (int)n;
	}
	v = getenv("ICM20608_DB_PATH");
	if (v && *v)
		gDbPath = v;
	v = getenv("ICM20608_DB_DIR");
	if (v && *v)
		gDbDir = v;
	v = getenv("ICM20608_IIO_NAME");
	if (v && *v)
		gIioName = v;
}

static int ensureDbDir(void)
{
	if (mkdir(gDbDir, 0755) == 0)
		return 0;
	if (errno == EEXIST)
		return 0;
	syslog(LOG_ERR, "mkdir %s: %s", gDbDir, strerror(errno));
	return -1;
}

static int openDb(sqlite3 **db)
{
	const char *schema =
		"CREATE TABLE IF NOT EXISTS samples ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  ts INTEGER NOT NULL,"
		"  ax INTEGER NOT NULL, ay INTEGER NOT NULL, az INTEGER NOT NULL,"
		"  gx INTEGER NOT NULL, gy INTEGER NOT NULL, gz INTEGER NOT NULL,"
		"  temp_raw INTEGER NOT NULL,"
		"  ax_g REAL NOT NULL, ay_g REAL NOT NULL, az_g REAL NOT NULL,"
		"  gx_dps REAL NOT NULL, gy_dps REAL NOT NULL, gz_dps REAL NOT NULL,"
		"  temp_c REAL NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts);";
	char *errMsg = NULL;
	int rc;

	if (!db)
		return -1;
	rc = sqlite3_open(gDbPath, db);
	if (rc != SQLITE_OK) {
		syslog(LOG_ERR, "sqlite3_open: %s", sqlite3_errmsg(*db));
		sqlite3_close(*db);
		*db = NULL;
		return -1;
	}
	rc = sqlite3_exec(*db, schema, NULL, NULL, &errMsg);
	if (rc != SQLITE_OK) {
		syslog(LOG_ERR, "schema: %s",
		       errMsg ? errMsg : sqlite3_errmsg(*db));
		sqlite3_free(errMsg);
		sqlite3_close(*db);
		*db = NULL;
		return -1;
	}
	return 0;
}

static int readDeviceSample(struct IcmIioSample *out)
{
	if (gSysfsDir == NULL || out == NULL)
		return -1;
	if (iioIcmReadSample(gSysfsDir, out) != 0) {
		syslog(LOG_ERR, "iioIcmReadSample %s failed", gSysfsDir);
		return -1;
	}
	return 0;
}

static int insertSample(sqlite3 *db, time_t ts, const struct IcmIioSample *s)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	rc = sqlite3_prepare_v2(
		db,
		"INSERT INTO samples(ts,ax,ay,az,gx,gy,gz,temp_raw,"
		"ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c)"
		" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		syslog(LOG_ERR, "prepare insert: %s", sqlite3_errmsg(db));
		return -1;
	}
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)ts);
	sqlite3_bind_int(stmt, 2, s->ax);
	sqlite3_bind_int(stmt, 3, s->ay);
	sqlite3_bind_int(stmt, 4, s->az);
	sqlite3_bind_int(stmt, 5, s->gx);
	sqlite3_bind_int(stmt, 6, s->gy);
	sqlite3_bind_int(stmt, 7, s->gz);
	sqlite3_bind_int(stmt, 8, s->temp_raw);
	sqlite3_bind_double(stmt, 9, s->ax_g);
	sqlite3_bind_double(stmt, 10, s->ay_g);
	sqlite3_bind_double(stmt, 11, s->az_g);
	sqlite3_bind_double(stmt, 12, s->gx_dps);
	sqlite3_bind_double(stmt, 13, s->gy_dps);
	sqlite3_bind_double(stmt, 14, s->gz_dps);
	sqlite3_bind_double(stmt, 15, s->temp_c);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		syslog(LOG_ERR, "insert: %s", sqlite3_errmsg(db));
		return -1;
	}
	return 0;
}

static int purgeOldSamples(sqlite3 *db, time_t now)
{
	sqlite3_stmt *stmt = NULL;
	time_t cutoff = now - gRetainSeconds;
	int rc;

	rc = sqlite3_prepare_v2(db, "DELETE FROM samples WHERE ts < ?;", -1,
				&stmt, NULL);
	if (rc != SQLITE_OK) {
		syslog(LOG_ERR, "prepare purge: %s", sqlite3_errmsg(db));
		return -1;
	}
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		syslog(LOG_ERR, "purge: %s", sqlite3_errmsg(db));
		return -1;
	}
	return 0;
}

static int sampleOnce(sqlite3 *db)
{
	struct IcmIioSample s;
	time_t now = time(NULL);

	if (now == (time_t)-1) {
		syslog(LOG_ERR, "time: %s", strerror(errno));
		return -1;
	}
	if (readDeviceSample(&s) != 0)
		return -1;
	if (insertSample(db, now, &s) != 0)
		return -1;
	if (purgeOldSamples(db, now) != 0)
		return -1;
	syslog(LOG_INFO, "sample ax_g=%.4f ay_g=%.4f az_g=%.4f temp_c=%.2f",
	       s.ax_g, s.ay_g, s.az_g, s.temp_c);
	return 0;
}

int main(void)
{
	sqlite3 *db = NULL;
	struct sigaction sa;

	openlog("icm20608-logger", LOG_PID, LOG_DAEMON);
	loadRuntimeConfig();
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	gSysfsDir = iioIcmFindSysfsDir(gIioName);
	if (gSysfsDir == NULL) {
		syslog(LOG_ERR, "IIO device \"%s\" not found", gIioName);
		closelog();
		return 1;
	}

	if (ensureDbDir() != 0) {
		free(gSysfsDir);
		closelog();
		return 1;
	}
	if (openDb(&db) != 0) {
		free(gSysfsDir);
		closelog();
		return 1;
	}
	syslog(LOG_INFO, "started interval=%ds retain=%ds iio=%s",
	       gSampleIntervalSec, gRetainSeconds, gIioName);

	while (!gStopRequested) {
		sampleOnce(db);
		if (gStopRequested)
			break;
		sleep((unsigned int)gSampleIntervalSec);
	}

	sqlite3_close(db);
	free(gSysfsDir);
	syslog(LOG_INFO, "stopped");
	closelog();
	return 0;
}
