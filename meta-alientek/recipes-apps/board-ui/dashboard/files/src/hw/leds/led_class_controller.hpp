/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>
#include <memory>

/* LED/蜂鸣器只读快照（sysfs：trigger / brightness；chardev：无 trigger） */
struct LedStatus {
	QString trigger;
	int brightness = 0;
	int maxBrightness = 1;
	bool ok = false;
};

/*
 * 板载灯控：LED 优先 /dev/alientek-led（chardev），否则 /sys/class/leds/<name>；
 * 蜂鸣器始终走 LED class（gpio-leds beep）。
 */
class LedClassController final {
public:
	explicit LedClassController(
		QString sysfsRoot = QStringLiteral("/sys/class/leds"));
	~LedClassController();

	LedClassController(const LedClassController &) = delete;
	LedClassController &operator=(const LedClassController &) = delete;

	/* 绑定 LED：若 charDev 存在则用字符设备，否则用 sysfs 节点名 */
	void bindLed(const QString &sysfsName,
		     const QString &charDev = QStringLiteral("/dev/alientek-led"));
	void bindBeep(const QString &name);

	QString ledName() const;
	QString beepName() const;
	QString sysfsRoot() const { return sysfsRoot_; }
	/* true：当前走 /dev；无 heartbeat trigger */
	bool usesCharLed() const;
	bool supportsHeartbeat() const { return !usesCharLed(); }

	LedStatus ledStatus() const;
	LedStatus beepStatus() const;

	bool ledOn();
	bool ledOff();
	bool ledHeartbeat();
	bool beepOn();
	bool beepOff();

private:
	class Device;

	QString sysfsRoot_;
	QString charLedPath_;
	std::unique_ptr<Device> led_;
	std::unique_ptr<Device> beep_;
};
