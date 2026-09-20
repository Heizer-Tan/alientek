/* SPDX-License-Identifier: MIT */
#ifndef SENSOR_DASHBOARD_SENSORS_H
#define SENSOR_DASHBOARD_SENSORS_H

struct ApSample {
	unsigned ir;
	unsigned als;
	unsigned ps;
	int valid;
};

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
	int valid;
};

/* 解析 AP3216C 文本行；0 成功 */
int parseApSample(const char *line, struct ApSample *out);

/* 解析 ICM20608 文本行；0 成功 */
int parseIcmSample(const char *line, struct IcmSample *out);

#ifndef SENSOR_DASHBOARD_TEST_PARSE
/* 从设备路径读一次；失败时 out->valid=0，返回非 0 */
int readApSample(const char *devPath, struct ApSample *out);
int readIcmSample(const char *devPath, struct IcmSample *out);
#endif

#endif
