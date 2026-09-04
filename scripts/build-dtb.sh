#!/usr/bin/env bash
# 只编 imx6ull-alientek-alpha.dtb，不跑 bitbake、不编内核。
# 前提：已经完整编过一次 linux（work 目录里要有 .config 和 scripts/dtc）。
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
dt_name="imx6ull-alientek-alpha"
dt_make_target="nxp/imx/${dt_name}.dtb"
tftp_dir=""
make_only=0

print_usage() {
  echo "用法: $0 [--tftp DIR]" >&2
  echo "  从 meta-alientek 拷 dts，只 make 设备树，写入 deploy。" >&2
  echo "  --tftp DIR  额外拷到 DIR/${dt_name}.dtb（例如 /tftpboot）" >&2
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -h|--help) print_usage; exit 0 ;;
      --tftp) tftp_dir="$2"; shift 2 ;;
      --make-only) make_only=1; shift ;;
      *) echo "ERROR: 未知参数 $1" >&2; print_usage; exit 2 ;;
    esac
  done
}

# 内核 out-of-tree 构建目录（含 .config）
find_kernel_build() {
  local matches=()
  shopt -s nullglob
  matches=("$root"/build/tmp/work/*/linux/*/build/.config)
  shopt -u nullglob
  if [[ "${#matches[@]}" -ne 1 ]]; then
    echo "ERROR: 未找到唯一的内核 build 目录（找到 ${#matches[@]} 个）。" >&2
    echo "       请先完整编一次内核：./scripts/build.sh 或 bitbake virtual/kernel" >&2
    exit 1
  fi
  dirname "${matches[0]}"
}

copy_board_dts() {
  local src_dir="$root/meta-alientek/recipes-bsp/device-tree/alientek-aes"
  local ksrc="$root/build/tmp/work-shared/imx6ull-alientek-alpha/kernel-source"
  local dst="$ksrc/arch/arm/boot/dts/nxp/imx"
  if [[ ! -f "$src_dir/${dt_name}.dts" || ! -f "$src_dir/${dt_name}.dtsi" ]]; then
    echo "ERROR: 缺少板级 dts: $src_dir/${dt_name}.dts{,i}" >&2
    exit 1
  fi
  if [[ ! -d "$dst" ]]; then
    echo "ERROR: 内核源码树不存在: $dst" >&2
    exit 1
  fi
  cp -f "$src_dir/${dt_name}.dts" "$src_dir/${dt_name}.dtsi" "$dst/"
}

# 从 Yocto 生成的 run.do_compile 取出交叉工具链环境，不执行编译正文
load_kernel_make_env() {
  local runfile="$1"
  if [[ ! -f "$runfile" ]]; then
    echo "ERROR: 找不到 $runfile" >&2
    exit 1
  fi
  eval "$(grep -E '^export (PATH|ARCH|CROSS_COMPILE)=' "$runfile")"
}

make_dtb_in_tree() {
  local kbuild="$1"
  local ksrc="$2"
  local runfile="$3"
  load_kernel_make_env "$runfile"
  if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
    echo "ERROR: 交叉编译器 ${CROSS_COMPILE}gcc 不在 PATH 中" >&2
    exit 1
  fi
  make -C "$ksrc" O="$kbuild" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
    "$dt_make_target"
}

pick_kas_image() {
  local official="ghcr.io/siemens/kas/kas:5.5"
  local mirror="ghcr.nju.edu.cn/siemens/kas/kas:5.5"
  if [[ -n "${KAS_CONTAINER_IMAGE:-}" ]]; then
    echo "$KAS_CONTAINER_IMAGE"
    return
  fi
  if "$container_engine" image inspect "$official" >/dev/null 2>&1; then
    echo "$official"
  elif "$container_engine" image inspect "$mirror" >/dev/null 2>&1; then
    echo "$mirror"
  else
    echo "$official"
  fi
}

run_make_in_container() {
  local image
  image="$(pick_kas_image)"
  echo "INFO: 在容器里 make ${dt_make_target}（镜像 $image）" >&2
  "$container_engine" run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$root:/work:rw" \
    -w /work \
    "$image" \
    /work/scripts/build-dtb.sh --make-only
}

install_dtb() {
  local built="$1/arch/arm/boot/dts/nxp/imx/${dt_name}.dtb"
  local deploy="$root/build/tmp/deploy/images/imx6ull-alientek-alpha"
  if [[ ! -f "$built" ]]; then
    echo "ERROR: 未生成 $built" >&2
    exit 1
  fi
  mkdir -p "$deploy"
  cp -f "$built" "$deploy/${dt_name}.dtb"
  echo "DTB: $deploy/${dt_name}.dtb"
  if [[ -n "$tftp_dir" ]]; then
    mkdir -p "$tftp_dir"
    cp -f "$built" "$tftp_dir/${dt_name}.dtb"
    echo "TFTP: $tftp_dir/${dt_name}.dtb"
  fi
}

inside_work_container() {
  [[ -d /work/meta-alientek && -d /work/build && "$root" == /work ]]
}

parse_args "$@"

if [[ "$make_only" -eq 1 ]] || inside_work_container; then
  root="/work"
  kbuild="$(find_kernel_build)"
  ksrc="$root/build/tmp/work-shared/imx6ull-alientek-alpha/kernel-source"
  runfile="$(dirname "$kbuild")/temp/run.do_compile"
  make_dtb_in_tree "$kbuild" "$ksrc" "$runfile"
  exit 0
fi

if command -v docker >/dev/null 2>&1; then
  container_engine=docker
elif command -v podman >/dev/null 2>&1; then
  container_engine=podman
else
  echo "ERROR: 需要 docker 或 podman（与 ./scripts/build.sh 相同）。" >&2
  exit 1
fi

copy_board_dts
run_make_in_container
kbuild="$(find_kernel_build)"
install_dtb "$kbuild"
