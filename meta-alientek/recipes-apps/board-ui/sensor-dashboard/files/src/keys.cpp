/* SPDX-License-Identifier: MIT */
/* 按键：复用 inputFindDeviceByName，QSocketNotifier 读 EV_KEY */

#include "keys.hpp"

extern "C" {
#include "input-device.h"
}

#include <QSocketNotifier>

#include <linux/input.h>
#include <unistd.h>

KeyWatcher::KeyWatcher(QObject *parent) : QObject(parent) {}

KeyWatcher::~KeyWatcher()
{
	closeDevice();
}

bool KeyWatcher::isOpen() const
{
	return fd_ >= 0;
}

void KeyWatcher::closeDevice()
{
	if (notifier_) {
		delete notifier_;
		notifier_ = nullptr;
	}
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
}

bool KeyWatcher::openByNameSubstr(const QString &nameSubstr)
{
	closeDevice();
	const QByteArray name = nameSubstr.toUtf8();
	fd_ = inputFindDeviceByName(name.constData(), "sensor-dashboard", 1);
	if (fd_ < 0)
		return false;
	notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
	connect(notifier_, &QSocketNotifier::activated, this, &KeyWatcher::onReadable);
	return true;
}

void KeyWatcher::onReadable()
{
	struct input_event ev;
	while (true) {
		const ssize_t n = ::read(fd_, &ev, sizeof(ev));
		if (n != static_cast<ssize_t>(sizeof(ev)))
			break;
		if (ev.type != EV_KEY)
			continue;
		if (ev.code != KEY_ENTER && ev.code != KEY_1)
			continue;
		const bool down = ev.value != 0;
		if (down == pressed_)
			continue;
		pressed_ = down;
		emit pressedChanged(pressed_);
	}
}
