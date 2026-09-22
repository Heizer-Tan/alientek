/* SPDX-License-Identifier: MIT */
#include "dashboard.hpp"

#include "sensors.hpp"

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr char kBg[] = "#121820";
constexpr char kCard[] = "#1E2A38";
constexpr char kText[] = "#E8EEF4";
constexpr char kMuted[] = "#8FA3B8";
constexpr char kAccent[] = "#3A9BDC";
constexpr char kAccent2[] = "#2BB673";

QString cardStyle(const char *accent)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2; border: 2px solid #2E4055;"
		"  border-radius: 16px; text-align: left; padding: 20px;"
		"}"
		"QPushButton:pressed { background-color: %3; }")
		.arg(QLatin1String(kCard), QLatin1String(kText),
		     QLatin1String(accent));
}

} // namespace

Dashboard::Dashboard(const QString &apDev, const QString &icmDev, int homeMs,
		     int detailMs, QWidget *parent)
	: QWidget(parent), apDev_(apDev), icmDev_(icmDev)
{
	applyDarkStyle(this);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	stack_ = new QStackedWidget(this);
	lay->addWidget(stack_);
	stack_->addWidget(buildHomePage());
	stack_->addWidget(buildApPage());
	stack_->addWidget(buildIcmPage());

	homeTimer_ = new QTimer(this);
	detailTimer_ = new QTimer(this);
	homeTimer_->setInterval(homeMs > 0 ? homeMs : 1000);
	detailTimer_->setInterval(detailMs > 0 ? detailMs : 500);
	connect(homeTimer_, &QTimer::timeout, this, &Dashboard::onHomeTick);
	connect(detailTimer_, &QTimer::timeout, this, &Dashboard::onDetailTick);
	connect(stack_, &QStackedWidget::currentChanged, this,
		&Dashboard::setPageTimers);

	setPageTimers(0);
	onHomeTick();
}

void Dashboard::applyDarkStyle(QWidget *w)
{
	w->setStyleSheet(QStringLiteral("QWidget { background-color: %1; color: %2; }")
				 .arg(QLatin1String(kBg), QLatin1String(kText)));
}

