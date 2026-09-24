/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class KeyWatcher;

/* 板级控制台：主页 2×3 + 各详情页 */
class Dashboard final : public QWidget {
	Q_OBJECT
public:
	Dashboard(const QString &apDev, const QString &icmName, const QString &iface,
		  const QString &ledName, const QString &beepName, int homeMs,
		  int detailMs, QWidget *parent = nullptr);

private slots:
	void onHomeTick();
	void onDetailTick();
	void openAp();
	void openIcm();
	void openSys();
	void openLeds();
	void openKeys();
	void openOta();
	void backHome();
	void onLedOn();
	void onLedOff();
	void onLedHeartbeat();
	void onBeepOn();
	void onBeepOff();
	void onKeyPressed(bool pressed);

private:
	enum Page : int {
		PageHome = 0,
		PageAp,
		PageIcm,
		PageSys,
		PageLeds,
		PageKeys,
		PageOta,
	};

	QWidget *buildHomePage();
	QWidget *buildApPage();
	QWidget *buildIcmPage();
	QWidget *buildSysPage();
	QWidget *buildLedsPage();
	QWidget *buildKeysPage();
	QWidget *buildOtaPage();
	QPushButton *makeHomeCard(const QString &title, QLabel **summaryOut,
				  const char *accent);
	void applyDarkStyle(QWidget *w);
	void refreshApLabels();
	void refreshIcmLabels();
	void refreshSysLabels();
	void refreshLedLabels();
	void refreshOtaLabels();
	void setPageTimers(int pageIndex);

	QString apDev_;
	QString icmName_;
	QString iface_;
	QString ledName_;
	QString beepName_;
	QString ledsRoot_;

	QStackedWidget *stack_ = nullptr;
	QLabel *homeApSummary_ = nullptr;
	QLabel *homeIcmSummary_ = nullptr;
	QLabel *homeSysSummary_ = nullptr;
	QLabel *homeLedSummary_ = nullptr;
	QLabel *homeKeySummary_ = nullptr;
	QLabel *homeOtaSummary_ = nullptr;
	QLabel *homeHudClock_ = nullptr;
	QLabel *apDetail_ = nullptr;
	QLabel *icmDetail_ = nullptr;
	QLabel *sysDetail_ = nullptr;
	QLabel *ledDetail_ = nullptr;
	QLabel *keyDetail_ = nullptr;
	QLabel *otaDetail_ = nullptr;
	QTimer *homeTimer_ = nullptr;
	QTimer *detailTimer_ = nullptr;
	KeyWatcher *keys_ = nullptr;
};
