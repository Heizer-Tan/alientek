/* SPDX-License-Identifier: MIT */
/* 系统信息：/proc + getifaddrs IPv4（不依赖 Qt Network） */

#include "sysinfo.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <cstring>

namespace {

QString readFileTrim(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	return QString::fromUtf8(f.readAll()).trimmed();
}

unsigned long parseMemKb(const QString &meminfo, const char *key)
{
	const QString lineKey = QString::fromLatin1(key);
	const QStringList lines = meminfo.split('\n');
	for (const QString &line : lines) {
		if (!line.startsWith(lineKey))
			continue;
		const QStringList parts =
			line.split(QRegularExpression(QStringLiteral("\\s+")),
				   Qt::SkipEmptyParts);
		if (parts.size() >= 2)
			return parts[1].toULong();
	}
	return 0;
}

QString ipv4ForIface(const QString &iface)
{
	struct ifaddrs *list = nullptr;
	if (getifaddrs(&list) != 0)
		return QString();
	QString found;
	const QByteArray want = iface.toUtf8();
	for (struct ifaddrs *ifa = list; ifa != nullptr; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (std::strcmp(ifa->ifa_name, want.constData()) != 0)
			continue;
		char buf[INET_ADDRSTRLEN];
		const auto *in =
			reinterpret_cast<sockaddr_in *>(ifa->ifa_addr);
		if (!inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf)))
			continue;
		found = QString::fromLatin1(buf);
		break;
	}
	freeifaddrs(list);
	return found;
}

} // namespace

bool readSysInfo(const QString &iface, SysInfo *out)
{
	if (!out)
		return false;
	*out = SysInfo{};

	out->hostname = readFileTrim(QStringLiteral("/etc/hostname"));
	if (out->hostname.isEmpty())
		out->hostname = QStringLiteral("unknown");

	const QString up = readFileTrim(QStringLiteral("/proc/uptime"));
	if (!up.isEmpty())
		out->uptimeSec = up.section(' ', 0, 0).toDouble();

	const QString mem = readFileTrim(QStringLiteral("/proc/meminfo"));
	if (!mem.isEmpty()) {
		out->memTotalKb = parseMemKb(mem, "MemTotal:");
		out->memAvailKb = parseMemKb(mem, "MemAvailable:");
	}

	const QString load = readFileTrim(QStringLiteral("/proc/loadavg"));
	if (!load.isEmpty())
		out->load1 = load.section(' ', 0, 0).toDouble();

	const QString ifName = iface.isEmpty() ? QStringLiteral("eth0") : iface;
	out->ipv4 = ipv4ForIface(ifName);
	out->ok = true;
	return true;
}
