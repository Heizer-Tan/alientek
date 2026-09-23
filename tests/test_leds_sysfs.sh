#!/usr/bin/env bash
# 宿主单测：LED sysfs 状态机（假目录）
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/meta-alientek/recipes-apps/sensor-dashboard/files/src"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fake="$tmp/leds"
mkdir -p "$fake/alientek-led0"
echo 'none heartbeat [heartbeat]' >"$fake/alientek-led0/trigger"
echo 1 >"$fake/alientek-led0/brightness"
echo 1 >"$fake/alientek-led0/max_brightness"

cat >"$tmp/test_leds.cpp" <<EOF
#include "leds.hpp"
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
	const QString root = QString::fromUtf8("$fake");
	LedStatus st;
	if (!ledReadStatus(root, QStringLiteral("alientek-led0"), &st) || !st.ok)
		fail("read");
	if (st.trigger != QStringLiteral("heartbeat"))
		fail("parse trigger");
	if (!ledSetManual(root, QStringLiteral("alientek-led0"), false))
		fail("off");
	QFile t(root + QStringLiteral("/alientek-led0/trigger"));
	t.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(t.readAll()).trimmed() != QStringLiteral("none"))
		fail("trigger none");
	t.close();
	QFile b(root + QStringLiteral("/alientek-led0/brightness"));
	b.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(b.readAll()).trimmed() != QStringLiteral("0"))
		fail("brightness 0");
	b.close();
	if (!ledSetManual(root, QStringLiteral("alientek-led0"), true))
		fail("on");
	b.open(QIODevice::ReadOnly);
	if (QString::fromUtf8(b.readAll()).trimmed() != QStringLiteral("1"))
		fail("brightness 1");
	b.close();
	if (!ledRestoreHeartbeat(root, QStringLiteral("alientek-led0")))
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
g++ -std=c++17 -fPIC $CXXFLAGS -I"$src" -o "$tmp/t" "$tmp/test_leds.cpp" "$src/leds.cpp" $LIBS
"$tmp/t"
