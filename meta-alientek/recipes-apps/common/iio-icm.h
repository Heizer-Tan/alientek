/* SPDX-License-Identifier: MIT */
#ifndef IIO_ICM_H
#define IIO_ICM_H

/*
 * ICM20608 IIO sysfs 辅助：按 name 发现设备并读 accel/gyro/temp。
 * 温度换算遵循 IIO 惯例：processed = (raw + offset) * scale。
 */

/* 与 dashboard C++ IcmSample 字段对齐；valid 用 int 避免 C/C++ ABI 差异 */
struct IcmIioSample {
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

/* 按 name 查找 iio:device*；找到返回 malloc 路径，调用方 free；未找到 NULL */
char *iioIcmFindSysfsDir(const char *name);

/* 从 sysfs 目录读 raw+scale+offset 填满 sample；成功 0，失败 -1 */
int iioIcmReadSample(const char *sysfsDir, struct IcmIioSample *out);

/* 仅换算（单测）：温度用 (raw + offset) * scale */
void iioIcmConvert(struct IcmIioSample *s, double accel_scale, double gyro_scale,
		   double temp_scale, double temp_offset);

#endif
