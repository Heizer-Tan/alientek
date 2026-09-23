/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>

/* LED/蜂鸣器 sysfs 控制；sysfsRoot 默认 /sys/class/leds */

struct LedStatus {
	QString trigger;
	int brightness = 0;
	int maxBrightness = 1;
	bool ok = false;
};

QString ledSysfsDir(const QString &sysfsRoot, const QString &name);

bool ledReadStatus(const QString &sysfsRoot, const QString &name, LedStatus *out);

/* 先 trigger=none，再写 brightness（max 或 0） */
bool ledSetManual(const QString &sysfsRoot, const QString &name, bool on);

/* 仅 LED：trigger=heartbeat */
bool ledRestoreHeartbeat(const QString &sysfsRoot, const QString &name);
