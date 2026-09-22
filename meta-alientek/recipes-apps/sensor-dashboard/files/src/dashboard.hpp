/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTimer;

/* LCD 主页 / 光感详情 / 六轴详情 */
class Dashboard final : public QWidget {
	Q_OBJECT
public:
	Dashboard(const QString &apDev, const QString &icmDev, int homeMs,
		  int detailMs, QWidget *parent = nullptr);

private slots:
	void onHomeTick();
	void onDetailTick();
	void openAp();
	void openIcm();
	void backHome();

private:
	QWidget *buildHomePage();
	QWidget *buildApPage();
	QWidget *buildIcmPage();
	void applyDarkStyle(QWidget *w);
	void refreshApLabels();
	void refreshIcmLabels();
	void setPageTimers(int pageIndex);

	QString apDev_;
	QString icmDev_;
	QStackedWidget *stack_ = nullptr;
	QLabel *homeApSummary_ = nullptr;
	QLabel *homeIcmSummary_ = nullptr;
	QLabel *apDetail_ = nullptr;
	QLabel *icmDetail_ = nullptr;
	QTimer *homeTimer_ = nullptr;
	QTimer *detailTimer_ = nullptr;
};
