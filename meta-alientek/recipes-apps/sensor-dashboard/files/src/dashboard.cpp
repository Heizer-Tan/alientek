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

/* 像素风色板：直角、粗边、无圆角、无虚线焦点 */
constexpr char kBg[] = "#1A1C2C";
constexpr char kPanel[] = "#262B44";
constexpr char kPanelHi[] = "#3A4060";
constexpr char kText[] = "#F4F0E6";
constexpr char kMuted[] = "#8B9BB4";
constexpr char kCyan[] = "#5BC0EB";
constexpr char kGreen[] = "#6BCB63";
constexpr char kYellow[] = "#F7D51D";
constexpr char kOrange[] = "#E85A4F";
constexpr char kInk[] = "#0D0F18";
constexpr char kHi[] = "#FFFFFF";

QString pixelBtnStyle(const char *face)
{
	/* 米白描边 + 按下反色；贴近掌机 LCD */
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2;"
		"  border: 3px solid %3; border-radius: 0px; outline: none;"
		"  text-align: left; padding: 12px 14px; font-weight: bold;"
		"}"
		"QPushButton:focus { outline: none; border: 3px solid %3; }"
		"QPushButton:pressed {"
		"  background-color: %4; border: 3px solid %5;"
		"  padding-top: 14px; padding-left: 16px;"
		"  padding-bottom: 10px; padding-right: 12px;"
		"}")
		.arg(QLatin1String(face), QLatin1String(kText),
		     QLatin1String(kText), QLatin1String(kPanelHi),
		     QLatin1String(kMuted));
}

QString pixelActionStyle(const char * /*face*/)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2;"
		"  border: 3px solid %2; border-radius: 0px; outline: none;"
		"  text-align: center; padding: 10px; font-weight: bold;"
		"  font-size: 16px;"
		"}"
		"QPushButton:focus { outline: none; }"
		"QPushButton:pressed {"
		"  background-color: %3; color: %4; border: 3px solid %4;"
		"}")
		.arg(QLatin1String(kPanel), QLatin1String(kText),
		     QLatin1String(kText), QLatin1String(kInk));
}

QString titleStyle(const char *accent)
{
	return QStringLiteral(
		"font-size: 22px; font-weight: 900; color: %1;"
		" letter-spacing: 2px;")
		.arg(QLatin1String(accent));
}

QString monoStyle()
{
	return QStringLiteral(
		"font-size: 18px; font-family: monospace; color: %1;"
		" background-color: %2; border: 4px double %3; padding: 12px;")
		.arg(QLatin1String(kText), QLatin1String(kInk),
		     QLatin1String(kMuted));
}

QString fmtUnavailable(const QString &v)
{
	return v.isEmpty() ? QString::fromUtf8("N/A") : v;
}

void noFocus(QPushButton *b)
{
	/* 触摸屏去掉 Qt 默认虚线焦点框 */
	b->setFocusPolicy(Qt::NoFocus);
	b->setAttribute(Qt::WA_MacShowFocusRect, false);
}

QPushButton *makeBackBtn(const char *face)
{
	auto *back = new QPushButton(QString::fromUtf8("[ 返回 ]"));
	back->setMinimumHeight(56);
	back->setStyleSheet(pixelActionStyle(face));
	noFocus(back);
	return back;
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
	lay->setSpacing(0);
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
		homeKeySummary_->setText(QString::fromUtf8("N/A"));

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
	/* 点阵底模拟老 LCD；不可用时纯色底仍可读 */
	w->setStyleSheet(QStringLiteral(
		"QWidget {"
		"  background-color: %1; color: %2;"
		"  background-image: radial-gradient(%3 1px, transparent 1px);"
		"  background-size: 4px 4px;"
		"}"
		"QLabel { background: transparent; background-image: none; }"
		"QStackedWidget { background-color: %1;"
		"  background-image: radial-gradient(%3 1px, transparent 1px);"
		"  background-size: 4px 4px; }"
		"QPushButton, QPushButton:focus { outline: none; }"
		"*:focus { outline: none; }")
				 .arg(QLatin1String(kBg), QLatin1String(kText),
				      QLatin1String(kPanelHi)));
	w->setFocusPolicy(Qt::NoFocus);
}

