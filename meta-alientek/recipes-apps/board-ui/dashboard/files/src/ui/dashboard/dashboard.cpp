/* SPDX-License-Identifier: MIT */
#include "dashboard.hpp"

#include "hw/keys/keys.hpp"
#include "hw/leds/leds.hpp"
#include "hw/sensors/sensors.hpp"
#include "sys/ota/ota_status.hpp"
#include "sys/sysinfo/sysinfo.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/* 复古终端：黑底荧光绿（参考 CRT/console） */
constexpr char kBg[] = "#000000";
constexpr char kPanel[] = "#001408";
constexpr char kPanelHi[] = "#00331a";
constexpr char kText[] = "#00FF66";
constexpr char kMuted[] = "#007A3D";
constexpr char kCyan[] = "#00FF66";
constexpr char kGreen[] = "#00FF66";
constexpr char kYellow[] = "#FFCC00";
constexpr char kOrange[] = "#FF3344";
constexpr char kInk[] = "#000000";
constexpr char kWarn[] = "#FF3344";

QString terminalFont()
{
	return QStringLiteral(
		"font-family: \"DejaVu Sans Mono\", \"Liberation Mono\", "
		"monospace;");
}

QString pixelBtnStyle(const char * /*face*/)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2; %3"
		"  border: 1px solid %2; border-radius: 0px; outline: none;"
		"  text-align: left; padding: 6px 10px; font-weight: bold;"
		"}"
		"QPushButton:focus { outline: none; border: 1px solid %4; }"
		"QPushButton:pressed {"
		"  background-color: %5; color: %1; border: 1px solid %2;"
		"}")
		.arg(QLatin1String(kPanel), QLatin1String(kText), terminalFont(),
		     QLatin1String(kYellow), QLatin1String(kText));
}

QString pixelActionStyle(const char * /*face*/)
{
	return QStringLiteral(
		"QPushButton {"
		"  background-color: %1; color: %2; %3"
		"  border: 1px solid %2; border-radius: 0px; outline: none;"
		"  text-align: center; padding: 10px; font-weight: bold;"
		"  font-size: 18px;"
		"}"
		"QPushButton:focus { outline: none; }"
		"QPushButton:pressed {"
		"  background-color: %2; color: %1; border: 1px solid %2;"
		"}")
		.arg(QLatin1String(kPanel), QLatin1String(kText), terminalFont());
}

QString titleStyle(const char *accent)
{
	return QStringLiteral(
		"%1 font-size: 22px; font-weight: bold; color: %2;"
		" letter-spacing: 1px;")
		.arg(terminalFont(), QLatin1String(accent));
}

QString monoStyle()
{
	return QStringLiteral(
		"%1 font-size: 18px; color: %2;"
		" background-color: %3; border: 1px solid %4; padding: 12px;")
		.arg(terminalFont(), QLatin1String(kText), QLatin1String(kInk),
		     QLatin1String(kMuted));
}

QString fmtUnavailable(const QString &v)
{
	return v.isEmpty() ? QString::fromUtf8("N/A") : v;
}

/* 终端点线对齐：LABEL........VALUE */
QString padDots(const QString &label, const QString &value, int width = 22)
{
	QString left = label;
	if (left.size() >= width)
		return left + QStringLiteral(" ") + value;
	return left + QString(width - left.size(), QLatin1Char('.')) + value;
}

void noFocus(QPushButton *b)
{
	/* 触摸屏去掉 Qt 默认虚线焦点框 */
	b->setFocusPolicy(Qt::NoFocus);
	b->setAttribute(Qt::WA_MacShowFocusRect, false);
}

QPushButton *makeBackBtn(const char *face)
{
	auto *back = new QPushButton(QString::fromUtf8("[ BACK ]"));
	back->setMinimumHeight(64);
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
	/* 纯黑底 + 绿字；细扫描线感用极淡点阵 */
	w->setStyleSheet(QStringLiteral(
		"QWidget {"
		"  background-color: %1; color: %2; %3"
		"  background-image: radial-gradient(%4 1px, transparent 1px);"
		"  background-size: 3px 3px;"
		"}"
		"QLabel { background: transparent; background-image: none; }"
		"QStackedWidget { background-color: %1;"
		"  background-image: radial-gradient(%4 1px, transparent 1px);"
		"  background-size: 3px 3px; }"
		"QPushButton, QPushButton:focus { outline: none; }"
		"QMessageBox { background-color: %1; color: %2; }"
		"QMessageBox QLabel { color: %2; %3 }"
		"*:focus { outline: none; }")
				 .arg(QLatin1String(kBg), QLatin1String(kText),
				      terminalFont(), QLatin1String(kPanelHi)));
	w->setFocusPolicy(Qt::NoFocus);
}

