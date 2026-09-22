/* SPDX-License-Identifier: MIT */
/* sensor-dashboard：LCD 传感器仪表盘（Qt6 linuxfb + evdev） */

#include "dashboard.hpp"

#include <QApplication>
#include <QFont>

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
	QFont font(QString::fromUtf8("WenQuanYi Zen Hei"));
	if (!font.exactMatch())
		font = QFont(QString::fromUtf8("WenQuanYi Micro Hei"));
	if (!font.exactMatch())
		font = QFont(QString::fromUtf8("Noto Sans CJK SC"));
	font.setPointSize(16);
	app.setFont(font);
}

int main(int argc, char **argv)
{
	setupPlatformEnv();
	QApplication app(argc, argv);
	setupCjkFont(app);

	Dashboard w(QString::fromUtf8(envOr("SENSOR_DASHBOARD_AP_DEV", "/dev/ap3216c")),
		    QString::fromUtf8(envOr("SENSOR_DASHBOARD_ICM_DEV", "/dev/icm20608")),
		    envInt("SENSOR_DASHBOARD_HOME_MS", 1000),
		    envInt("SENSOR_DASHBOARD_DETAIL_MS", 500));
	w.showFullScreen();
	return app.exec();
}