QWidget *Dashboard::buildHomePage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(40, 40, 40, 40);
	lay->setSpacing(24);

	auto *title = new QLabel(QString::fromUtf8("传感器仪表盘"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: bold;"));
	lay->addWidget(title);

	auto *apBtn = new QPushButton;
	apBtn->setMinimumHeight(180);
	apBtn->setStyleSheet(cardStyle(kAccent));
	auto *apCol = new QVBoxLayout(apBtn);
	auto *apTitle = new QLabel(QString::fromUtf8("光感 AP3216C"));
	apTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	apTitle->setStyleSheet(QStringLiteral("font-size: 22px; color: %1;")
				       .arg(QLatin1String(kAccent)));
	homeApSummary_ = new QLabel(QString::fromUtf8("读取中…"));
	homeApSummary_->setAttribute(Qt::WA_TransparentForMouseEvents);
	homeApSummary_->setStyleSheet(
		QStringLiteral("font-size: 18px; color: %1;").arg(QLatin1String(kMuted)));
	auto *apHint = new QLabel(QString::fromUtf8("点击查看详情 ›"));
	apHint->setAttribute(Qt::WA_TransparentForMouseEvents);
	apHint->setStyleSheet(
		QStringLiteral("font-size: 14px; color: %1;").arg(QLatin1String(kMuted)));
	apCol->addWidget(apTitle);
	apCol->addWidget(homeApSummary_);
	apCol->addWidget(apHint);
	connect(apBtn, &QPushButton::clicked, this, &Dashboard::openAp);
	lay->addWidget(apBtn);

	auto *icmBtn = new QPushButton;
	icmBtn->setMinimumHeight(180);
	icmBtn->setStyleSheet(cardStyle(kAccent2));
	auto *icmCol = new QVBoxLayout(icmBtn);
	auto *icmTitle = new QLabel(QString::fromUtf8("六轴 ICM20608"));
	icmTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	icmTitle->setStyleSheet(QStringLiteral("font-size: 22px; color: %1;")
					.arg(QLatin1String(kAccent2)));
	homeIcmSummary_ = new QLabel(QString::fromUtf8("读取中…"));
	homeIcmSummary_->setAttribute(Qt::WA_TransparentForMouseEvents);
	homeIcmSummary_->setStyleSheet(
		QStringLiteral("font-size: 18px; color: %1;").arg(QLatin1String(kMuted)));
	auto *icmHint = new QLabel(QString::fromUtf8("点击查看详情 ›"));
	icmHint->setAttribute(Qt::WA_TransparentForMouseEvents);
	icmHint->setStyleSheet(
		QStringLiteral("font-size: 14px; color: %1;").arg(QLatin1String(kMuted)));
	icmCol->addWidget(icmTitle);
	icmCol->addWidget(homeIcmSummary_);
	icmCol->addWidget(icmHint);
	connect(icmBtn, &QPushButton::clicked, this, &Dashboard::openIcm);
	lay->addWidget(icmBtn);
	lay->addStretch(1);
	return page;
}

QWidget *Dashboard::buildApPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(40, 40, 40, 40);
	auto *title = new QLabel(QString::fromUtf8("光感 AP3216C"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	apDetail_ = new QLabel;
	apDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	apDetail_->setStyleSheet(QStringLiteral("font-size: 20px; font-family: monospace;"));
	auto *back = new QPushButton(QString::fromUtf8("返回"));
	back->setMinimumHeight(64);
	back->setStyleSheet(cardStyle(kAccent));
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);
	lay->addWidget(title);
	lay->addWidget(apDetail_, 1);
	lay->addWidget(back);
	return page;
}

QWidget *Dashboard::buildIcmPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(40, 40, 40, 40);
	auto *title = new QLabel(QString::fromUtf8("六轴 ICM20608"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	icmDetail_ = new QLabel;
	icmDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	icmDetail_->setStyleSheet(QStringLiteral("font-size: 18px; font-family: monospace;"));
	auto *back = new QPushButton(QString::fromUtf8("返回"));
	back->setMinimumHeight(64);
	back->setStyleSheet(cardStyle(kAccent2));
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);
	lay->addWidget(title);
	lay->addWidget(icmDetail_, 1);
	lay->addWidget(back);
	return page;
}

void Dashboard::refreshApLabels()
{
	ApSample s{};
	const bool ok = readApSample(apDev_.toUtf8().constData(), &s);
	if (!ok || !s.valid) {
		homeApSummary_->setText(QString::fromUtf8("读取失败"));
		apDetail_->setText(QString::fromUtf8("读取失败\n设备: %1").arg(apDev_));
		return;
	}
	homeApSummary_->setText(
		QString::fromUtf8("IR=%1  ALS=%2  PS=%3").arg(s.ir).arg(s.als).arg(s.ps));
	apDetail_->setText(QString::fromUtf8(
				   "红外 IR      : %1\n"
				   "环境光 ALS   : %2\n"
				   "接近 PS      : %3\n\n"
				   "设备: %4")
				   .arg(s.ir)
				   .arg(s.als)
				   .arg(s.ps)
				   .arg(apDev_));
}

void Dashboard::refreshIcmLabels()
{
	IcmSample s{};
	const bool ok = readIcmSample(icmDev_.toUtf8().constData(), &s);
	if (!ok || !s.valid) {
		homeIcmSummary_->setText(QString::fromUtf8("读取失败"));
		icmDetail_->setText(QString::fromUtf8("读取失败\n设备: %1").arg(icmDev_));
		return;
	}
	homeIcmSummary_->setText(QString::fromUtf8("az=%1 g  temp=%2 °C")
					 .arg(s.az_g, 0, 'f', 2)
					 .arg(s.temp_c, 0, 'f', 1));
	icmDetail_->setText(
		QString::fromUtf8("加速度 raw: ax=%1 ay=%2 az=%3\n")
			.arg(s.ax)
			.arg(s.ay)
			.arg(s.az) +
		QString::fromUtf8("加速度 g  : ax=%1 ay=%2 az=%3\n")
			.arg(s.ax_g, 0, 'f', 3)
			.arg(s.ay_g, 0, 'f', 3)
			.arg(s.az_g, 0, 'f', 3) +
		QString::fromUtf8("角速度 raw: gx=%1 gy=%2 gz=%3\n")
			.arg(s.gx)
			.arg(s.gy)
			.arg(s.gz) +
		QString::fromUtf8("角速度 dps: gx=%1 gy=%2 gz=%3\n")
			.arg(s.gx_dps, 0, 'f', 2)
			.arg(s.gy_dps, 0, 'f', 2)
			.arg(s.gz_dps, 0, 'f', 2) +
		QString::fromUtf8("温度      : raw=%1  %2 °C\n\n设备: %3")
			.arg(s.temp_raw)
			.arg(s.temp_c, 0, 'f', 2)
			.arg(icmDev_));
}

void Dashboard::onHomeTick()
{
	refreshApLabels();
	refreshIcmLabels();
}

void Dashboard::onDetailTick()
{
	if (stack_->currentIndex() == 1)
		refreshApLabels();
	else if (stack_->currentIndex() == 2)
		refreshIcmLabels();
}

void Dashboard::openAp()
{
	stack_->setCurrentIndex(1);
	refreshApLabels();
}

void Dashboard::openIcm()
{
	stack_->setCurrentIndex(2);
	refreshIcmLabels();
}

void Dashboard::backHome()
{
	stack_->setCurrentIndex(0);
	onHomeTick();
}

void Dashboard::setPageTimers(int pageIndex)
{
	homeTimer_->stop();
	detailTimer_->stop();
	if (pageIndex == 0)
		homeTimer_->start();
	else
		detailTimer_->start();
}
