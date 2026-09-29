#!/usr/bin/env bash
# 宿主单测：LED sysfs 状态机（假目录）
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/meta-alientek/recipes-apps/board-ui/dashboard/files/src/hw/leds"
sysfs_src="$root/meta-alientek/recipes-apps/board-ui/dashboard/files/src/hw/sysfs"
src_root="$root/meta-alientek/recipes-apps/board-ui/dashboard/files/src"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fake="$tmp/leds"
mkdir -p "$fake/alientek-led0"
echo 'none heartbeat [heartbeat]' >"$fake/alientek-led0/trigger"
echo 1 >"$fake/alientek-led0/brightness"
echo 1 >"$fake/alientek-led0/max_brightness"

cat >"$tmp/test_leds.cpp" <<EOF
#include "led_class_controller.hpp"
#include <cstdio>
#include <cstdlib>
#include <QCoreApplication>
#include <QFile>

static void fail(const char *m)
{
	std::fprintf(stderr, "FAIL: %s\\n", m);
	std::exit(1);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	LedClassController leds(QString::fromUtf8("$fake"));
	leds.bindLed(QStringLiteral("alientek-led0"));

	const LedStatus st = leds.ledStatus();
	if (!st.ok)
		fail("read");
	if (st.trigger != QStringLiteral("heartbeat"))
		fail("parse trigger");
	if (!leds.ledOff())
		fail("off");
	QFile t(QString::fromUtf8("$fake/alientek-led0/trigger"));
	t.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(t.readAll()).trimmed() != QStringLiteral("none"))
		fail("trigger none");
	t.close();
	QFile b(QString::fromUtf8("$fake/alientek-led0/brightness"));
	b.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(b.readAll()).trimmed() != QStringLiteral("0"))
		fail("brightness 0");
	b.close();
	if (!leds.ledOn())
		fail("on");
	b.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(b.readAll()).trimmed() != QStringLiteral("1"))
		fail("brightness 1");
	b.close();
	if (!leds.ledHeartbeat())
		fail("heartbeat");
	t.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(t.readAll()).trimmed() != QStringLiteral("heartbeat"))
		fail("trigger heartbeat");
	std::puts("PASS");
	return 0;
}
EOF

# 需要 Qt6 Core；无则跳过提示
if ! pkg-config --exists Qt6Core 2>/dev/null && ! qmake6 -v >/dev/null 2>&1; then
	echo "SKIP: no Qt6 on host"
	exit 0
fi

CXXFLAGS="$(pkg-config --cflags Qt6Core 2>/dev/null || true)"
LIBS="$(pkg-config --libs Qt6Core 2>/dev/null || echo '-lQt6Core')"
g++ -std=c++17 -fPIC $CXXFLAGS -I"$src" -I"$src_root" -o "$tmp/t" \
	"$tmp/test_leds.cpp" "$src/led_class_controller.cpp" "$sysfs_src/sysfs_file.cpp" $LIBS
"$tmp/t"
