/* SPDX-License-Identifier: MIT */
/* ap3216c-logger：定时读取 /dev/ap3216c 并写入 SQLite */

#include <stdio.h>
#include <string.h>

#ifndef AP3216C_LOGGER_TEST_PARSE
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

#define AP3216C_DEV_PATH "/dev/ap3216c"
#define AP3216C_DB_PATH  "/var/lib/ap3216c/ap3216c.db"
#define AP3216C_DB_DIR   "/var/lib/ap3216c"
#define SAMPLE_INTERVAL_SEC 300
#define RETAIN_SECONDS (7 * 86400)
#define AP3216C_BUF_SIZE 128

#ifndef AP3216C_LOGGER_TEST_PARSE
static volatile sig_atomic_t gStopRequested = 0;

static void handleSignal(int sig)
{
	(void)sig;
	gStopRequested = 1;
}
#endif

/* 解析设备文本行 ir=%u als=%u ps=%u；0 成功，非 0 失败 */
static int parseSample(const char *line, unsigned *ir, unsigned *als, unsigned *ps)
{
	unsigned parsedIr, parsedAls, parsedPs;

	if (!line || !ir || !als || !ps)
		return -1;
	if (sscanf(line, "ir=%u als=%u ps=%u", &parsedIr, &parsedAls, &parsedPs) != 3)
		return -1;
	*ir = parsedIr;
	*als = parsedAls;
	*ps = parsedPs;
	return 0;
}

#ifndef AP3216C_LOGGER_TEST_PARSE
/* 确保数据库目录存在；0 成功，非 0 失败 */
static int ensureDbDir(void)
{
	if (mkdir(AP3216C_DB_DIR, 0755) == 0)
		return 0;
	if (errno == EEXIST)
		return 0;
	syslog(LOG_ERR, "mkdir %s: %s", AP3216C_DB_DIR, strerror(errno));
	return -1;
}

/* 打开数据库并建表、索引；0 成功，非 0 失败 */
static int openDb(sqlite3 **db)
{
	const char *schema =
		"CREATE TABLE IF NOT EXISTS samples ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  ts INTEGER NOT NULL,"
		"  ir INTEGER NOT NULL,"
		"  als INTEGER NOT NULL,"
		"  ps INTEGER NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts);";
	char *errMsg = NULL;
	int rc;

	if (!db)
		return -1;
	rc = sqlite3_open(AP3216C_DB_PATH, db);
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

/* 从 /dev/ap3216c 读取一次样本；0 成功，非 0 失败 */
static int readDeviceSample(unsigned *ir, unsigned *als, unsigned *ps)
{
	char buf[AP3216C_BUF_SIZE];
	ssize_t n;
	int fd;

	fd = open(AP3216C_DEV_PATH, O_RDONLY);
	if (fd < 0) {
		syslog(LOG_ERR, "open %s: %s", AP3216C_DEV_PATH, strerror(errno));
		return -1;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		syslog(LOG_ERR, "read %s: %s", AP3216C_DEV_PATH, strerror(errno));
		close(fd);
		return -1;
	}
	buf[n] = '\0';
	close(fd);
	if (parseSample(buf, ir, als, ps) != 0) {
		syslog(LOG_ERR, "parse sample: %s", buf);
		return -1;
	}
	return 0;
}

/* 插入一条采样记录；0 成功，非 0 失败 */
static int insertSample(sqlite3 *db, time_t ts, unsigned ir, unsigned als, unsigned ps)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	rc = sqlite3_prepare_v2(db,
			      "INSERT INTO samples(ts,ir,als,ps) VALUES(?,?,?,?);",
			      -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		syslog(LOG_ERR, "prepare insert: %s", sqlite3_errmsg(db));
		return -1;
	}
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)ts);
	sqlite3_bind_int(stmt, 2, (int)ir);
	sqlite3_bind_int(stmt, 3, (int)als);
	sqlite3_bind_int(stmt, 4, (int)ps);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		syslog(LOG_ERR, "insert: %s", sqlite3_errmsg(db));
		return -1;
	}
	return 0;
}

/* 删除超过保留期的旧记录；0 成功，非 0 失败 */
static int purgeOldSamples(sqlite3 *db, time_t now)
{
	sqlite3_stmt *stmt = NULL;
	time_t cutoff = now - RETAIN_SECONDS;
	int rc;

	rc = sqlite3_prepare_v2(db, "DELETE FROM samples WHERE ts < ?;", -1, &stmt, NULL);
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

/* 单次采样：读设备、入库、清理；失败记 syslog 并返回非 0 */
static int sampleOnce(sqlite3 *db)
{
	unsigned ir, als, ps;
	time_t now = time(NULL);

	if (now == (time_t)-1) {
		syslog(LOG_ERR, "time: %s", strerror(errno));
		return -1;
	}
	if (readDeviceSample(&ir, &als, &ps) != 0)
		return -1;
	if (insertSample(db, now, ir, als, ps) != 0)
		return -1;
	if (purgeOldSamples(db, now) != 0)
		return -1;
	syslog(LOG_INFO, "sample ir=%u als=%u ps=%u", ir, als, ps);
	return 0;
}

#endif /* AP3216C_LOGGER_TEST_PARSE */

#ifdef AP3216C_LOGGER_TEST_PARSE
/* 宿主机 parseSample 小测 */
int main(void)
{
	unsigned ir = 0, als = 0, ps = 0;

	if (parseSample("ir=1 als=2 ps=3\n", &ir, &als, &ps) != 0 ||
	    ir != 1 || als != 2 || ps != 3) {
		fprintf(stderr, "parseSample test failed\n");
		return 1;
	}
	return 0;
}
#else
int main(void)
{
	sqlite3 *db = NULL;
	struct sigaction sa;

	openlog("ap3216c-logger", LOG_PID, LOG_DAEMON);
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
	syslog(LOG_INFO, "started interval=%ds retain=%ds",
	       SAMPLE_INTERVAL_SEC, RETAIN_SECONDS);

	while (!gStopRequested) {
		sampleOnce(db);
		if (gStopRequested)
			break;
		sleep(SAMPLE_INTERVAL_SEC);
	}

	sqlite3_close(db);
	syslog(LOG_INFO, "stopped");
	closelog();
	return 0;
}
#endif