QPushButton *Dashboard::makeHomeCard(const QString &title, QLabel **summaryOut,
				     const char *accent)
{
	auto *btn = new QPushButton;
	noFocus(btn);
	btn->setMinimumHeight(128);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setStyleSheet(pixelBtnStyle(kPanel));
	auto *col = new QVBoxLayout(btn);
	col->setContentsMargins(8, 4, 8, 4);
	col->setSpacing(6);

	auto *tag = new QLabel(QString::fromUtf8("▌") + title);
	tag->setAttribute(Qt::WA_TransparentForMouseEvents);
	tag->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 900; color: %1;")
				   .arg(QLatin1String(accent)));

	auto *sum = new QLabel(QString::fromUtf8("..."));
	sum->setAttribute(Qt::WA_TransparentForMouseEvents);
	sum->setStyleSheet(QStringLiteral(
				   "font-size: 15px; font-family: monospace; color: %1;")
				   .arg(QLatin1String(kMuted)));

	auto *hint = new QLabel(QString::fromUtf8("> ENTER"));
	hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	hint->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
				    .arg(QLatin1String(accent)));

	col->addWidget(tag);
	col->addWidget(sum);
	col->addStretch(1);
	col->addWidget(hint);
	if (summaryOut)
		*summaryOut = sum;
	return btn;
}

QWidget *Dashboard::buildHomePage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(20, 16, 20, 16);
	lay->setSpacing(10);

	auto *brand = new QLabel(QString::fromUtf8("ALIENTEK"));
	brand->setAlignment(Qt::AlignCenter);
	brand->setStyleSheet(QStringLiteral(
		"font-size: 14px; font-weight: 900; letter-spacing: 6px; color: %1;")
				     .arg(QLatin1String(kYellow)));

	auto *title = new QLabel(QString::fromUtf8("◆ 板级控制台 ◆"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kText));

	auto *bar = new QLabel(QString::fromUtf8("========================"));
	bar->setAlignment(Qt::AlignCenter);
	bar->setStyleSheet(QStringLiteral("font-family: monospace; color: %1;")
				   .arg(QLatin1String(kMuted)));

	lay->addWidget(brand);
	lay->addWidget(title);
	lay->addWidget(bar);

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(14);
	grid->setVerticalSpacing(14);
	auto *apBtn = makeHomeCard(QString::fromUtf8("光感"), &homeApSummary_, kCyan);
	auto *icmBtn =
		makeHomeCard(QString::fromUtf8("六轴"), &homeIcmSummary_, kGreen);
	auto *sysBtn =
		makeHomeCard(QString::fromUtf8("系统"), &homeSysSummary_, kYellow);
	auto *ledBtn =
		makeHomeCard(QString::fromUtf8("灯控"), &homeLedSummary_, kOrange);
	auto *keyBtn =
		makeHomeCard(QString::fromUtf8("按键"), &homeKeySummary_, kCyan);
	auto *otaBtn =
		makeHomeCard(QString::fromUtf8("OTA"), &homeOtaSummary_, kGreen);
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
	lay->setContentsMargins(28, 24, 28, 24);
	auto *title = new QLabel(QString::fromUtf8("■ 光感 AP3216C"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kCyan));
	apDetail_ = new QLabel;
	apDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	apDetail_->setStyleSheet(monoStyle());
	auto *back = makeBackBtn(kCyan);
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
	lay->setContentsMargins(28, 24, 28, 24);
	auto *title = new QLabel(QString::fromUtf8("■ 六轴 ICM20608"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kGreen));
	icmDetail_ = new QLabel;
	icmDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	icmDetail_->setStyleSheet(monoStyle());
	auto *back = makeBackBtn(kGreen);
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
	lay->setContentsMargins(28, 24, 28, 24);
	auto *title = new QLabel(QString::fromUtf8("■ 系统信息"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kYellow));
	sysDetail_ = new QLabel;
	sysDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	sysDetail_->setStyleSheet(monoStyle());
	auto *back = makeBackBtn(kYellow);
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
	lay->setContentsMargins(24, 20, 24, 20);
	lay->setSpacing(8);
	auto *title = new QLabel(QString::fromUtf8("■ 灯控"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kOrange));
	ledDetail_ = new QLabel;
	ledDetail_->setWordWrap(true);
	ledDetail_->setStyleSheet(monoStyle());

	auto *ledOn = new QPushButton(QString::fromUtf8("[ LED 开 ]"));
	auto *ledOff = new QPushButton(QString::fromUtf8("[ LED 关 ]"));
	auto *ledHb = new QPushButton(QString::fromUtf8("[ 恢复呼吸灯 ]"));
	auto *beepOn = new QPushButton(QString::fromUtf8("[ 蜂鸣器 开 ]"));
	auto *beepOff = new QPushButton(QString::fromUtf8("[ 蜂鸣器 关 ]"));
	for (QPushButton *b : {ledOn, ledOff, ledHb, beepOn, beepOff}) {
		b->setMinimumHeight(48);
		b->setStyleSheet(pixelActionStyle(kPanel));
		noFocus(b);
	}
	connect(ledOn, &QPushButton::clicked, this, &Dashboard::onLedOn);
	connect(ledOff, &QPushButton::clicked, this, &Dashboard::onLedOff);
	connect(ledHb, &QPushButton::clicked, this, &Dashboard::onLedHeartbeat);
	connect(beepOn, &QPushButton::clicked, this, &Dashboard::onBeepOn);
	connect(beepOff, &QPushButton::clicked, this, &Dashboard::onBeepOff);

	auto *back = makeBackBtn(kOrange);
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
	lay->setContentsMargins(28, 24, 28, 24);
	auto *title = new QLabel(QString::fromUtf8("■ 按键"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kCyan));
	keyDetail_ = new QLabel(QString::fromUtf8("松开"));
	keyDetail_->setAlignment(Qt::AlignCenter);
	keyDetail_->setStyleSheet(QStringLiteral(
		"font-size: 48px; font-weight: 900; color: %1;"
		" background-color: %2; border: 4px solid %3; padding: 24px;")
					  .arg(QLatin1String(kYellow),
					       QLatin1String(kPanel),
					       QLatin1String(kInk)));
	auto *back = makeBackBtn(kCyan);
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
	lay->setContentsMargins(28, 24, 28, 24);
	auto *title = new QLabel(QString::fromUtf8("■ OTA 状态"));
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(titleStyle(kGreen));
	otaDetail_ = new QLabel;
	otaDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	otaDetail_->setStyleSheet(monoStyle());
	auto *back = makeBackBtn(kGreen);
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
		homeApSummary_->setText(QString::fromUtf8("FAIL"));
		apDetail_->setText(QString::fromUtf8("读取失败\n设备: %1").arg(apDev_));
		return;
	}
	homeApSummary_->setText(QString::fromUtf8("ALS=%1").arg(s.als));
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
		homeIcmSummary_->setText(QString::fromUtf8("FAIL"));
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
		QString::fromUtf8("温度      : %1 C\n\nIIO: %2")
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
				  "内存     : %5 / %6 MB\n"
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
	QString home = QString::fromUtf8("?");
	if (ledOk) {
		if (led.trigger == QStringLiteral("heartbeat"))
			home = QString::fromUtf8("HEART");
		else
			home = led.brightness > 0 ? QString::fromUtf8("ON")
						 : QString::fromUtf8("OFF");
	}
	homeLedSummary_->setText(home);
	ledDetail_->setText(
		QString::fromUtf8("LED (%1)\n  trigger=%2  brightness=%3\n\n"
				  "BEEP (%4)\n  trigger=%5  brightness=%6")
			.arg(ledName_)
			.arg(ledOk ? led.trigger : QString::fromUtf8("N/A"))
			.arg(ledOk ? QString::number(led.brightness)
				   : QString::fromUtf8("-"))
			.arg(beepName_)
			.arg(beepOk ? beep.trigger : QString::fromUtf8("N/A"))
			.arg(beepOk ? QString::number(beep.brightness)
				    : QString::fromUtf8("-")));
}

