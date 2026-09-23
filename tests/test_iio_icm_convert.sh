#!/usr/bin/env bash
# 宿主单测：iioIcmConvert 换算（无需真实 IIO sysfs）
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
common="$root/meta-alientek/recipes-apps/common"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/test_convert.c" <<'EOF'
#include "iio-icm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *m)
{
	fprintf(stderr, "FAIL: %s\n", m);
	exit(1);
}

static int near(double a, double b, double eps)
{
	return fabs(a - b) < eps;
}

int main(void)
{
	struct IcmIioSample s;
	const double accel_scale = 1.0 / 16384.0;
	const double gyro_scale = 10.0 / 164.0;
	const double temp_scale = 10.0 / 3268.0;
	const double temp_offset = 8170.0; /* 25 * 3268 / 10 */

	s.ax = 16384;
	s.ay = 0;
	s.az = 0;
	s.gx = 164; /* → 约 10 dps（旧 raw*10/164） */
	s.gy = 0;
	s.gz = 0;
	s.temp_raw = 0;
	iioIcmConvert(&s, accel_scale, gyro_scale, temp_scale, temp_offset);
	if (!near(s.ax_g, 1.0, 1e-9))
		fail("accel 16384 → 1g");
	if (!near(s.gx_dps, 10.0, 1e-9))
		fail("gyro 164 → 10 dps (10/164)");
	if (!near(s.temp_c, 25.0, 1e-6))
		fail("temp raw=0 → 25C");
	if (!s.valid)
		fail("valid");
	puts("PASS");
	return 0;
}
EOF

gcc -std=c11 -Wall -Wextra -Werror -I"$common" -o "$tmp/t" \
	"$tmp/test_convert.c" "$common/iio-icm.c" -lm
"$tmp/t"
