/* SPDX-License-Identifier: MIT */
/* 传感器设备文本解析与读取 */

#include "sensors.h"

#include <stdio.h>
#include <string.h>

#ifndef SENSOR_DASHBOARD_TEST_PARSE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

int parseApSample(const char *line, struct ApSample *out)
{
	unsigned ir = 0;
	unsigned als = 0;
	unsigned ps = 0;

	if (!line || !out)
		return -1;
	memset(out, 0, sizeof(*out));
	if (sscanf(line, "ir=%u als=%u ps=%u", &ir, &als, &ps) != 3)
		return -1;
	out->ir = ir;
	out->als = als;
	out->ps = ps;
	out->valid = 1;
	return 0;
}

int parseIcmSample(const char *line, struct IcmSample *out)
{
	struct IcmSample s;

	if (!line || !out)
		return -1;
	memset(out, 0, sizeof(*out));
	memset(&s, 0, sizeof(s));
	if (sscanf(line,
		   "ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp_raw=%d "
		   "ax_g=%lf ay_g=%lf az_g=%lf gx_dps=%lf gy_dps=%lf "
		   "gz_dps=%lf temp_c=%lf",
		   &s.ax, &s.ay, &s.az, &s.gx, &s.gy, &s.gz, &s.temp_raw,
		   &s.ax_g, &s.ay_g, &s.az_g, &s.gx_dps, &s.gy_dps, &s.gz_dps,
		   &s.temp_c) != 14)
		return -1;
	s.valid = 1;
	*out = s;
	return 0;
}

#ifndef SENSOR_DASHBOARD_TEST_PARSE
static int readDevLine(const char *devPath, char *buf, size_t bufSize)
{
	int fd;
	ssize_t n;

	if (!devPath || !buf || bufSize < 2)
		return -1;
	fd = open(devPath, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, bufSize - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return 0;
}

int readApSample(const char *devPath, struct ApSample *out)
{
	char buf[128];

	if (!out)
		return -1;
	memset(out, 0, sizeof(*out));
	if (readDevLine(devPath, buf, sizeof(buf)) != 0)
		return -1;
	return parseApSample(buf, out);
}

int readIcmSample(const char *devPath, struct IcmSample *out)
{
	char buf[256];

	if (!out)
		return -1;
	memset(out, 0, sizeof(*out));
	if (readDevLine(devPath, buf, sizeof(buf)) != 0)
		return -1;
	return parseIcmSample(buf, out);
}
#endif