void Dashboard::refreshOtaLabels()
{
	OtaStatus s{};
	const bool ok = readOtaStatus(&s);
	homeOtaSummary_->setText(ok ? fmtUnavailable(s.activeSlot)
				    : QString::fromUtf8("N/A"));
	otaDetail_->setText(
		QString::fromUtf8("active_slot       : %1\n"
				  "upgrade_available : %2\n"
				  "cmdline root      : %3\n\n"
				  "(READ-ONLY / use Web OTA)")
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
						 ? QString::fromUtf8("DOWN")
						 : QString::fromUtf8("UP"));
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
		keyDetail_->setText(QString::fromUtf8("N/A"));
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
		ledDetail_->setText(QString::fromUtf8("FAIL: LED ON"));
	else
		refreshLedLabels();
}

void Dashboard::onLedOff()
{
	if (!ledSetManual(ledsRoot_, ledName_, false))
		ledDetail_->setText(QString::fromUtf8("FAIL: LED OFF"));
	else
		refreshLedLabels();
}

void Dashboard::onLedHeartbeat()
{
	if (!ledRestoreHeartbeat(ledsRoot_, ledName_))
		ledDetail_->setText(QString::fromUtf8("FAIL: HEARTBEAT"));
	else
		refreshLedLabels();
}

void Dashboard::onBeepOn()
{
	if (!ledSetManual(ledsRoot_, beepName_, true))
		ledDetail_->setText(QString::fromUtf8("FAIL: BEEP ON"));
	else
		refreshLedLabels();
}

void Dashboard::onBeepOff()
{
	if (!ledSetManual(ledsRoot_, beepName_, false))
		ledDetail_->setText(QString::fromUtf8("FAIL: BEEP OFF"));
	else
		refreshLedLabels();
}

void Dashboard::onKeyPressed(bool pressed)
{
	const QString text =
		pressed ? QString::fromUtf8("按下") : QString::fromUtf8("松开");
	keyDetail_->setText(text);
	homeKeySummary_->setText(pressed ? QString::fromUtf8("DOWN")
					 : QString::fromUtf8("UP"));
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
