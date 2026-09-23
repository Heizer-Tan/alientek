/* SPDX-License-Identifier: MIT */
#include "dashboard.hpp"

#include "keys.hpp"
#include "leds.hpp"
#include "ota_status.hpp"
#include "sensors.hpp"
#include "sysinfo.hpp"

#include <QGridLayout>
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
constexpr char kAccent3[] = "#E0A100";
constexpr char kAccent4[] = "#C75B39";

QString cardStyle(const char *accent)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2; border: 2px solid #2E4055;"
		"  border-radius: 16px; text-align: left; padding: 16px;"
		"}"
		"QPushButton:pressed { background-color: %3; }")
		.arg(QLatin1String(kCard), QLatin1String(kText),
		     QLatin1String(accent));
}

QString fmtUnavailable(const QString &v)
{
	return v.isEmpty() ? QString::fromUtf8("不可用") : v;
}

} // namespace

Dashboard::Dashboard(const QString &apDev, const QString &icmName,
		     const QString &iface, const QString &ledName,
		     const QString &beepName, int homeMs, int detailMs,
		     QWidget *parent)
	: QWidget(parent), apDev_(apDev), icmName_(icmName), iface_(iface),
	  ledName_(ledName), beepName_(beepName),
	  ledsRoot_(QStringLiteral("/sys/class/leds"))
{
	applyDarkStyle(this);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	stack_ = new QStackedWidget(this);
	lay->addWidget(stack_);
	stack_->addWidget(buildHomePage());
	stack_->addWidget(buildApPage());
	stack_->addWidget(buildIcmPage());
	stack_->addWidget(buildSysPage());
	stack_->addWidget(buildLedsPage());
	stack_->addWidget(buildKeysPage());
	stack_->addWidget(buildOtaPage());

	keys_ = new KeyWatcher(this);
	connect(keys_, &KeyWatcher::pressedChanged, this, &Dashboard::onKeyPressed);
	if (!keys_->openByNameSubstr(QStringLiteral("user-key")))
		homeKeySummary_->setText(QString::fromUtf8("不可用"));

	homeTimer_ = new QTimer(this);
	detailTimer_ = new QTimer(this);
	homeTimer_->setInterval(homeMs > 0 ? homeMs : 1000);
	detailTimer_->setInterval(detailMs > 0 ? detailMs : 500);
	connect(homeTimer_, &QTimer::timeout, this, &Dashboard::onHomeTick);
	connect(detailTimer_, &QTimer::timeout, this, &Dashboard::onDetailTick);
	connect(stack_, &QStackedWidget::currentChanged, this,
		&Dashboard::setPageTimers);

	setPageTimers(PageHome);
	onHomeTick();
}

void Dashboard::applyDarkStyle(QWidget *w)
{
	w->setStyleSheet(QStringLiteral("QWidget { background-color: %1; color: %2; }")
				 .arg(QLatin1String(kBg), QLatin1String(kText)));
}

QPushButton *Dashboard::makeHomeCard(const QString &title, QLabel **summaryOut,
				     const char *accent)
{
	auto *btn = new QPushButton;
	btn->setMinimumHeight(120);
	btn->setStyleSheet(cardStyle(accent));
	auto *col = new QVBoxLayout(btn);
	auto *t = new QLabel(title);
	t->setAttribute(Qt::WA_TransparentForMouseEvents);
	t->setStyleSheet(QStringLiteral("font-size: 18px; color: %1;")
				 .arg(QLatin1String(accent)));
	auto *sum = new QLabel(QString::fromUtf8("…"));
	sum->setAttribute(Qt::WA_TransparentForMouseEvents);
	sum->setStyleSheet(
		QStringLiteral("font-size: 14px; color: %1;").arg(QLatin1String(kMuted)));
	col->addWidget(t);
	col->addWidget(sum);
	if (summaryOut)
		*summaryOut = sum;
	return btn;
}

