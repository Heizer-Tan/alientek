/* SPDX-License-Identifier: MIT */
/* sensor-dashboard：板级控制台（Qt6 linuxfb + evdev） */

#include "dashboard.hpp"
#include "nofocus_style.hpp"

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

#include <cstdlib>

static const char *envOr(const char *name, const char *fallback)
{
	const char *v = std::getenv(name);
	if (v && *v)
		return v;
	return fallback;
}

static int envInt(const char *name, int fallback)
{
	const char *v = std::getenv(name);
	if (!v || !*v)
		return fallback;
	char *end = nullptr;
	const long n = std::strtol(v, &end, 10);
	if (end == v || *end != '\0' || n <= 0 || n > 60000)
		return fallback;
	return static_cast<int>(n);
}

static void setupPlatformEnv()
{
	const char *fb = envOr("SENSOR_DASHBOARD_FB", "/dev/fb0");
	if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
		QByteArray plat = QByteArray("linuxfb:fb=") + fb;
		qputenv("QT_QPA_PLATFORM", plat);
	}
	const char *touch = std::getenv("SENSOR_DASHBOARD_TOUCH_DEV");
	if (touch && *touch)
		qputenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", touch);
}

static void setupCjkFont(QApplication &app)
{
	QFont font(QString::fromUtf8("WenQuanYi Micro Hei"));
	if (!font.exactMatch())
		font = QFont(QString::fromUtf8("WenQuanYi Zen Hei"));
	if (!font.exactMatch())
		font = QFont(QString::fromUtf8("Noto Sans CJK SC"));
	/* 像素字号 + 关抗锯齿：1024x600 上避免 pt/DPI 造成发糊 */
	font.setPixelSize(20);
	font.setStyleStrategy(QFont::NoAntialias);
	font.setHintingPreference(QFont::PreferFullHinting);
	app.setFont(font);
}

int main(int argc, char **argv)
{
	setupPlatformEnv();
	/* 1pt≈1px，样式表里混用 px 时也不被错误 DPI 放大 */
	if (qgetenv("QT_FONT_DPI").isEmpty())
		qputenv("QT_FONT_DPI", "72");
	QApplication app(argc, argv);
	/* Fusion + 自定义 Style：彻底去掉虚线焦点框 */
	if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
		app.setStyle(new NoFocusStyle(fusion));
	else
		app.setStyle(new NoFocusStyle);
	setupCjkFont(app);
	app.setApplicationDisplayName(QString::fromUtf8("板级控制台"));

	Dashboard w(QString::fromUtf8(envOr("SENSOR_DASHBOARD_AP_DEV", "/dev/ap3216c")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_ICM_IIO_NAME", "icm20608")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_IFACE", "eth0")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_LED_NAME", "alientek-led0")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_BEEP_NAME", "beep")),
		    envInt("SENSOR_DASHBOARD_HOME_MS", 1000),
		    envInt("SENSOR_DASHBOARD_DETAIL_MS", 500));
	w.setWindowTitle(QString::fromUtf8("板级控制台"));
	/* 与 fbset 一致：1024x600，不信任 QScreen 可能偏大的 geometry */
	w.setFixedSize(1024, 600);
	w.move(0, 0);
	w.show();
	return app.exec();
}
