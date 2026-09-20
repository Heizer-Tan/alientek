/* SPDX-License-Identifier: MIT */
/* icm20608-logger：定时读取 /dev/icm20608 并写入 SQLite */

#include <stdio.h>
#include <string.h>

#ifndef ICM20608_LOGGER_TEST_PARSE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#endif

#define ICM20608_DEV_PATH "/dev/icm20608"
#define ICM20608_DB_PATH  "/var/lib/icm20608/icm20608.db"
#define ICM20608_DB_DIR   "/var/lib/icm20608"
#define DEFAULT_SAMPLE_INTERVAL_SEC 5
#define DEFAULT_RETAIN_SECONDS (7 * 86400)
#define ICM20608_BUF_SIZE 256

struct IcmSample {
	int ax;
	int ay;
	int az;
	int gx;
	int gy;
	int gz;
	int temp_raw;
	double ax_g;
	double ay_g;
	double az_g;
	double gx_dps;
	double gy_dps;
	double gz_dps;
	double temp_c;
};

#ifndef ICM20608_LOGGER_TEST_PARSE
static volatile sig_atomic_t gStopRequested = 0;
static int gSampleIntervalSec = DEFAULT_SAMPLE_INTERVAL_SEC;
static int gRetainSeconds = DEFAULT_RETAIN_SECONDS;
static const char *gDbPath = ICM20608_DB_PATH;
static const char *gDbDir = ICM20608_DB_DIR;
static const char *gDevPath = ICM20608_DEV_PATH;

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
	v = getenv("ICM20608_DEV_PATH");
	if (v && *v)
		gDevPath = v;
}
#endif

/* 解析设备文本行；0 成功，非 0 失败 */
static int parseSample(const char *line, struct IcmSample *out)
{
	struct IcmSample s;

	if (!line || !out)
		return -1;
	if (sscanf(line,
		   "ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp_raw=%d "
		   "ax_g=%lf ay_g=%lf az_g=%lf gx_dps=%lf gy_dps=%lf "
		   "gz_dps=%lf temp_c=%lf",
		   &s.ax, &s.ay, &s.az, &s.gx, &s.gy, &s.gz, &s.temp_raw,
		   &s.ax_g, &s.ay_g, &s.az_g, &s.gx_dps, &s.gy_dps, &s.gz_dps,
		   &s.temp_c) != 14)
		return -1;
	*out = s;
	return 0;
}

#ifndef ICM20608_LOGGER_TEST_PARSE
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
		syslog(LOG_ERR, "schema: %s", errMsg ? errMsg : sqlite3_errmsg(*db));
		sqlite3_free(errMsg);
		sqlite3_close(*db);
		*db = NULL;
		return -1;
	}
	return 0;
}

static int readDeviceSample(struct IcmSample *out)
{
	char buf[ICM20608_BUF_SIZE];
	ssize_t n;
	int fd;

	fd = open(gDevPath, O_RDONLY);
	if (fd < 0) {
		syslog(LOG_ERR, "open %s: %s", gDevPath, strerror(errno));
		return -1;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		syslog(LOG_ERR, "read %s: %s", gDevPath, strerror(errno));
		close(fd);
		return -1;
	}
	buf[n] = '\0';
	close(fd);
	if (parseSample(buf, out) != 0) {
		syslog(LOG_ERR, "parse sample: %s", buf);
		return -1;
	}
	return 0;
}

static int insertSample(sqlite3 *db, time_t ts, const struct IcmSample *s)
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
	struct IcmSample s;
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
#endif /* ICM20608_LOGGER_TEST_PARSE */

#ifdef ICM20608_LOGGER_TEST_PARSE
int main(void)
{
	struct IcmSample s;
	const char *line =
		"ax=-164 ay=328 az=16320 gx=12 gy=-8 gz=3 temp_raw=-1200 "
		"ax_g=-0.0100 ay_g=0.0200 az_g=0.9956 gx_dps=0.732 "
		"gy_dps=-0.488 gz_dps=0.183 temp_c=21.33\n";

	if (parseSample(line, &s) != 0) {
		fprintf(stderr, "parseSample test failed\n");
		return 1;
	}
	if (s.ax != -164 || s.az != 16320 || s.temp_raw != -1200)
		return 1;
	if (s.ax_g < -0.011 || s.ax_g > -0.009)
		return 1;
	if (s.temp_c < 21.3 || s.temp_c > 21.4)
		return 1;
	return 0;
}
#else
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

	if (ensureDbDir() != 0) {
		closelog();
		return 1;
	}
	if (openDb(&db) != 0) {
		closelog();
		return 1;
	}
	syslog(LOG_INFO, "started interval=%ds retain=%ds", gSampleIntervalSec,
	       gRetainSeconds);

	while (!gStopRequested) {
		sampleOnce(db);
		if (gStopRequested)
			break;
		sleep((unsigned int)gSampleIntervalSec);
	}

	sqlite3_close(db);
	syslog(LOG_INFO, "stopped");
	closelog();
	return 0;
}
#endif
