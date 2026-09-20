/* SPDX-License-Identifier: MIT */
#ifndef SENSOR_DASHBOARD_UI_H
#define SENSOR_DASHBOARD_UI_H

#include "sensors.h"

struct DashboardCfg {
	const char *apDev;
	const char *icmDev;
	unsigned homeIntervalMs;
	unsigned detailIntervalMs;
};

/* 创建全部页面；之后由 timer 刷新 */
void uiInit(const struct DashboardCfg *cfg);

#endif
