/* SPDX-License-Identifier: MIT */
/* 传感器设备文本解析与读取 */

#include "sensors.hpp"

#include <cstdio>
#include <cstring>

#ifndef SENSOR_DASHBOARD_TEST_PARSE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
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

bool parseIcmSample(const char *line, IcmSample *out)
{
	IcmSample s{};

	if (!line || !out)
		return false;
	*out = IcmSample{};
	if (std::sscanf(line,
			"ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp_raw=%d "
			"ax_g=%lf ay_g=%lf az_g=%lf gx_dps=%lf gy_dps=%lf "
			"gz_dps=%lf temp_c=%lf",
			&s.ax, &s.ay, &s.az, &s.gx, &s.gy, &s.gz, &s.temp_raw,
			&s.ax_g, &s.ay_g, &s.az_g, &s.gx_dps, &s.gy_dps, &s.gz_dps,
			&s.temp_c) != 14)
		return false;
	s.valid = true;
	*out = s;
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

bool readIcmSample(const char *devPath, IcmSample *out)
{
	char buf[256];

	if (!out)
		return false;
	*out = IcmSample{};
	if (!readDevLine(devPath, buf, sizeof(buf)))
		return false;
	return parseIcmSample(buf, out);
}
#endif
