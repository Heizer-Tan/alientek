/* SPDX-License-Identifier: MIT */
/* sensor-dashboard：LCD 传感器仪表盘（LVGL fbdev + evdev） */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/display/fb/lv_linux_fbdev.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"

#include "ui.h"

#define DEFAULT_FB "/dev/fb0"
#define DEFAULT_AP "/dev/ap3216c"
#define DEFAULT_ICM "/dev/icm20608"
#define DEFAULT_HOME_MS 1000U
#define DEFAULT_DETAIL_MS 500U

/* LVGL LV_TICK_CUSTOM 回调 */
uint32_t custom_tick_get(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static const char *envOr(const char *name, const char *fallback)
{
	const char *v = getenv(name);

	if (v && *v)
		return v;
	return fallback;
}

static unsigned envUInt(const char *name, unsigned fallback)
{
	const char *v = getenv(name);
	char *end = NULL;
	unsigned long n;

	if (!v || !*v)
		return fallback;
	n = strtoul(v, &end, 10);
	if (end == v || *end != '\0' || n == 0 || n > 60000)
		return fallback;
	return (unsigned)n;
}

/* 在 /proc/bus/input/devices 中找名称含 Goodix 的 event 节点 */
static int findGoodixEvent(char *out, size_t outSize)
{
	FILE *fp;
	char line[256];
	int matched = 0;

	if (!out || outSize < 32)
		return -1;
	fp = fopen("/proc/bus/input/devices", "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "N: Name=", 8) == 0) {
			matched = (strstr(line, "Goodix") != NULL);
		} else if (matched && strncmp(line, "H: Handlers=", 12) == 0) {
			const char *p = strstr(line, "event");
			char name[64];

			if (p && sscanf(p, "%63s", name) == 1) {
				snprintf(out, outSize, "/dev/input/%s", name);
				fclose(fp);
				return 0;
			}
		}
	}
	fclose(fp);
	return -1;
}

/* 关闭帧缓冲控制台，避免登录/内核日志在 LCD 上叠出黑色字符块 */
static void disableFbConsole(void)
{
	DIR *dir;
	struct dirent *ent;
	char path[128];
	char name[64];
	int fd;
	FILE *fp;

	dir = opendir("/sys/class/vtconsole");
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "vtcon", 5) != 0)
				continue;
			snprintf(path, sizeof(path),
				 "/sys/class/vtconsole/%s/name", ent->d_name);
			fp = fopen(path, "r");
			if (!fp)
				continue;
			if (!fgets(name, sizeof(name), fp)) {
				fclose(fp);
				continue;
			}
			fclose(fp);
			/* fbcon 名称通常含 "frame buffer" */
			if (strstr(name, "frame") == NULL)
				continue;
			snprintf(path, sizeof(path),
				 "/sys/class/vtconsole/%s/bind", ent->d_name);
			fp = fopen(path, "w");
			if (fp) {
				fputs("0\n", fp);
				fclose(fp);
				syslog(LOG_INFO, "unbound %s (%s)", ent->d_name,
				       name);
			}
		}
		closedir(dir);
	}

	fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
	if (fd >= 0) {
		(void)ioctl(fd, KDSETMODE, KD_GRAPHICS);
		close(fd);
	}
	fd = open("/dev/tty1", O_RDWR | O_NOCTTY);
	if (fd >= 0) {
		(void)ioctl(fd, KDSETMODE, KD_GRAPHICS);
		close(fd);
	}
}

static int initDisplay(const char *fbDev)
{
	lv_display_t *disp;

	disableFbConsole();
	disp = lv_linux_fbdev_create();
	if (!disp) {
		syslog(LOG_ERR, "lv_linux_fbdev_create failed");
		return -1;
	}
	lv_linux_fbdev_set_file(disp, fbDev);
	return 0;
}

static int initTouch(const char *touchDev)
{
	char autoPath[128];
	const char *path = touchDev;
	lv_indev_t *indev;

	if (!path || !*path) {
		if (findGoodixEvent(autoPath, sizeof(autoPath)) != 0) {
			syslog(LOG_WARNING, "Goodix touch not found");
			return -1;
		}
		path = autoPath;
	}
	indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, path);
	if (!indev) {
		syslog(LOG_WARNING, "lv_evdev_create %s failed", path);
		return -1;
	}
	syslog(LOG_INFO, "touch %s", path);
	return 0;
}

int main(void)
{
	struct DashboardCfg cfg;
	const char *fbDev;
	const char *touchDev;

	openlog("sensor-dashboard", LOG_PID, LOG_DAEMON);
	fbDev = envOr("SENSOR_DASHBOARD_FB", DEFAULT_FB);
	touchDev = getenv("SENSOR_DASHBOARD_TOUCH_DEV");
	cfg.apDev = envOr("SENSOR_DASHBOARD_AP_DEV", DEFAULT_AP);
	cfg.icmDev = envOr("SENSOR_DASHBOARD_ICM_DEV", DEFAULT_ICM);
	cfg.homeIntervalMs =
		envUInt("SENSOR_DASHBOARD_HOME_MS", DEFAULT_HOME_MS);
	cfg.detailIntervalMs =
		envUInt("SENSOR_DASHBOARD_DETAIL_MS", DEFAULT_DETAIL_MS);

	if (access(fbDev, R_OK | W_OK) != 0) {
		syslog(LOG_ERR, "fb %s: %s", fbDev, strerror(errno));
		closelog();
		return 1;
	}

	lv_init();
	if (initDisplay(fbDev) != 0) {
		closelog();
		return 1;
	}
	(void)initTouch(touchDev && *touchDev ? touchDev : NULL);
	uiInit(&cfg);
	syslog(LOG_INFO, "started fb=%s", fbDev);

	for (;;) {
		uint32_t waitMs = lv_timer_handler();

		if (waitMs > 50)
			waitMs = 50;
		usleep(waitMs * 1000U);
	}
}
