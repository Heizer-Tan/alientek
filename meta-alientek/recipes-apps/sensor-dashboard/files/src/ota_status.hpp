/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>

struct OtaStatus {
	QString activeSlot;
	QString upgradeAvailable;
	QString cmdlineRootHint;
	bool ok = false;
};

/* 只读 fw_printenv；不调用 fw_setenv */
bool readOtaStatus(OtaStatus *out);
