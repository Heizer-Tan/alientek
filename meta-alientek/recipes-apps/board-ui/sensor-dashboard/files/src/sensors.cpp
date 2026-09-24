/* SPDX-License-Identifier: MIT */
/* 传感器读取：AP 仍为 misc 文本；ICM 走 IIO sysfs */

#include "sensors.hpp"

#include <cstdio>
#include <cstring>

#ifndef SENSOR_DASHBOARD_TEST_PARSE
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include "iio-icm.h"
}
#endif

bool parseApSample(const char *line, ApSample *out)
{
	unsigned ir = 0;
	unsigned als = 0;
	unsigned ps = 0;

	if (!line || !out)
		return false;
	*out = ApSample{};
	if (std::sscanf(line, "ir=%u als=%u ps=%u", &ir, &als, &ps) != 3)
		return false;
	out->ir = ir;
	out->als = als;
	out->ps = ps;
	out->valid = true;
	return true;
}

#ifndef SENSOR_DASHBOARD_TEST_PARSE
static bool readDevLine(const char *devPath, char *buf, size_t bufSize)
{
	int fd;
	ssize_t n;

	if (!devPath || !buf || bufSize < 2)
		return false;
	fd = open(devPath, O_RDONLY);
	if (fd < 0)
		return false;
	n = read(fd, buf, bufSize - 1);
	close(fd);
	if (n < 0)
		return false;
	buf[n] = '\0';
	return true;
}

bool readApSample(const char *devPath, ApSample *out)
{
	char buf[128];

	if (!out)
		return false;
	*out = ApSample{};
	if (!readDevLine(devPath, buf, sizeof(buf)))
		return false;
	return parseApSample(buf, out);
}

bool readIcmSample(const char *iioName, IcmSample *out)
{
	char *dir = nullptr;
	struct IcmIioSample raw {};
	const char *name = (iioName && iioName[0]) ? iioName : "icm20608";

	if (!out)
		return false;
	*out = IcmSample{};
	dir = iioIcmFindSysfsDir(name);
	if (!dir)
		return false;
	if (iioIcmReadSample(dir, &raw) != 0) {
		free(dir);
		return false;
	}
	free(dir);
	out->ax = raw.ax;
	out->ay = raw.ay;
	out->az = raw.az;
	out->gx = raw.gx;
	out->gy = raw.gy;
	out->gz = raw.gz;
	out->temp_raw = raw.temp_raw;
	out->ax_g = raw.ax_g;
	out->ay_g = raw.ay_g;
	out->az_g = raw.az_g;
	out->gx_dps = raw.gx_dps;
	out->gy_dps = raw.gy_dps;
	out->gz_dps = raw.gz_dps;
	out->temp_c = raw.temp_c;
	out->valid = raw.valid != 0;
	return out->valid;
}
#endif
