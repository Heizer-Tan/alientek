/* SPDX-License-Identifier: MIT */
/* ICM20608：IIO sysfs 发现与读数换算 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "iio-icm.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IIO_BUS_DIR "/sys/bus/iio/devices"

static int readSysfsDouble(const char *dir, const char *name, double *out)
{
	char path[512];
	FILE *fp;
	int n;

	if (dir == NULL || name == NULL || out == NULL)
		return -1;
	n = snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	if (fscanf(fp, "%lf", out) != 1) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

static int readSysfsInt(const char *dir, const char *name, int *out)
{
	char path[512];
	FILE *fp;
	int n;

	if (dir == NULL || name == NULL || out == NULL)
		return -1;
	n = snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	if (fscanf(fp, "%d", out) != 1) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

static int nameFileMatches(const char *dirPath, const char *want)
{
	char path[512];
	char got[128];
	FILE *fp;
	size_t len;

	snprintf(path, sizeof(path), "%s/name", dirPath);
	fp = fopen(path, "r");
	if (fp == NULL)
		return 0;
	if (fgets(got, sizeof(got), fp) == NULL) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	len = strlen(got);
	while (len > 0 && (got[len - 1] == '\n' || got[len - 1] == '\r'))
		got[--len] = '\0';
	return strcmp(got, want) == 0;
}

char *iioIcmFindSysfsDir(const char *name)
{
	DIR *dir;
	struct dirent *ent;
	char path[512];
	char *found = NULL;

	if (name == NULL || name[0] == '\0')
		return NULL;
	dir = opendir(IIO_BUS_DIR);
	if (dir == NULL)
		return NULL;
	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, "iio:device", 9) != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", IIO_BUS_DIR, ent->d_name);
		if (!nameFileMatches(path, name))
			continue;
		found = strdup(path);
		break;
	}
	closedir(dir);
	return found;
}

void iioIcmConvert(struct IcmIioSample *s, double accel_scale, double gyro_scale,
		   double temp_scale, double temp_offset)
{
	if (s == NULL)
		return;
	s->ax_g = (double)s->ax * accel_scale;
	s->ay_g = (double)s->ay * accel_scale;
	s->az_g = (double)s->az * accel_scale;
	s->gx_dps = (double)s->gx * gyro_scale;
	s->gy_dps = (double)s->gy * gyro_scale;
	s->gz_dps = (double)s->gz * gyro_scale;
	/* IIO：processed = (raw + offset) * scale */
	s->temp_c = ((double)s->temp_raw + temp_offset) * temp_scale;
	s->valid = 1;
}

int iioIcmReadSample(const char *sysfsDir, struct IcmIioSample *out)
{
	double accel_scale = 0.0;
	double gyro_scale = 0.0;
	double temp_scale = 0.0;
	double temp_offset = 0.0;

	if (sysfsDir == NULL || out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));

	if (readSysfsInt(sysfsDir, "in_accel_x_raw", &out->ax) != 0 ||
	    readSysfsInt(sysfsDir, "in_accel_y_raw", &out->ay) != 0 ||
	    readSysfsInt(sysfsDir, "in_accel_z_raw", &out->az) != 0 ||
	    readSysfsInt(sysfsDir, "in_anglvel_x_raw", &out->gx) != 0 ||
	    readSysfsInt(sysfsDir, "in_anglvel_y_raw", &out->gy) != 0 ||
	    readSysfsInt(sysfsDir, "in_anglvel_z_raw", &out->gz) != 0 ||
	    readSysfsInt(sysfsDir, "in_temp_raw", &out->temp_raw) != 0)
		return -1;

	if (readSysfsDouble(sysfsDir, "in_accel_scale", &accel_scale) != 0 ||
	    readSysfsDouble(sysfsDir, "in_anglvel_scale", &gyro_scale) != 0 ||
	    readSysfsDouble(sysfsDir, "in_temp_scale", &temp_scale) != 0 ||
	    readSysfsDouble(sysfsDir, "in_temp_offset", &temp_offset) != 0)
		return -1;

	iioIcmConvert(out, accel_scale, gyro_scale, temp_scale, temp_offset);
	return 0;
}
