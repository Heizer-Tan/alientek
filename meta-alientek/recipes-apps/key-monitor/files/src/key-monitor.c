/* SPDX-License-Identifier: MIT */
/*
 * key-monitor — 读取 gpio-keys 的 EV_KEY，打印 type/code/value
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define DEFAULT_NAME_SUBSTR "gpio-keys"
#define INPUT_DIR "/dev/input"
#define EVENT_PREFIX "event"

static volatile sig_atomic_t gStopFlag = 0;
static int gUseSyslog = 0;
static int gInputFd = -1;

/* 处理 SIGINT/SIGTERM，请求主循环退出 */
static void onSignal(int signo)
{
	(void)signo;
	gStopFlag = 1;
}

/* 将一行事件写到 stdout 或 syslog */
static void emitLine(const char *line)
{
	if (gUseSyslog) {
		syslog(LOG_INFO, "%s", line);
		return;
	}
	puts(line);
	fflush(stdout);
}

/* 打印 EV_KEY 的 type/code/value（十进制） */
static void printKeyEvent(const struct input_event *ev)
{
	char buf[96];

	snprintf(buf, sizeof(buf), "type=%u code=%u value=%d",
		 (unsigned int)ev->type, (unsigned int)ev->code, ev->value);
	emitLine(buf);
}

/* 打开 path；失败返回 -1 并写 stderr */
static int openDevicePath(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "key-monitor: open %s: %s\n", path, strerror(errno));
		return -1;
	}
	return fd;
}

/* 若设备名包含 nameSubstr 则返回 1 */
static int nameMatches(int fd, const char *nameSubstr)
{
	char name[256];

	memset(name, 0, sizeof(name));
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
		return 0;
	return strstr(name, nameSubstr) != NULL;
}

/* 在 /dev/input 中按名称子串查找 event 设备，找到则返回已打开 fd */
static int findDeviceByName(const char *nameSubstr)
{
	DIR *dir = opendir(INPUT_DIR);
	struct dirent *ent;
	char path[PATH_MAX];

	if (!dir) {
		fprintf(stderr, "key-monitor: opendir %s: %s\n", INPUT_DIR, strerror(errno));
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		int fd;

		if (strncmp(ent->d_name, EVENT_PREFIX, strlen(EVENT_PREFIX)) != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, ent->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		if (nameMatches(fd, nameSubstr)) {
			closedir(dir);
			return fd;
		}
		close(fd);
	}

	closedir(dir);
	fprintf(stderr, "key-monitor: no input device matching \"%s\"\n", nameSubstr);
	return -1;
}

/* 解析参数；成功返回 0，失败返回 1 */
static int parseArgs(int argc, char **argv, const char **devPath,
		     const char **nameSubstr)
{
	static const struct option longOpts[] = {
		{"syslog", no_argument, NULL, 's'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0},
	};
	int opt;

	*devPath = NULL;
	*nameSubstr = DEFAULT_NAME_SUBSTR;

	while ((opt = getopt_long(argc, argv, "d:n:sh", longOpts, NULL)) != -1) {
		switch (opt) {
		case 'd':
			*devPath = optarg;
			break;
		case 'n':
			*nameSubstr = optarg;
			break;
		case 's':
			gUseSyslog = 1;
			break;
		case 'h':
			fprintf(stderr,
				"Usage: %s [-d /dev/input/eventN] [-n substr] [--syslog]\n",
				argv[0]);
			return 1;
		default:
			return 1;
		}
	}
	return 0;
}

/* 注册信号并可选打开 syslog */
static void setupRuntime(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = onSignal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (gUseSyslog)
		openlog("key-monitor", LOG_PID | LOG_CONS, LOG_USER);
}

/* 阻塞读取并打印 EV_KEY，直到收到停止信号 */
static int runEventLoop(int fd)
{
	while (!gStopFlag) {
		struct input_event ev;
		ssize_t n = read(fd, &ev, sizeof(ev));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "key-monitor: read: %s\n", strerror(errno));
			return 1;
		}
		if ((size_t)n != sizeof(ev))
			continue;
		if (ev.type != EV_KEY)
			continue;
		printKeyEvent(&ev);
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *devPath = NULL;
	const char *nameSubstr = DEFAULT_NAME_SUBSTR;
	int exitCode;

	if (parseArgs(argc, argv, &devPath, &nameSubstr) != 0)
		return 1;

	setupRuntime();

	if (devPath)
		gInputFd = openDevicePath(devPath);
	else
		gInputFd = findDeviceByName(nameSubstr);

	if (gInputFd < 0) {
		if (gUseSyslog)
			closelog();
		return 1;
	}

	exitCode = runEventLoop(gInputFd);
	close(gInputFd);
	gInputFd = -1;
	if (gUseSyslog)
		closelog();
	/* 信号停止视为成功 */
	return gStopFlag ? 0 : exitCode;
}