QPushButton *Dashboard::makeHomeCard(const QString &title, QLabel **summaryOut,
				     const char *accent)
{
	auto *btn = new QPushButton;
	noFocus(btn);
	btn->setFixedHeight(150);
	btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setStyleSheet(pixelBtnStyle(kPanel));
	auto *col = new QVBoxLayout(btn);
	col->setContentsMargins(8, 6, 8, 6);
	col->setSpacing(4);

	auto *tag = new QLabel(QString::fromUtf8("> ") + title.toUpper());
	tag->setAttribute(Qt::WA_TransparentForMouseEvents);
	tag->setStyleSheet(QStringLiteral("%1 font-size: 20px; font-weight: bold; color: %2;")
				   .arg(terminalFont(), QLatin1String(accent)));

	auto *sum = new QLabel(QString::fromUtf8("...."));
	sum->setAttribute(Qt::WA_TransparentForMouseEvents);
	sum->setWordWrap(false);
	sum->setStyleSheet(QStringLiteral("%1 font-size: 16px; color: %2;")
				   .arg(terminalFont(), QLatin1String(kMuted)));

	col->addWidget(tag);
	col->addWidget(sum);
	col->addStretch(1);
	if (summaryOut)
		*summaryOut = sum;
	return btn;
}

QWidget *Dashboard::buildHomePage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(12, 8, 12, 8);
	lay->setSpacing(8);

	/* 顶栏：终端 HUD */
	auto *hud = new QWidget;
	hud->setObjectName(QStringLiteral("hud"));
	hud->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	hud->setStyleSheet(QStringLiteral(
		"QWidget#hud {"
		"  background-color: %1; color: %2; %3"
		"  border: 1px solid %2; padding: 4px 8px;"
		"  font-weight: bold; font-size: 15px;"
		"  background-image: none;"
		"}")
				   .arg(QLatin1String(kInk), QLatin1String(kText),
					terminalFont()));

	auto *hudLay = new QHBoxLayout(hud);
	hudLay->setContentsMargins(4, 2, 4, 2);
	hudLay->setSpacing(8);
	auto *brand = new QLabel(QString::fromUtf8("ALIENTEK-ALPHA // CONSOLE"));
	brand->setStyleSheet(QStringLiteral("color: %1; background: transparent; %2")
				     .arg(QLatin1String(kText), terminalFont()));
	auto *ok = new QLabel(QString::fromUtf8("[SYS OK]"));
	ok->setStyleSheet(QStringLiteral("color: %1; background: transparent; %2")
				  .arg(QLatin1String(kYellow), terminalFont()));
	ok->setAlignment(Qt::AlignCenter);
	homeHudClock_ = new QLabel(QString::fromUtf8("--:--:--"));
	homeHudClock_->setStyleSheet(
		QStringLiteral("color: %1; background: transparent; %2")
			.arg(QLatin1String(kWarn), terminalFont()));
	homeHudClock_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	hudLay->addWidget(brand);
	hudLay->addWidget(ok, 1);
	hudLay->addWidget(homeHudClock_);
	lay->addWidget(hud, 0);

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(8);
	grid->setContentsMargins(0, 0, 0, 0);
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
	lay->addLayout(grid, 0);
	/* 剩余高度留白，不把卡片纵向拉满 */
	lay->addStretch(1);
	return page;
}

