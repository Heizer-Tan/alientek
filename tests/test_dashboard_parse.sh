#!/usr/bin/env bash
# 宿主编译：AP 文本 parse + iioIcmConvert
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/meta-alientek/recipes-apps/board-ui/dashboard/files/src"
sensors="$src/hw/sensors"
common="$root/meta-alientek/recipes-apps/common"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/test_parse.cpp" <<'EOF'
#define DASHBOARD_TEST_PARSE 1
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
	std::puts("PASS");
	return 0;
}
EOF

g++ -std=c++17 -Wall -Wextra -I"$sensors" -I"$common" -o "$tmp/t" "$tmp/test_parse.cpp"
"$tmp/t"
"$root/tests/test_iio_icm_convert.sh"
