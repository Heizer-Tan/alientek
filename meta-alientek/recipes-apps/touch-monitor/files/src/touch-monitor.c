/* SPDX-License-Identifier: MIT */
/*
 * touch-monitor — 读取电容触摸（默认 Goodix）input 事件流
 * 打印 EV_ABS / EV_KEY / EV_SYN，便于串口观察 touch stream
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "input-device.h"

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

#define DEFAULT_NAME_SUBSTR "Goodix"
#define INPUT_DIR "/dev/input"
#define EVENT_PREFIX "event"

static volatile sig_atomic_t gStopFlag = 0;
static int gUseSyslog = 0;
static int gQuietSummary = 0;
static int gInputFd = -1;

/* 当前一帧内的多点摘要（SYN_REPORT 时打印） */
static int gHaveX;
static int gHaveY;
static int gHaveId;
static int gHaveBtn;
static int gPosX;
static int gPosY;
static int gTrackId;
static int gBtnTouch;

static void onSignal(int signo)
{
	(void)signo;
	gStopFlag = 1;
}

static void emitLine(const char *line)
{
	if (gUseSyslog) {
		syslog(LOG_INFO, "%s", line);
		return;
	}
	puts(line);
	fflush(stdout);
}

static const char *absCodeName(unsigned int code)
{
	switch (code) {
	case ABS_X:
		return "ABS_X";
	case ABS_Y:
		return "ABS_Y";
	case ABS_MT_SLOT:
		return "ABS_MT_SLOT";
	case ABS_MT_TRACKING_ID:
		return "ABS_MT_TRACKING_ID";
	case ABS_MT_POSITION_X:
		return "ABS_MT_POSITION_X";
	case ABS_MT_POSITION_Y:
		return "ABS_MT_POSITION_Y";
	case ABS_MT_PRESSURE:
		return "ABS_MT_PRESSURE";
	case ABS_MT_TOUCH_MAJOR:
		return "ABS_MT_TOUCH_MAJOR";
	default:
		return NULL;
	}
}

static void printRawEvent(const struct input_event *ev)
{
	char buf[160];
	const char *absName;

	if (ev->type == EV_ABS) {
		absName = absCodeName((unsigned int)ev->code);
		if (absName)
			snprintf(buf, sizeof(buf),
				 "type=%u(%s) code=%u(%s) value=%d",
				 (unsigned int)ev->type, "EV_ABS",
				 (unsigned int)ev->code, absName, ev->value);
		else
			snprintf(buf, sizeof(buf),
				 "type=%u(EV_ABS) code=%u value=%d",
				 (unsigned int)ev->type,
				 (unsigned int)ev->code, ev->value);
	} else if (ev->type == EV_KEY && ev->code == BTN_TOUCH) {
		snprintf(buf, sizeof(buf),
			 "type=%u(EV_KEY) code=%u(BTN_TOUCH) value=%d",
			 (unsigned int)ev->type, (unsigned int)ev->code,
			 ev->value);
	} else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
		snprintf(buf, sizeof(buf), "type=%u(EV_SYN) code=%u(SYN_REPORT)",
			 (unsigned int)ev->type, (unsigned int)ev->code);
	} else {
		snprintf(buf, sizeof(buf), "type=%u code=%u value=%d",
			 (unsigned int)ev->type, (unsigned int)ev->code,
			 ev->value);
	}
	emitLine(buf);
}

static void updateSummary(const struct input_event *ev)
{
	if (ev->type == EV_ABS) {
		switch (ev->code) {
		case ABS_X:
		case ABS_MT_POSITION_X:
			gPosX = ev->value;
			gHaveX = 1;
			break;
		case ABS_Y:
		case ABS_MT_POSITION_Y:
			gPosY = ev->value;
			gHaveY = 1;
			break;
		case ABS_MT_TRACKING_ID:
			gTrackId = ev->value;
			gHaveId = 1;
			break;
		default:
			break;
		}
	} else if (ev->type == EV_KEY && ev->code == BTN_TOUCH) {
		gBtnTouch = ev->value;
		gHaveBtn = 1;
	}
}

