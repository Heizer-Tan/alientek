#!/usr/bin/env bash
# 用 kas-container 编译阿尔法镜像；不在 WSL2 宿主机原生跑 bitbake
# 若仅设备树有改动，自动只编 DTB（不编 linux/u-boot）
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
# shellcheck source=kas-env.sh
source "$root/scripts/kas-env.sh"

fetch_only=0
force_full=0
target=""
tftp_dir="${TFTP_DIR:-}"
forward_args=()

print_usage() {
  echo "用法: $0 [--fetch-only] [--full] [--tftp DIR] [--target RECIPE|RECIPE] [kas 额外参数...]" >&2
  echo "  --fetch-only     只检出层并下载源码，不编译" >&2
  echo "  --full           强制完整 kas 构建（关闭设备树快路径）" >&2
  echo "  --tftp DIR       设备树快路径时额外拷贝 dtb 到 DIR" >&2
  echo "  --target RECIPE  只编指定 recipe（覆盖 yml 里的镜像目标）" >&2
  echo "  RECIPE           同 --target RECIPE，例如: $0 key-monitor" >&2
  echo "  默认: 若仅 meta-alientek 设备树有改动 → ./scripts/build-dtb.sh" >&2
  echo "        否则编 alientek-image-update（未改动的包走 sstate）" >&2
}

# 解析参数：支持 ./scripts/build.sh key-monitor
parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -h|--help)
        print_usage
        exit 0
        ;;
      --fetch-only)
        fetch_only=1
        shift
        ;;
      --full)
        force_full=1
        shift
        ;;
      --tftp)
        if [[ $# -lt 2 || "$2" == -* ]]; then
          echo "ERROR: --tftp 需要目录" >&2
          exit 1
        fi
        tftp_dir="$2"
        shift 2
        ;;
      --target)
        if [[ $# -lt 2 || "$2" == -* ]]; then
          echo "ERROR: --target 需要 recipe 名" >&2
          exit 1
        fi
        if [[ -n "$target" ]]; then
          echo "ERROR: 只能指定一个 target（已有: $target）" >&2
          exit 1
        fi
        target="$2"
        shift 2
        ;;
      --target=*)
        if [[ -n "$target" ]]; then
          echo "ERROR: 只能指定一个 target（已有: $target）" >&2
          exit 1
        fi
        target="${1#--target=}"
        if [[ -z "$target" ]]; then
          echo "ERROR: --target= 后需要 recipe 名" >&2
          exit 1
        fi
        shift
        ;;
      -*)
        forward_args+=("$1")
        shift
        ;;
      *)
        # 裸 recipe 名视为 --target
        if [[ -n "$target" ]]; then
          echo "ERROR: 多余参数 \"$1\"（target 已是 $target）" >&2
          exit 1
        fi
        target="$1"
        shift
        ;;
    esac
  done
}

# 从 git porcelain 行取出路径（支持 rename: "old -> new"）
porcelain_path() {
  local line="$1"
  local path="${line:3}"
  if [[ "$path" == *" -> "* ]]; then
    path="${path##* -> }"
  fi
  path="${path#\"}"
  path="${path%\"}"
  printf '%s' "$path"
}

# meta-alientek/kas 下是否有「非设备树」改动
has_non_dt_bsp_changes() {
  local line path
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    path="$(porcelain_path "$line")"
    case "$path" in
      meta-alientek/recipes-bsp/device-tree/*) ;;
      meta-alientek/*|kas/*) return 0 ;;
    esac
  done < <(git -C "$root" status --porcelain --untracked-files=all -- meta-alientek kas 2>/dev/null || true)
  return 1
}

# git 工作区是否「只有设备树」在变
only_device_tree_git_dirty() {
  local line path
  local has_dt=0
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    path="$(porcelain_path "$line")"
    case "$path" in
      meta-alientek/recipes-bsp/device-tree/*) has_dt=1 ;;
      meta-alientek/*|kas/*) return 1 ;;
    esac
  done < <(git -C "$root" status --porcelain --untracked-files=all -- meta-alientek kas 2>/dev/null || true)
  [[ "$has_dt" -eq 1 ]]
}

# 设备树源文件比已部署 dtb 更新（含已 commit 但未重新导出的情况）
device_tree_newer_than_deployed_dtb() {
  local dtb="$root/build/tmp/deploy/images/imx6ull-alientek-alpha/imx6ull-alientek-alpha.dtb"
  local dt_dir="$root/meta-alientek/recipes-bsp/device-tree"
  local f
  [[ -d "$dt_dir" ]] || return 1
  [[ -f "$dtb" ]] || return 0
  while IFS= read -r -d '' f; do
    if [[ "$f" -nt "$dtb" ]]; then
      return 0
    fi
  done < <(find "$dt_dir" -type f -print0)
  return 1
}

# 仅设备树改动 → 走 build-dtb.sh
should_fast_dtb() {
  if [[ "$force_full" -eq 1 || "$fetch_only" -eq 1 || -n "$target" ]]; then
    return 1
  fi
  if ! git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    return 1
  fi
  if only_device_tree_git_dirty; then
    return 0
  fi
  # 无其它 BSP 改动，且 dts 比 deploy 的 dtb 新
  if ! has_non_dt_bsp_changes && device_tree_newer_than_deployed_dtb; then
    return 0
  fi
  return 1
}

run_fast_dtb() {
  local -a dtb_args=()
  echo "INFO: 仅设备树有更新，跳过 linux/u-boot，只编 DTB（--full 可强制完整构建）" >&2
  if [[ -n "$tftp_dir" ]]; then
    dtb_args+=(--tftp "$tftp_dir")
  elif [[ -d /tftp ]]; then
    dtb_args+=(--tftp /tftp)
  elif [[ -d /srv/tftp ]]; then
    dtb_args+=(--tftp /srv/tftp)
  fi
  exec "$root/scripts/build-dtb.sh" "${dtb_args[@]}"
}

# 把参数交给 kas；--fetch-only 转成 bitbake --runall=fetch
run_kas() {
  local -a kas_cmd=("$@")
  local -a target_opts=()
  local selected_target="${target:-alientek-image-update}"

  target_opts=(--target "$selected_target")
  echo "INFO: 只构建 target=$selected_target（其它未改 recipe 尽量走 sstate）" >&2

  if [[ "$fetch_only" -eq 1 ]]; then
    echo "INFO: --fetch-only，只下载依赖（层 + 源码），不编译。" >&2
    echo "INFO: 源码落在 downloads/。完成后去掉该参数再跑同一脚本即开始编译。" >&2
    exec "${kas_cmd[@]}" "${target_opts[@]}" "$kas_yml" "${forward_args[@]}" -- --runall=fetch
  fi
  exec "${kas_cmd[@]}" "${target_opts[@]}" "$kas_yml" "${forward_args[@]}"
}

parse_args "$@"

if should_fast_dtb; then
  run_fast_dtb
fi

prepare_kas_env

if kas_using_host; then
  echo "INFO: pseudo/大小写问题若出现，再改回容器构建。" >&2
  run_kas kas build
fi

run_kas kas-container build
