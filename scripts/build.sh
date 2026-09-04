#!/usr/bin/env bash
# 用 kas-container 编译阿尔法镜像；不在 WSL2 宿主机原生跑 bitbake
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
# shellcheck source=kas-env.sh
source "$root/scripts/kas-env.sh"

fetch_only=0
forward_args=()

print_usage() {
  echo "用法: $0 [--fetch-only] [kas 额外参数...]" >&2
  echo "  --fetch-only  只检出层并下载源码，不编译" >&2
}

parse_args() {
  local arg
  for arg in "$@"; do
    case "$arg" in
      -h|--help)
        print_usage
        exit 0
        ;;
      --fetch-only)
        fetch_only=1
        ;;
      *)
        forward_args+=("$arg")
        ;;
    esac
  done
}

# 把参数交给 kas；--fetch-only 转成 bitbake --runall=fetch
run_kas() {
  if [[ "$fetch_only" -eq 1 ]]; then
    echo "INFO: --fetch-only，只下载依赖（层 + 源码），不编译。" >&2
    echo "INFO: 源码落在 downloads/。完成后去掉该参数再跑同一脚本即开始编译。" >&2
    exec "$@" "${forward_args[@]}" -- --runall=fetch
  fi
  exec "$@" "${forward_args[@]}"
}

parse_args "$@"
prepare_kas_env

if kas_using_host; then
  echo "INFO: pseudo/大小写问题若出现，再改回容器构建。" >&2
  run_kas kas build "$kas_yml"
fi

run_kas kas-container build "$kas_yml"
