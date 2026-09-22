#!/usr/bin/env bash
# 宿主编译 sensors.cpp（TEST_PARSE）并断言解析
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/meta-alientek/recipes-apps/sensor-dashboard/files/src"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/test_parse.cpp" <<'EOF'
#define SENSOR_DASHBOARD_TEST_PARSE 1
#include "sensors.cpp"
#include <cstdio>
#include <cstdlib>

static void fail(const char *m)
{
	std::fprintf(stderr, "FAIL: %s\n", m);
	std::exit(1);
}

int main()
{
	ApSample a{};
	if (!parseApSample("ir=1 als=2 ps=3", &a) || a.ir != 1 || a.als != 2 ||
	    a.ps != 3 || !a.valid)
		fail("ap ok");
	if (parseApSample("bad", &a))
		fail("ap bad");
	IcmSample i{};
	const char *line =
		"ax=1 ay=2 az=3 gx=4 gy=5 gz=6 temp_raw=7 "
		"ax_g=0.1 ay_g=0.2 az_g=0.9 gx_dps=0.0 gy_dps=0.0 gz_dps=0.0 temp_c=25.0";
	if (!parseIcmSample(line, &i) || i.ax != 1 || i.az_g < 0.89 || !i.valid)
		fail("icm ok");
	std::puts("PASS");
	return 0;
}
EOF

g++ -std=c++17 -Wall -Wextra -I"$src" -o "$tmp/t" "$tmp/test_parse.cpp"
"$tmp/t"
