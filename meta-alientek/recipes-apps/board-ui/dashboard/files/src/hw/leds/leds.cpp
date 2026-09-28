/* SPDX-License-Identifier: MIT */
/* LED/beep：LedController 管理 /sys/class/leds/<name> 节点 */

#include "leds.hpp"

#include "hw/sysfs/sysfs_file.hpp"

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

} // namespace

/* 单个 sysfs LED 节点 */
class LedController::Device final {
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
		const QString trig = SysfsFile(d + QStringLiteral("/trigger")).readTrim();
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

LedController::LedController(QString sysfsRoot)
	: sysfsRoot_(std::move(sysfsRoot))
{
}

LedController::~LedController() = default;

void LedController::bindLed(const QString &name)
{
	led_ = std::make_unique<Device>(sysfsRoot_, name);
}

void LedController::bindBeep(const QString &name)
{
	beep_ = std::make_unique<Device>(sysfsRoot_, name);
}

QString LedController::ledName() const
{
	return led_ ? led_->name() : QString();
}

QString LedController::beepName() const
{
	return beep_ ? beep_->name() : QString();
}

LedStatus LedController::ledStatus() const
{
	return led_ ? led_->status() : LedStatus{};
}

LedStatus LedController::beepStatus() const
{
	return beep_ ? beep_->status() : LedStatus{};
}

bool LedController::ledOn()
{
	return led_ && led_->setManual(true);
}

bool LedController::ledOff()
{
	return led_ && led_->setManual(false);
}

bool LedController::ledHeartbeat()
{
	return led_ && led_->setHeartbeat();
}

bool LedController::beepOn()
{
	return beep_ && beep_->setManual(true);
}

bool LedController::beepOff()
{
	return beep_ && beep_->setManual(false);
}
