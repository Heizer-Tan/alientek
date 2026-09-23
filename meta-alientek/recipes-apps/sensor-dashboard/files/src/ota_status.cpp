/* SPDX-License-Identifier: MIT */
/* OTA 只读：fw_printenv + cmdline 提示 */

#include "ota_status.hpp"

#include <QFile>
#include <QProcess>

namespace {

QString fwPrintenv(const char *name)
{
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.start(QStringLiteral("fw_printenv"),
		{QStringLiteral("-n"), QString::fromLatin1(name)});
	if (!p.waitForFinished(1500)) {
		p.kill();
		p.waitForFinished(200);
		return QString();
	}
	if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
		return QString();
	return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString cmdlineRootHint()
{
	QFile f(QStringLiteral("/proc/cmdline"));
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	const QString cmd = QString::fromUtf8(f.readAll());
	if (cmd.contains(QStringLiteral("PARTLABEL=rootfsA")))
		return QStringLiteral("rootfsA");
	if (cmd.contains(QStringLiteral("PARTLABEL=rootfsB")))
		return QStringLiteral("rootfsB");
	return QString();
}

} // namespace

bool readOtaStatus(OtaStatus *out)
{
	if (!out)
		return false;
	*out = OtaStatus{};
	out->activeSlot = fwPrintenv("active_slot");
	out->upgradeAvailable = fwPrintenv("upgrade_available");
	out->cmdlineRootHint = cmdlineRootHint();
	out->ok = !out->activeSlot.isEmpty() || !out->upgradeAvailable.isEmpty() ||
		  !out->cmdlineRootHint.isEmpty();
	return out->ok;
}
