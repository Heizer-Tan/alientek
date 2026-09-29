/* SPDX-License-Identifier: MIT */
/* LED/beep：优先 /dev/alientek-led，回退 /sys/class/leds；beep 始终 sysfs */

#include "led_class_controller.hpp"

#include "hw/sysfs/sysfs_file.hpp"

#include <QFile>

namespace {

/* trigger 文件可能含 [active] 列表，取方括号内或首个 token */
QString parseActiveTrigger(const QString &raw)
{
	const int l = raw.indexOf('[');
	const int r = raw.indexOf(']');
	if (l >= 0 && r > l)
		return raw.mid(l + 1, r - l - 1).trimmed();
	return raw.section(' ', 0, 0).trimmed();
}

/* 读 /dev/alientek-led：期望 "0\n"/"1\n" */
bool charLedReadOn(const QString &path, bool *onOut)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	const QByteArray raw = f.read(8);
	if (raw.isEmpty())
		return false;
	*onOut = (raw[0] == '1');
	return true;
}

bool charLedWriteOn(const QString &path, bool on)
{
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	return f.write(on ? "1\n" : "0\n") > 0;
}

} // namespace

/* 单个 sysfs LED 节点 */
class LedClassController::Device final {
public:
	Device(QString sysfsRoot, QString name)
		: sysfsRoot_(std::move(sysfsRoot)), name_(std::move(name))
	{
	}

	QString name() const { return name_; }

	QString dir() const
	{
		return sysfsRoot_ + QLatin1Char('/') + name_;
	}

	LedStatus status() const
	{
		LedStatus out{};
		if (name_.isEmpty())
			return out;
		const QString d = dir();
		const QString trig =
			SysfsFile(d + QStringLiteral("/trigger")).readTrim();
		const QString bri =
			SysfsFile(d + QStringLiteral("/brightness")).readTrim();
		const QString maxb =
			SysfsFile(d + QStringLiteral("/max_brightness")).readTrim();
		if (trig.isEmpty() && bri.isEmpty())
			return out;
		out.trigger = parseActiveTrigger(trig);
		out.brightness = bri.toInt();
		out.maxBrightness = maxb.isEmpty() ? 1 : maxb.toInt();
		if (out.maxBrightness <= 0)
			out.maxBrightness = 1;
		out.ok = true;
		return out;
	}

	/* 先 trigger=none，再写 brightness（max 或 0） */
	bool setManual(bool on) const
	{
		const LedStatus st = status();
		if (!st.ok)
			return false;
		const QString d = dir();
		if (!SysfsFile(d + QStringLiteral("/trigger"))
			     .write(QStringLiteral("none")))
			return false;
		const int val = on ? st.maxBrightness : 0;
		return SysfsFile(d + QStringLiteral("/brightness"))
			.write(QString::number(val));
	}

	bool setHeartbeat() const
	{
		if (name_.isEmpty())
			return false;
		return SysfsFile(dir() + QStringLiteral("/trigger"))
			.write(QStringLiteral("heartbeat"));
	}

private:
	QString sysfsRoot_;
	QString name_;
};

LedClassController::LedClassController(QString sysfsRoot)
	: sysfsRoot_(std::move(sysfsRoot))
{
}

LedClassController::~LedClassController() = default;

void LedClassController::bindLed(const QString &sysfsName, const QString &charDev)
{
	charLedPath_.clear();
	led_.reset();
	if (!charDev.isEmpty() && QFile::exists(charDev)) {
		charLedPath_ = charDev;
		return;
	}
	led_ = std::make_unique<Device>(sysfsRoot_, sysfsName);
}

void LedClassController::bindBeep(const QString &name)
{
	beep_ = std::make_unique<Device>(sysfsRoot_, name);
}

bool LedClassController::usesCharLed() const
{
	return !charLedPath_.isEmpty();
}

QString LedClassController::ledName() const
{
	if (usesCharLed())
		return charLedPath_;
	return led_ ? led_->name() : QString();
}

QString LedClassController::beepName() const
{
	return beep_ ? beep_->name() : QString();
}

LedStatus LedClassController::ledStatus() const
{
	if (usesCharLed()) {
		LedStatus out{};
		bool on = false;
		if (!charLedReadOn(charLedPath_, &on))
			return out;
		out.trigger = QStringLiteral("chardev");
		out.brightness = on ? 1 : 0;
		out.maxBrightness = 1;
		out.ok = true;
		return out;
	}
	return led_ ? led_->status() : LedStatus{};
}

LedStatus LedClassController::beepStatus() const
{
	return beep_ ? beep_->status() : LedStatus{};
}

bool LedClassController::ledOn()
{
	if (usesCharLed())
		return charLedWriteOn(charLedPath_, true);
	return led_ && led_->setManual(true);
}

bool LedClassController::ledOff()
{
	if (usesCharLed())
		return charLedWriteOn(charLedPath_, false);
	return led_ && led_->setManual(false);
}

bool LedClassController::ledHeartbeat()
{
	if (usesCharLed())
		return false;
	return led_ && led_->setHeartbeat();
}

bool LedClassController::beepOn()
{
	return beep_ && beep_->setManual(true);
}

bool LedClassController::beepOff()
{
	return beep_ && beep_->setManual(false);
}
