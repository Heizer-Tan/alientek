/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>

struct SysInfo {
	QString hostname;
	double uptimeSec = 0;
	unsigned long memTotalKb = 0;
	unsigned long memAvailKb = 0;
	double load1 = 0;
	QString ipv4;
	bool ok = false;
};

/* iface 默认 eth0；失败字段可为空，ok 表示至少读到部分信息 */
bool readSysInfo(const QString &iface, SysInfo *out);