static void printSummary(void)
{
	char buf[160];

	if (!gHaveX && !gHaveY && !gHaveId && !gHaveBtn)
		return;
	snprintf(buf, sizeof(buf),
		 "touch x=%s%d y=%s%d id=%s%d btn=%s%d",
		 gHaveX ? "" : "?", gHaveX ? gPosX : 0,
		 gHaveY ? "" : "?", gHaveY ? gPosY : 0,
		 gHaveId ? "" : "?", gHaveId ? gTrackId : -1,
		 gHaveBtn ? "" : "?", gHaveBtn ? gBtnTouch : 0);
	emitLine(buf);
	gHaveX = gHaveY = gHaveId = gHaveBtn = 0;
}

static int openDevicePath(const char *path)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "touch-monitor: open %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	return fd;
}

/* 设备是否具备触摸绝对坐标能力 */
static int hasTouchAbs(int fd)
{
	struct input_absinfo info;

	memset(&info, 0, sizeof(info));
	if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &info) == 0)
		return 1;
	if (ioctl(fd, EVIOCGABS(ABS_X), &info) == 0)
		return 1;
	return 0;
}

static int findDeviceByName(const char *nameSubstr)
{
	DIR *dir;
	struct dirent *ent;
	char path[PATH_MAX];
	int fallbackFd = -1;
	int fd;

	fd = inputFindDeviceByName(nameSubstr, "touch-monitor", 1);
	if (fd >= 0) {
		fprintf(stderr, "touch-monitor: using name match \"%s\"\n",
			nameSubstr);
		return fd;
	}

	dir = opendir(INPUT_DIR);
	if (!dir) {
		fprintf(stderr, "touch-monitor: opendir %s: %s\n", INPUT_DIR,
			strerror(errno));
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, EVENT_PREFIX, strlen(EVENT_PREFIX)) !=
		    0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, ent->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		if (fallbackFd < 0 && hasTouchAbs(fd)) {
			fallbackFd = fd;
			continue;
		}
		close(fd);
	}

	closedir(dir);
	if (fallbackFd >= 0) {
		fprintf(stderr,
			"touch-monitor: name \"%s\" 未命中，改用首个触摸 ABS 设备\n",
			nameSubstr);
		return fallbackFd;
	}
	fprintf(stderr, "touch-monitor: 未找到触摸 input 设备\n");
	return -1;
}

static int parseArgs(int argc, char **argv, const char **devPath,
		     const char **nameSubstr)
{
	static const struct option longOpts[] = {
		{"syslog", no_argument, NULL, 's'},
		{"summary", no_argument, NULL, 'S'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0},
	};
	int opt;

	*devPath = NULL;
	*nameSubstr = DEFAULT_NAME_SUBSTR;

	while ((opt = getopt_long(argc, argv, "d:n:sSh", longOpts, NULL)) !=
	       -1) {
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
		case 'S':
			gQuietSummary = 1;
			break;
		case 'h':
			fprintf(stderr,
				"Usage: %s [-d /dev/input/eventN] [-n substr]\n"
				"          [--summary|-S] [--syslog]\n"
				"  默认按名称含 \"%s\" 查找；-S 仅在 SYN_REPORT 打印 x/y 摘要\n",
				argv[0], DEFAULT_NAME_SUBSTR);
			return 1;
		default:
			return 1;
		}
	}
	return 0;
}

static void setupRuntime(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = onSignal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (gUseSyslog)
		openlog("touch-monitor", LOG_PID | LOG_CONS, LOG_USER);
}

static int runEventLoop(int fd)
{
	while (!gStopFlag) {
		struct input_event ev;
		ssize_t n = read(fd, &ev, sizeof(ev));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "touch-monitor: read: %s\n",
				strerror(errno));
			return 1;
		}
		if ((size_t)n != sizeof(ev))
			continue;
		if (ev.type != EV_ABS && ev.type != EV_KEY && ev.type != EV_SYN)
			continue;
		if (ev.type == EV_KEY && ev.code != BTN_TOUCH)
			continue;

		updateSummary(&ev);
		if (gQuietSummary) {
			if (ev.type == EV_SYN && ev.code == SYN_REPORT)
				printSummary();
		} else {
			printRawEvent(&ev);
			if (ev.type == EV_SYN && ev.code == SYN_REPORT)
				printSummary();
		}
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
	return gStopFlag ? 0 : exitCode;
}
