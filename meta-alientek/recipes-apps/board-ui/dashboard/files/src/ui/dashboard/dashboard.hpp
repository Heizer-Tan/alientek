/* SPDX-License-Identifier: MIT */
#pragma once

#include "hw/leds/led_class_controller.hpp"
#include "ui/style/pixel_widgets.hpp"

#include <QProcess>
#include <QString>
#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class KeyWatcher;

/* 板级控制台：主页可滚动卡片 + 各详情页 */
class Dashboard final : public QWidget {
	Q_OBJECT
public:
	Dashboard(const QString &apDev, const QString &icmName, const QString &iface,
		  const QString &ledName, const QString &beepName, int homeMs,
		  int detailMs, QWidget *parent = nullptr);

private slots:
	void onHomeTick();
	void onClockTick();
	void onDetailTick();
	void openAp();
	void openIcm();
	void openSys();
	void openLeds();
	void openKeys();
	void openOta();
	void openFailover();
	void backHome();
	void onLedOn();
	void onLedOff();
	void onLedHeartbeat();
	void onBeepOn();
	void onBeepOff();
	void onKeyPressed(bool pressed);
	void onOtaPullLatest();
	void onOtaPullFinished(int exitCode, QProcess::ExitStatus status);
	void onOtaPullStdout();
	void onFailoverArm();
	void onFailoverSoft();
	void onFailoverSoftUndo();
	void onFailoverCmdFinished(int exitCode, QProcess::ExitStatus status);

private:
	enum Page : int {
		PageHome = 0,
		PageAp,
		PageIcm,
		PageSys,
		PageLeds,
		PageKeys,
		PageOta,
		PageFailover,
	};

	QWidget *buildHomePage();
	QWidget *buildApPage();
	QWidget *buildIcmPage();
	QWidget *buildSysPage();
	QWidget *buildLedsPage();
	QWidget *buildKeysPage();
	QWidget *buildOtaPage();
	QWidget *buildFailoverPage();
	PixelShell *makeHomeCard(PixelGlyph glyph, const QString &title,
				 QLabel **summaryOut, const char *accent);
	void applyDarkStyle(QWidget *w);
	void refreshApLabels();
	void refreshIcmLabels();
	void refreshSysLabels();
	void refreshLedLabels();
	void refreshOtaLabels();
	void refreshFailoverLabels();
	void setPageTimers(int pageIndex);
	void setOtaProgressVisible(bool visible);
	void applyOtaProgressLine(const QString &line);
	void runFailoverCmd(const QStringList &args, const QString &busyMsg);
	void setFailoverButtonsEnabled(bool enabled);
	bool homeScrollBusy() const;

	QString apDev_;
	QString icmName_;
	QString iface_;
	QString otaPullMsg_;
	QString otaStdoutBuf_;
	QString failoverMsg_;
	int otaProgressHighWater_ = 0;

	std::unique_ptr<LedClassController> leds_;
	QStackedWidget *stack_ = nullptr;
	QScrollArea *homeScroll_ = nullptr;
	QLabel *homeApSummary_ = nullptr;
	QLabel *homeIcmSummary_ = nullptr;
	QLabel *homeSysSummary_ = nullptr;
	QLabel *homeLedSummary_ = nullptr;
	QLabel *homeKeySummary_ = nullptr;
	QLabel *homeOtaSummary_ = nullptr;
	QLabel *homeFailoverSummary_ = nullptr;
	QLabel *homeHudClock_ = nullptr;
	QLabel *apDetail_ = nullptr;
	QLabel *icmDetail_ = nullptr;
	QLabel *sysDetail_ = nullptr;
	QLabel *ledDetail_ = nullptr;
	QLabel *keyDetail_ = nullptr;
	QLabel *otaDetail_ = nullptr;
	QLabel *failoverDetail_ = nullptr;
	QLabel *otaProgressLabel_ = nullptr;
	PixelProgressBar *otaProgressBar_ = nullptr;
	QPushButton *otaPullBtn_ = nullptr;
	QPushButton *failoverArmBtn_ = nullptr;
	QPushButton *failoverSoftBtn_ = nullptr;
	QPushButton *failoverSoftUndoBtn_ = nullptr;
	QProcess *otaPullProc_ = nullptr;
	QProcess *failoverProc_ = nullptr;
	QTimer *homeTimer_ = nullptr;
	QTimer *clockTimer_ = nullptr;
	QTimer *detailTimer_ = nullptr;
	KeyWatcher *keys_ = nullptr;
};