QWidget *Dashboard::buildHomePage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(24, 24, 24, 24);
	lay->setSpacing(12);

	auto *title = new QLabel(QString::fromUtf8("板级控制台"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	lay->addWidget(title);

	auto *grid = new QGridLayout;
	grid->setSpacing(12);
	auto *apBtn = makeHomeCard(QString::fromUtf8("光感"), &homeApSummary_, kAccent);
	auto *icmBtn =
		makeHomeCard(QString::fromUtf8("六轴"), &homeIcmSummary_, kAccent2);
	auto *sysBtn =
		makeHomeCard(QString::fromUtf8("系统"), &homeSysSummary_, kAccent3);
	auto *ledBtn =
		makeHomeCard(QString::fromUtf8("灯控"), &homeLedSummary_, kAccent4);
	auto *keyBtn =
		makeHomeCard(QString::fromUtf8("按键"), &homeKeySummary_, kAccent);
	auto *otaBtn =
		makeHomeCard(QString::fromUtf8("OTA"), &homeOtaSummary_, kAccent2);
	connect(apBtn, &QPushButton::clicked, this, &Dashboard::openAp);
	connect(icmBtn, &QPushButton::clicked, this, &Dashboard::openIcm);
	connect(sysBtn, &QPushButton::clicked, this, &Dashboard::openSys);
	connect(ledBtn, &QPushButton::clicked, this, &Dashboard::openLeds);
	connect(keyBtn, &QPushButton::clicked, this, &Dashboard::openKeys);
	connect(otaBtn, &QPushButton::clicked, this, &Dashboard::openOta);
	grid->addWidget(apBtn, 0, 0);
	grid->addWidget(icmBtn, 0, 1);
	grid->addWidget(sysBtn, 1, 0);
	grid->addWidget(ledBtn, 1, 1);
	grid->addWidget(keyBtn, 2, 0);
	grid->addWidget(otaBtn, 2, 1);
	lay->addLayout(grid, 1);
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

QWidget *Dashboard::buildSysPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(40, 40, 40, 40);
	auto *title = new QLabel(QString::fromUtf8("系统信息"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	sysDetail_ = new QLabel;
	sysDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	sysDetail_->setStyleSheet(QStringLiteral("font-size: 20px; font-family: monospace;"));
	auto *back = new QPushButton(QString::fromUtf8("返回"));
	back->setMinimumHeight(64);
	back->setStyleSheet(cardStyle(kAccent3));
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);
	lay->addWidget(title);
	lay->addWidget(sysDetail_, 1);
	lay->addWidget(back);
	return page;
}

QWidget *Dashboard::buildLedsPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(32, 32, 32, 32);
	auto *title = new QLabel(QString::fromUtf8("灯控"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	ledDetail_ = new QLabel;
	ledDetail_->setWordWrap(true);
	ledDetail_->setStyleSheet(QStringLiteral("font-size: 16px; font-family: monospace;"));

	auto *ledOn = new QPushButton(QString::fromUtf8("LED 开"));
	auto *ledOff = new QPushButton(QString::fromUtf8("LED 关"));
	auto *ledHb = new QPushButton(QString::fromUtf8("恢复呼吸灯"));
	auto *beepOn = new QPushButton(QString::fromUtf8("蜂鸣器 开"));
	auto *beepOff = new QPushButton(QString::fromUtf8("蜂鸣器 关"));
	for (QPushButton *b : {ledOn, ledOff, ledHb, beepOn, beepOff}) {
		b->setMinimumHeight(52);
		b->setStyleSheet(cardStyle(kAccent4));
	}
	connect(ledOn, &QPushButton::clicked, this, &Dashboard::onLedOn);
	connect(ledOff, &QPushButton::clicked, this, &Dashboard::onLedOff);
	connect(ledHb, &QPushButton::clicked, this, &Dashboard::onLedHeartbeat);
	connect(beepOn, &QPushButton::clicked, this, &Dashboard::onBeepOn);
	connect(beepOff, &QPushButton::clicked, this, &Dashboard::onBeepOff);

	auto *back = new QPushButton(QString::fromUtf8("返回"));
	back->setMinimumHeight(64);
	back->setStyleSheet(cardStyle(kAccent));
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);

	lay->addWidget(title);
	lay->addWidget(ledDetail_);
	lay->addWidget(ledOn);
	lay->addWidget(ledOff);
	lay->addWidget(ledHb);
	lay->addWidget(beepOn);
	lay->addWidget(beepOff);
	lay->addStretch(1);
	lay->addWidget(back);
	return page;
}

QWidget *Dashboard::buildKeysPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(40, 40, 40, 40);
	auto *title = new QLabel(QString::fromUtf8("按键"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	keyDetail_ = new QLabel(QString::fromUtf8("松开"));
	keyDetail_->setAlignment(Qt::AlignCenter);
	keyDetail_->setStyleSheet(QStringLiteral("font-size: 32px; font-weight: bold;"));
	auto *back = new QPushButton(QString::fromUtf8("返回"));
	back->setMinimumHeight(64);
	back->setStyleSheet(cardStyle(kAccent));
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);
	lay->addWidget(title);
	lay->addWidget(keyDetail_, 1);
	lay->addWidget(back);
	return page;
}

QWidget *Dashboard::buildOtaPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(40, 40, 40, 40);
	auto *title = new QLabel(QString::fromUtf8("OTA 状态"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: bold;"));
	otaDetail_ = new QLabel;
	otaDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	otaDetail_->setStyleSheet(QStringLiteral("font-size: 20px; font-family: monospace;"));
	auto *back = new QPushButton(QString::fromUtf8("返回"));
	back->setMinimumHeight(64);
	back->setStyleSheet(cardStyle(kAccent2));
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);
	lay->addWidget(title);
	lay->addWidget(otaDetail_, 1);
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
		QString::fromUtf8("ALS=%1").arg(s.als));
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
	const bool ok = readIcmSample(icmName_.toUtf8().constData(), &s);
	if (!ok || !s.valid) {
		homeIcmSummary_->setText(QString::fromUtf8("读取失败"));
		icmDetail_->setText(
			QString::fromUtf8("读取失败\nIIO: %1").arg(icmName_));
		return;
	}
	homeIcmSummary_->setText(QString::fromUtf8("az=%1g")
					 .arg(s.az_g, 0, 'f', 2));
	icmDetail_->setText(
		QString::fromUtf8("加速度 raw: ax=%1 ay=%2 az=%3\n")
			.arg(s.ax)
			.arg(s.ay)
			.arg(s.az) +
		QString::fromUtf8("加速度 g  : ax=%1 ay=%2 az=%3\n")
			.arg(s.ax_g, 0, 'f', 3)
			.arg(s.ay_g, 0, 'f', 3)
			.arg(s.az_g, 0, 'f', 3) +
		QString::fromUtf8("角速度 dps: gx=%1 gy=%2 gz=%3\n")
			.arg(s.gx_dps, 0, 'f', 2)
			.arg(s.gy_dps, 0, 'f', 2)
			.arg(s.gz_dps, 0, 'f', 2) +
		QString::fromUtf8("温度      : %1 °C\n\nIIO: %2")
			.arg(s.temp_c, 0, 'f', 2)
			.arg(icmName_));
}

void Dashboard::refreshSysLabels()
{
	SysInfo s{};
	readSysInfo(iface_, &s);
	const int upMin = static_cast<int>(s.uptimeSec / 60.0);
	homeSysSummary_->setText(fmtUnavailable(s.ipv4));
	sysDetail_->setText(
		QString::fromUtf8("主机名   : %1\n"
				  "IPv4(%2): %3\n"
				  "运行时间 : %4 分钟\n"
				  "内存     : %5 / %6 MB 可用/总计\n"
				  "负载 1m  : %7")
			.arg(s.hostname)
			.arg(iface_)
			.arg(fmtUnavailable(s.ipv4))
			.arg(upMin)
			.arg(s.memAvailKb / 1024)
			.arg(s.memTotalKb / 1024)
			.arg(s.load1, 0, 'f', 2));
}

void Dashboard::refreshLedLabels()
{
	LedStatus led{};
	LedStatus beep{};
	const bool ledOk = ledReadStatus(ledsRoot_, ledName_, &led);
	const bool beepOk = ledReadStatus(ledsRoot_, beepName_, &beep);
	QString home = QString::fromUtf8("LED?");
	if (ledOk) {
		if (led.trigger == QStringLiteral("heartbeat"))
			home = QString::fromUtf8("呼吸");
		else
			home = led.brightness > 0 ? QString::fromUtf8("亮")
						 : QString::fromUtf8("灭");
	}
	homeLedSummary_->setText(home);
	ledDetail_->setText(
		QString::fromUtf8("LED (%1)\n  trigger=%2  brightness=%3\n\n"
				  "蜂鸣器 (%4)\n  trigger=%5  brightness=%6")
			.arg(ledName_)
			.arg(ledOk ? led.trigger : QString::fromUtf8("不可用"))
			.arg(ledOk ? QString::number(led.brightness)
				   : QString::fromUtf8("-"))
			.arg(beepName_)
			.arg(beepOk ? beep.trigger : QString::fromUtf8("不可用"))
			.arg(beepOk ? QString::number(beep.brightness)
				    : QString::fromUtf8("-")));
}

void Dashboard::refreshOtaLabels()
{
	OtaStatus s{};
	const bool ok = readOtaStatus(&s);
	homeOtaSummary_->setText(ok ? fmtUnavailable(s.activeSlot)
				    : QString::fromUtf8("不可用"));
	otaDetail_->setText(
		QString::fromUtf8("active_slot       : %1\n"
				  "upgrade_available : %2\n"
				  "cmdline root      : %3\n\n"
				  "（只读，请用 Web 升级）")
			.arg(fmtUnavailable(s.activeSlot))
			.arg(fmtUnavailable(s.upgradeAvailable))
			.arg(fmtUnavailable(s.cmdlineRootHint)));
}

void Dashboard::onHomeTick()
{
	refreshApLabels();
	refreshIcmLabels();
	refreshSysLabels();
	refreshLedLabels();
	refreshOtaLabels();
	if (keys_ && keys_->isOpen())
		homeKeySummary_->setText(keys_->pressed()
						 ? QString::fromUtf8("按下")
						 : QString::fromUtf8("松开"));
}

void Dashboard::onDetailTick()
{
	switch (stack_->currentIndex()) {
	case PageAp:
		refreshApLabels();
		break;
	case PageIcm:
		refreshIcmLabels();
		break;
	case PageSys:
		refreshSysLabels();
		break;
	case PageLeds:
		refreshLedLabels();
		break;
	case PageOta:
		refreshOtaLabels();
		break;
	default:
		break;
	}
}

void Dashboard::openAp()
{
	stack_->setCurrentIndex(PageAp);
	refreshApLabels();
}

void Dashboard::openIcm()
{
	stack_->setCurrentIndex(PageIcm);
	refreshIcmLabels();
}

void Dashboard::openSys()
{
	stack_->setCurrentIndex(PageSys);
	refreshSysLabels();
}

void Dashboard::openLeds()
{
	stack_->setCurrentIndex(PageLeds);
	refreshLedLabels();
}

void Dashboard::openKeys()
{
	stack_->setCurrentIndex(PageKeys);
	if (!keys_->isOpen())
		keyDetail_->setText(QString::fromUtf8("不可用"));
	else
		onKeyPressed(keys_->pressed());
}

void Dashboard::openOta()
{
	stack_->setCurrentIndex(PageOta);
	refreshOtaLabels();
}

void Dashboard::backHome()
{
	stack_->setCurrentIndex(PageHome);
	onHomeTick();
}

void Dashboard::onLedOn()
{
	if (!ledSetManual(ledsRoot_, ledName_, true))
		ledDetail_->setText(QString::fromUtf8("控制失败（LED 开）"));
	else
		refreshLedLabels();
}

void Dashboard::onLedOff()
{
	if (!ledSetManual(ledsRoot_, ledName_, false))
		ledDetail_->setText(QString::fromUtf8("控制失败（LED 关）"));
	else
		refreshLedLabels();
}

void Dashboard::onLedHeartbeat()
{
	if (!ledRestoreHeartbeat(ledsRoot_, ledName_))
		ledDetail_->setText(QString::fromUtf8("控制失败（呼吸灯）"));
	else
		refreshLedLabels();
}

void Dashboard::onBeepOn()
{
	if (!ledSetManual(ledsRoot_, beepName_, true))
		ledDetail_->setText(QString::fromUtf8("控制失败（蜂鸣器 开）"));
	else
		refreshLedLabels();
}

void Dashboard::onBeepOff()
{
	if (!ledSetManual(ledsRoot_, beepName_, false))
		ledDetail_->setText(QString::fromUtf8("控制失败（蜂鸣器 关）"));
	else
		refreshLedLabels();
}

void Dashboard::onKeyPressed(bool pressed)
{
	const QString text =
		pressed ? QString::fromUtf8("按下") : QString::fromUtf8("松开");
	keyDetail_->setText(text);
	homeKeySummary_->setText(text);
}

void Dashboard::setPageTimers(int pageIndex)
{
	homeTimer_->stop();
	detailTimer_->stop();
	if (pageIndex == PageHome)
		homeTimer_->start();
	else if (pageIndex != PageKeys)
		detailTimer_->start();
}