QWidget *Dashboard::buildApPage()
{
	auto *page = new QWidget;
	applyDarkStyle(page);
	auto *lay = new QVBoxLayout(page);
	lay->setContentsMargins(28, 24, 28, 24);
	auto *title = new QLabel(QString::fromUtf8("> SENSOR / AP3216C"));
	title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	title->setStyleSheet(titleStyle(kGreen));
	apDetail_ = new QLabel;
	apDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	apDetail_->setStyleSheet(monoStyle());
	auto *back = makeBackBtn(kGreen);
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
	auto *title = new QLabel(QString::fromUtf8("> IMU / ICM20608"));
	title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
	auto *title = new QLabel(QString::fromUtf8("> SYSTEM"));
	title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	title->setStyleSheet(titleStyle(kGreen));
	sysDetail_ = new QLabel;
	sysDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	sysDetail_->setStyleSheet(monoStyle());
	auto *back = makeBackBtn(kGreen);
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
	auto *title = new QLabel(QString::fromUtf8("> GPIO / LEDS"));
	title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	title->setStyleSheet(titleStyle(kGreen));
	ledDetail_ = new QLabel;
	ledDetail_->setWordWrap(true);
	ledDetail_->setStyleSheet(monoStyle());

	auto *ledOn = new QPushButton(QString::fromUtf8("[ LED ON ]"));
	auto *ledOff = new QPushButton(QString::fromUtf8("[ LED OFF ]"));
	auto *ledHb = new QPushButton(QString::fromUtf8("[ LED HEARTBEAT ]"));
	auto *beepOn = new QPushButton(QString::fromUtf8("[ BEEP ON ]"));
	auto *beepOff = new QPushButton(QString::fromUtf8("[ BEEP OFF ]"));
	for (QPushButton *b : {ledOn, ledOff, ledHb, beepOn, beepOff}) {
		b->setMinimumHeight(56);
		b->setStyleSheet(pixelActionStyle(kPanel));
		noFocus(b);
	}
	connect(ledOn, &QPushButton::clicked, this, &Dashboard::onLedOn);
	connect(ledOff, &QPushButton::clicked, this, &Dashboard::onLedOff);
	connect(ledHb, &QPushButton::clicked, this, &Dashboard::onLedHeartbeat);
	connect(beepOn, &QPushButton::clicked, this, &Dashboard::onBeepOn);
	connect(beepOff, &QPushButton::clicked, this, &Dashboard::onBeepOff);

	auto *back = makeBackBtn(kGreen);
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
	auto *title = new QLabel(QString::fromUtf8("> INPUT / KEY"));
	title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	title->setStyleSheet(titleStyle(kGreen));
	keyDetail_ = new QLabel(QString::fromUtf8("[ RELEASED ]"));
	keyDetail_->setAlignment(Qt::AlignCenter);
	keyDetail_->setStyleSheet(QStringLiteral(
		"%1 font-size: 48px; font-weight: bold; color: %2;"
		" background-color: %3; border: 1px solid %4; padding: 24px;"
		" background-image: none;")
					  .arg(terminalFont(),
					       QLatin1String(kText),
					       QLatin1String(kInk),
					       QLatin1String(kMuted)));
	auto *back = makeBackBtn(kGreen);
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
	auto *title = new QLabel(QString::fromUtf8("> OTA / UPDATE"));
	title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	title->setStyleSheet(titleStyle(kGreen));
	otaDetail_ = new QLabel;
	otaDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	otaDetail_->setStyleSheet(monoStyle());
	otaProgressLabel_ = new QLabel(QString::fromUtf8("PROGRESS"));
	otaProgressLabel_->setStyleSheet(QStringLiteral(
		"%1 color: %2; font-size: 16px; font-weight: bold;")
						 .arg(terminalFont(),
						      QLatin1String(kYellow)));
	otaProgressBar_ = new QProgressBar;
	otaProgressBar_->setRange(0, 100);
	otaProgressBar_->setValue(0);
	otaProgressBar_->setTextVisible(true);
	otaProgressBar_->setFormat(QStringLiteral("[%p%]"));
	otaProgressBar_->setMinimumHeight(28);
	otaProgressBar_->setStyleSheet(QStringLiteral(
		"QProgressBar {"
		"  background-color: %1; border: 1px solid %2; border-radius: 0;"
		"  text-align: center; color: %2; font-weight: bold; font-size: 14px;"
		"  %3"
		"}"
		"QProgressBar::chunk {"
		"  background-color: %2; border: none; margin: 1px;"
		"}")
						  .arg(QLatin1String(kInk),
						       QLatin1String(kText),
						       terminalFont()));
	setOtaProgressVisible(false);
	otaPullBtn_ = new QPushButton(QString::fromUtf8("[ PULL LATEST & UPGRADE ]"));
	otaPullBtn_->setMinimumHeight(64);
	otaPullBtn_->setStyleSheet(pixelActionStyle(kGreen));
	noFocus(otaPullBtn_);
	connect(otaPullBtn_, &QPushButton::clicked, this,
		&Dashboard::onOtaPullLatest);
	otaPullProc_ = new QProcess(this);
	connect(otaPullProc_,
		QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		this, &Dashboard::onOtaPullFinished);
	connect(otaPullProc_, &QProcess::readyReadStandardOutput, this,
		&Dashboard::onOtaPullStdout);
	auto *back = makeBackBtn(kGreen);
	connect(back, &QPushButton::clicked, this, &Dashboard::backHome);
	lay->addWidget(title);
	lay->addWidget(otaDetail_, 1);
	lay->addWidget(otaProgressLabel_);
	lay->addWidget(otaProgressBar_);
	lay->addWidget(otaPullBtn_);
	lay->addWidget(back);
	return page;
}

