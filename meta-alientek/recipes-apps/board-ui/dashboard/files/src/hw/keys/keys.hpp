/* SPDX-License-Identifier: MIT */
#pragma once

#include <QObject>
#include <QString>

class QSocketNotifier;

/* 监听 gpio-keys（默认名称子串 user-key），报告 KEY 按下/松开 */
class KeyWatcher final : public QObject {
	Q_OBJECT
public:
	explicit KeyWatcher(QObject *parent = nullptr);
	~KeyWatcher() override;

	bool openByNameSubstr(const QString &nameSubstr);
	void closeDevice();
	bool isOpen() const;
	bool pressed() const { return pressed_; }

signals:
	void pressedChanged(bool pressed);

private slots:
	void onReadable();

private:
	int fd_ = -1;
	bool pressed_ = false;
	QSocketNotifier *notifier_ = nullptr;
};
