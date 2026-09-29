/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>
#include <memory>

/* LED/蜂鸣器只读快照（sysfs：trigger / brightness） */
struct LedStatus {
	QString trigger;
	int brightness = 0;
	int maxBrightness = 1;
	bool ok = false;
};

/*
 * Linux LED class 控制器（灯 + 蜂鸣器节点）：对外唯一入口。
 * 内部用 Device 封装单个 /sys/class/leds/<name> 节点。
 */
class LedClassController final {
public:
	explicit LedClassController(
		QString sysfsRoot = QStringLiteral("/sys/class/leds"));
	~LedClassController();

	LedClassController(const LedClassController &) = delete;
	LedClassController &operator=(const LedClassController &) = delete;

	/* 绑定 LED / 蜂鸣器节点名（可重复调用以更换） */
	void bindLed(const QString &name);
	void bindBeep(const QString &name);

	QString ledName() const;
	QString beepName() const;
	QString sysfsRoot() const { return sysfsRoot_; }

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
	std::unique_ptr<Device> led_;
	std::unique_ptr<Device> beep_;
};
