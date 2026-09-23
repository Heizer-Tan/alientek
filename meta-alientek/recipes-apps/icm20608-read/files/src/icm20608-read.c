/* SPDX-License-Identifier: MIT */
/* icm20608-read：读 IIO sysfs，支持单次或循环打印 */

#include "iio-icm.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ICM20608_LOOP_DELAY_US 500000U
#define ICM20608_DEFAULT_NAME "icm20608"

static volatile sig_atomic_t gStopRequested = 0;

static void handleSignal(int sig)
{
	(void)sig;
	gStopRequested = 1;
}

static void printUsage(const char *progName)
{
	fprintf(stderr, "用法: %s [-w]\n", progName);
	fprintf(stderr, "  -w  持续循环读取\n");
	fprintf(stderr, "环境变量 ICM20608_IIO_NAME 默认 icm20608\n");
}

static int printSample(const char *sysfsDir)
{
	struct IcmIioSample s;

	if (iioIcmReadSample(sysfsDir, &s) != 0) {
		fprintf(stderr, "icm20608-read: read IIO sample failed: %s\n",
			strerror(errno));
		return 1;
	}
	printf("ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp_raw=%d "
	       "ax_g=%.4f ay_g=%.4f az_g=%.4f gx_dps=%.3f gy_dps=%.3f "
	       "gz_dps=%.3f temp_c=%.2f\n",
	       s.ax, s.ay, s.az, s.gx, s.gy, s.gz, s.temp_raw, s.ax_g, s.ay_g,
	       s.az_g, s.gx_dps, s.gy_dps, s.gz_dps, s.temp_c);
	return 0;
}

int main(int argc, char *argv[])
{
	int loopMode = 0;
	struct sigaction sa;
	const char *iioName;
	char *sysfsDir;

	if (argc > 2) {
		printUsage(argv[0]);
		return 1;
	}
	if (argc == 2) {
		if (strcmp(argv[1], "-w") != 0) {
			printUsage(argv[0]);
			return 1;
		}
		loopMode = 1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	iioName = getenv("ICM20608_IIO_NAME");
	if (iioName == NULL || iioName[0] == '\0')
		iioName = ICM20608_DEFAULT_NAME;
	sysfsDir = iioIcmFindSysfsDir(iioName);
	if (sysfsDir == NULL) {
		fprintf(stderr, "icm20608-read: IIO device \"%s\" not found\n",
			iioName);
		return 1;
	}

	if (loopMode != 0) {
		while (!gStopRequested) {
			if (printSample(sysfsDir) != 0) {
				free(sysfsDir);
				return 1;
			}
			usleep(ICM20608_LOOP_DELAY_US);
		}
		free(sysfsDir);
		return 0;
	}

	if (printSample(sysfsDir) != 0) {
		free(sysfsDir);
		return 1;
	}
	free(sysfsDir);
	return 0;
}
