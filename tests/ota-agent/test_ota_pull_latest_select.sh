#!/usr/bin/env bash
# 验证 --pull-latest 优先选带时间戳的真实 .swu，而非无时间戳软链名
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
src="$root/meta-alientek/recipes-core/ota/ota-agent/files/src"
bin="$src/ota-agent"

make -C "$src" >/dev/null 2>&1

tmpdir="$(mktemp -d)"
cleanup() {
	[[ -n "${pid:-}" ]] && kill "$pid" 2>/dev/null || true
	rm -rf "$tmpdir"
}
trap cleanup EXIT

touch "$tmpdir/alientek-image-update-imx6ull-alientek-alpha.rootfs.swu"
touch "$tmpdir/alientek-image-update-imx6ull-alientek-alpha.rootfs-20260925084541.swu"
touch "$tmpdir/alientek-image-update-imx6ull-alientek-alpha.rootfs-20260926010000.swu"

port=18766
python3 -m http.server --bind 127.0.0.1 "$port" --directory "$tmpdir" >/dev/null 2>&1 &
pid=$!
sleep 0.4

url="$(OTA_PULL_DRY_RUN=1 "$bin" --pull-latest "http://127.0.0.1:${port}" 2>/dev/null | tr -d '\r' | tail -1)"
expect="http://127.0.0.1:${port}/alientek-image-update-imx6ull-alientek-alpha.rootfs-20260926010000.swu"
if [[ "$url" != "$expect" ]]; then
	echo "expected: $expect" >&2
	echo "got     : $url" >&2
	exit 1
fi
echo "ok: prefer stamped swu over symlink name"
