/* SPDX-License-Identifier: MIT */
#pragma once

struct ApSample {
	unsigned ir = 0;
	unsigned als = 0;
	unsigned ps = 0;
	bool valid = false;
};

struct IcmSample {
	int ax = 0;
	int ay = 0;
	int az = 0;
	int gx = 0;
	int gy = 0;
	int gz = 0;
	int temp_raw = 0;
	double ax_g = 0;
	double ay_g = 0;
	double az_g = 0;
	double gx_dps = 0;
	double gy_dps = 0;
	double gz_dps = 0;
	double temp_c = 0;
	bool valid = false;
};

/* 解析 AP 设备文本行；成功返回 true */
bool parseApSample(const char *line, ApSample *out);

#ifndef DASHBOARD_TEST_PARSE
bool readApSample(const char *devPath, ApSample *out);
/* iioName：IIO name（默认 icm20608），非 /dev 路径 */
bool readIcmSample(const char *iioName, IcmSample *out);
#endif
