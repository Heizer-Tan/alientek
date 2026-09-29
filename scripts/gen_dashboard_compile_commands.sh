#!/usr/bin/env bash
# 从 Yocto dashboard 构建目录生成 clangd 用 compile_commands.json
# - 源码路径：workdir/src → meta-alientek files/src
# - 编译命令：去掉交叉/GCC 专用参数，便于本机 clangd
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="$repo/build/tmp/work/cortexa7t2hf-neon-imx-poky-linux-gnueabi/dashboard/1.0"
yocto_cc="$work/build/compile_commands.json"
workdir_src="$work/src"
sysroot="$work/recipe-sysroot"
layer_src="$repo/meta-alientek/recipes-apps/board-ui/dashboard/files/src"
out="$layer_src/compile_commands.json"

if [[ ! -f "$yocto_cc" ]]; then
	echo "ERROR: 未找到 $yocto_cc" >&2
	echo "请先 bitbake dashboard 生成 compile_commands.json" >&2
	exit 1
fi

YOCTO_CC="$yocto_cc" WORKDIR_SRC="$workdir_src" LAYER_SRC="$layer_src" \
SYSROOT="$sysroot" OUT="$out" python3 <<'PY'
import json, os, shlex
from pathlib import Path

yocto_cc = Path(os.environ["YOCTO_CC"])
workdir_src = Path(os.environ["WORKDIR_SRC"])
layer_src = Path(os.environ["LAYER_SRC"])
sysroot = Path(os.environ["SYSROOT"])
out = Path(os.environ["OUT"])
ws, ls = str(workdir_src), str(layer_src)

drop_exact = {
	"-mthumb",
	"-fstack-protector-strong",
	"-fvisibility-inlines-hidden",
	"-fcanon-prefix-map",
}
drop_prefix = (
	"--sysroot=",
	"-mfpu=",
	"-mfloat-abi=",
	"-mcpu=",
	"-fmacro-prefix-map=",
	"-fdebug-prefix-map=",
	"-fcanon-prefix-map=",
)

def sanitize(cmd: str) -> str:
	cmd = cmd.replace(ws, ls)
	try:
		parts = shlex.split(cmd)
	except ValueError:
		parts = cmd.split()
	if not parts:
		return cmd
	kept = []
	for p in parts[1:]:
		if p in drop_exact:
			continue
		if any(p.startswith(pref) for pref in drop_prefix):
			continue
		# 丢掉交叉工具链路径参数
		if "arm-poky-linux-gnueabi" in p and p.startswith("-"):
			continue
		kept.append(p)
	# 本机 g++ 优先宿主 libc/libstdc++；sysroot/usr/include 放 idirafter
	# 仅用于解析 Qt 的 <QtCore/...>，避免 ARM stubs-soft 抢先
	extra = ["-idirafter", str(sysroot / "usr/include")]
	return shlex.join(["g++", *extra, *kept])

data = json.loads(yocto_cc.read_text())
remapped = []
for e in data:
	f = e.get("file", "")
	cmd = e.get("command", "")
	if ws in f:
		f = f.replace(ws, ls, 1)
	remapped.append({
		"directory": e.get("directory", ""),
		"command": sanitize(cmd),
		"file": f,
	})
out.write_text(json.dumps(remapped, indent=2) + "\n")
print(f"wrote {out} ({len(remapped)} entries)")
PY

ln -sfn meta-alientek/recipes-apps/board-ui/dashboard/files/src/compile_commands.json \
	"$repo/compile_commands.json"
echo "symlink: $repo/compile_commands.json"