void Dashboard::setOtaProgressVisible(bool visible)
{
	if (otaProgressLabel_ != nullptr)
		otaProgressLabel_->setVisible(visible);
	if (otaProgressBar_ != nullptr)
		otaProgressBar_->setVisible(visible);
}

void Dashboard::applyOtaProgressLine(const QString &line)
{
	const QString trimmed = line.trimmed();
	if (!trimmed.startsWith(QStringLiteral("OTA_PROGRESS")))
		return;
	const QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (parts.size() < 2)
		return;
	bool ok = false;
	const int pct = parts.at(1).toInt(&ok);
	if (!ok)
		return;
	QString stage = parts.size() >= 3 ? parts.at(2) : QString();
	QString stageZh = stage;
	if (stage == QStringLiteral("resolving") ||
	    stage == QStringLiteral("resolved"))
		stageZh = QString::fromUtf8("解析目录");
	else if (stage == QStringLiteral("downloading"))
		stageZh = QString::fromUtf8("下载固件");
	else if (stage == QStringLiteral("upgrading"))
		stageZh = QString::fromUtf8("写入并切槽");
	else if (stage == QStringLiteral("done"))
		stageZh = QString::fromUtf8("完成，即将重启");
	int clamped = pct;
	if (clamped < 0)
		clamped = 0;
	if (clamped > 100)
		clamped = 100;
	/* UI 侧也保证单调，防止乱序/重复行导致回跳 */
	if (clamped < otaProgressHighWater_)
		clamped = otaProgressHighWater_;
	otaProgressHighWater_ = clamped;
	if (otaProgressBar_ != nullptr)
		otaProgressBar_->setValue(clamped);
	if (otaProgressLabel_ != nullptr)
		otaProgressLabel_->setText(
			QString::fromUtf8("PROGRESS %1% · %2")
				.arg(clamped)
				.arg(stageZh));
	otaPullMsg_ = QString::fromUtf8("UPGRADING… %1% (%2)")
			      .arg(clamped)
			      .arg(stageZh);
	refreshOtaLabels();
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
	apDetail_->setText(
		padDots(QString::fromUtf8("IR"), QString::number(s.ir)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("ALS"), QString::number(s.als)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("PS"), QString::number(s.ps)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("DEV"), apDev_));
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
	homeIcmSummary_->setText(QString::fromUtf8("AZ=%1G")
					 .arg(s.az_g, 0, 'f', 2));
	icmDetail_->setText(
		padDots(QString::fromUtf8("AX_RAW"), QString::number(s.ax)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("AY_RAW"), QString::number(s.ay)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("AZ_RAW"), QString::number(s.az)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("AX_G"),
			QString::number(s.ax_g, 'f', 3)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("AY_G"),
			QString::number(s.ay_g, 'f', 3)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("AZ_G"),
			QString::number(s.az_g, 'f', 3)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("GX_DPS"),
			QString::number(s.gx_dps, 'f', 2)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("GY_DPS"),
			QString::number(s.gy_dps, 'f', 2)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("GZ_DPS"),
			QString::number(s.gz_dps, 'f', 2)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("TEMP_C"),
			QString::number(s.temp_c, 'f', 2)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("IIO"), icmName_));
}

void Dashboard::refreshSysLabels()
{
	SysInfo s{};
	readSysInfo(iface_, &s);
	const int upMin = static_cast<int>(s.uptimeSec / 60.0);
	homeSysSummary_->setText(fmtUnavailable(s.ipv4));
	sysDetail_->setText(
		padDots(QString::fromUtf8("HOST"), s.hostname) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("IFACE"), iface_) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("IPV4"), fmtUnavailable(s.ipv4)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("UPTIME_MIN"), QString::number(upMin)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("MEM_AVAIL_MB"),
			QString::number(s.memAvailKb / 1024)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("MEM_TOTAL_MB"),
			QString::number(s.memTotalKb / 1024)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("LOAD1"),
			QString::number(s.load1, 'f', 2)));
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
		padDots(QString::fromUtf8("LED"), ledName_) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("LED_TRIG"),
			ledOk ? led.trigger : QString::fromUtf8("N/A")) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("LED_BRIGHT"),
			ledOk ? QString::number(led.brightness)
			      : QString::fromUtf8("-")) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("BEEP"), beepName_) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("BEEP_TRIG"),
			beepOk ? beep.trigger : QString::fromUtf8("N/A")) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("BEEP_BRIGHT"),
			beepOk ? QString::number(beep.brightness)
			       : QString::fromUtf8("-")));
}

