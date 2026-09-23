/* SPDX-License-Identifier: MIT */
/* LED/beep：/sys/class/leds/<name>/{trigger,brightness,max_brightness} */

#include "leds.hpp"

#include <QFile>

namespace {

bool writeSysfs(const QString &path, const QString &value)
{
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return false;
	const QByteArray data = value.toUtf8();
	return f.write(data) == data.size();
}

QString readSysfsTrim(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	return QString::fromUtf8(f.readAll()).trimmed();
}

/* trigger 文件可能含 [active] 列表，取方括号内或整行第一个 token */
QString parseActiveTrigger(const QString &raw)
{
	const int l = raw.indexOf('[');
	const int r = raw.indexOf(']');
	if (l >= 0 && r > l)
		return raw.mid(l + 1, r - l - 1).trimmed();
	return raw.section(' ', 0, 0).trimmed();
}

} // namespace

QString ledSysfsDir(const QString &sysfsRoot, const QString &name)
{
	return sysfsRoot + QLatin1Char('/') + name;
}

bool ledReadStatus(const QString &sysfsRoot, const QString &name, LedStatus *out)
{
	if (!out || name.isEmpty())
		return false;
	*out = LedStatus{};
	const QString dir = ledSysfsDir(sysfsRoot, name);
	const QString trig = readSysfsTrim(dir + QStringLiteral("/trigger"));
	const QString bri = readSysfsTrim(dir + QStringLiteral("/brightness"));
	const QString maxb = readSysfsTrim(dir + QStringLiteral("/max_brightness"));
	if (trig.isEmpty() && bri.isEmpty())
		return false;
	out->trigger = parseActiveTrigger(trig);
	out->brightness = bri.toInt();
	out->maxBrightness = maxb.isEmpty() ? 1 : maxb.toInt();
	if (out->maxBrightness <= 0)
		out->maxBrightness = 1;
	out->ok = true;
	return true;
}

bool ledSetManual(const QString &sysfsRoot, const QString &name, bool on)
{
	LedStatus st;
	if (!ledReadStatus(sysfsRoot, name, &st))
		return false;
	const QString dir = ledSysfsDir(sysfsRoot, name);
	if (!writeSysfs(dir + QStringLiteral("/trigger"), QStringLiteral("none")))
		return false;
	const int val = on ? st.maxBrightness : 0;
	return writeSysfs(dir + QStringLiteral("/brightness"), QString::number(val));
}

bool ledRestoreHeartbeat(const QString &sysfsRoot, const QString &name)
{
	const QString dir = ledSysfsDir(sysfsRoot, name);
	return writeSysfs(dir + QStringLiteral("/trigger"), QStringLiteral("heartbeat"));
}