void Dashboard::refreshOtaLabels()
{
	OtaStatus s{};
	const bool ok = readOtaStatus(&s);
	homeOtaSummary_->setText(ok ? fmtUnavailable(s.activeSlot)
				    : QString::fromUtf8("N/A"));
	QString body =
		padDots(QString::fromUtf8("ACTIVE_SLOT"),
			fmtUnavailable(s.activeSlot)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("UPGRADE_AVAIL"),
			fmtUnavailable(s.upgradeAvailable)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("CMDLINE_ROOT"),
			fmtUnavailable(s.cmdlineRootHint)) +
		QLatin1Char('\n') +
		padDots(QString::fromUtf8("FW_BASE"),
			QString::fromUtf8("OTA_FIRMWARE_BASE")) +
		QLatin1Char('\n') +
		QString::fromUtf8("HINT....prefer stamped rootfs-*.swu");
	if (!otaPullMsg_.isEmpty())
		body += QString::fromUtf8("\n\nSTATUS..%1").arg(otaPullMsg_);
	otaDetail_->setText(body);
}

void Dashboard::onOtaPullLatest()
{
	if (otaPullProc_ != nullptr &&
	    otaPullProc_->state() != QProcess::NotRunning) {
		otaPullMsg_ = QString::fromUtf8("升级进行中，请稍候…");
		refreshOtaLabels();
		return;
	}
	const auto reply = QMessageBox::question(
		this, QString::fromUtf8("CONFIRM UPGRADE"),
		QString::fromUtf8(
			"Pull latest .swu from firmware base,\n"
			"flash inactive slot, then reboot.\n"
			"Continue?"),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (reply != QMessageBox::Yes)
		return;
	otaStdoutBuf_.clear();
	otaProgressHighWater_ = 0;
	otaPullMsg_ = QString::fromUtf8("UPGRADING…");
	if (otaProgressBar_ != nullptr)
		otaProgressBar_->setValue(0);
	if (otaProgressLabel_ != nullptr)
		otaProgressLabel_->setText(
			QString::fromUtf8("PROGRESS 0% · READY"));
	setOtaProgressVisible(true);
	if (otaPullBtn_ != nullptr)
		otaPullBtn_->setEnabled(false);
	refreshOtaLabels();
	otaPullProc_->start(QStringLiteral("ota-agent"),
			    {QStringLiteral("--pull-latest")});
	if (!otaPullProc_->waitForStarted(3000)) {
		otaPullMsg_ = QString::fromUtf8("FAIL: cannot start ota-agent");
		setOtaProgressVisible(false);
		if (otaPullBtn_ != nullptr)
			otaPullBtn_->setEnabled(true);
		refreshOtaLabels();
	}
}

void Dashboard::onOtaPullStdout()
{
	otaStdoutBuf_.append(
		QString::fromUtf8(otaPullProc_->readAllStandardOutput()));
	int nl;
	while ((nl = otaStdoutBuf_.indexOf(QLatin1Char('\n'))) >= 0) {
		const QString line = otaStdoutBuf_.left(nl);
		otaStdoutBuf_.remove(0, nl + 1);
		applyOtaProgressLine(line);
	}
}

void Dashboard::onOtaPullFinished(int exitCode, QProcess::ExitStatus status)
{
	if (!otaStdoutBuf_.isEmpty()) {
		applyOtaProgressLine(otaStdoutBuf_);
		otaStdoutBuf_.clear();
	}
	const QString err =
		QString::fromUtf8(otaPullProc_->readAllStandardError());
	if (otaPullBtn_ != nullptr)
		otaPullBtn_->setEnabled(true);
	if (status != QProcess::NormalExit || exitCode != 0) {
		otaPullMsg_ = QString::fromUtf8("FAIL code=%1\n%2")
				      .arg(exitCode)
				      .arg(err.trimmed());
		if (otaProgressLabel_ != nullptr)
			otaProgressLabel_->setText(
				QString::fromUtf8("PROGRESS FAIL"));
	} else {
		otaPullMsg_ = QString::fromUtf8(
			"UPGRADE DONE (reboot if not already)\n%1")
					  .arg(err.trimmed());
		if (otaProgressBar_ != nullptr)
			otaProgressBar_->setValue(100);
		if (otaProgressLabel_ != nullptr)
			otaProgressLabel_->setText(
				QString::fromUtf8("PROGRESS 100% · DONE"));
	}
	refreshOtaLabels();
}

void Dashboard::onHomeTick()
{
	if (homeHudClock_)
		homeHudClock_->setText(
			QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
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
	keyDetail_->setText(pressed ? QString::fromUtf8("[ PRESSED ]")
				    : QString::fromUtf8("[ RELEASED ]"));
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
