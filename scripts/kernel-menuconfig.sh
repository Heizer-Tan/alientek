#!/usr/bin/env bash
# 在宿主机启动 kas 容器，打开内核 menuconfig（需要交互式 TTY）
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
# shellcheck source=kas-env.sh
source "$root/scripts/kas-env.sh"

print_usage() {
  echo "用法: $0" >&2
  echo "  等价于: kas-container shell kas/alientek-alpha.yml" >&2
  echo "          然后 bitbake virtual/kernel -c menuconfig" >&2
  echo "  请在仓库外的真实终端运行，不要在已有 kas-container shell 里再套一层。" >&2
}

require_tty() {
  if [[ ! -t 0 || ! -t 1 ]]; then
    echo "ERROR: menuconfig 需要交互式终端，请在真实 TTY 里运行。" >&2
    exit 1
  fi
}

# 已在 kas 容器内则直接提示跑 bitbake，避免套娃
refuse_nested_container() {
  if [[ -f /.dockerenv || -f /run/.containerenv ]]; then
    echo "ERROR: 已在容器内。直接执行: bitbake virtual/kernel -c menuconfig" >&2
    exit 1
  fi
}

kas_runtime_busy() {
  if kas_using_host; then
    pgrep -f 'bitbake-server' >/dev/null 2>&1
    return $?
  fi
  "$kas_container_engine" ps --format '{{.Image}}' | grep -q 'kas/kas'
}

# 已有 cooker 时再开客户端会卡在 No reply from server in 30s
fail_if_bitbake_busy() {
  local lock="$root/build/bitbake.lock"
  local sock="$root/build/bitbake.sock"
  if [[ ! -e "$lock" && ! -S "$sock" ]]; then
    return 0
  fi
  if kas_runtime_busy; then
    echo "ERROR: 已有 bitbake 锁，且 kas 容器（或 bitbake-server）仍在运行。" >&2
    echo "  同一时刻只能有一个 bitbake。请回到原来的 shell，或先关掉旧容器。" >&2
    echo "  docker ps    # 看 kas 容器" >&2
    echo "  确认无人使用后: rm -f build/bitbake.lock build/bitbake.sock build/hashserve.sock" >&2
    exit 1
  fi
  echo "WARN: 发现残留 bitbake 锁但无 kas 容器，删除锁文件。" >&2
  rm -f "$lock" "$sock" "$root/build/hashserve.sock"
}

run_menuconfig() {
  local cmd="bitbake virtual/kernel -c menuconfig"
  if kas_using_host; then
    exec kas shell "$kas_yml" -c "$cmd"
  fi
  exec kas-container shell "$kas_yml" -c "$cmd"
}

main() {
  case "${1:-}" in
    -h|--help)
      print_usage
      exit 0
      ;;
    "")
      ;;
    *)
      echo "ERROR: 未知参数 $1" >&2
      print_usage
      exit 2
      ;;
  esac
  require_tty
  refuse_nested_container
  prepare_kas_env
  fail_if_bitbake_busy
  run_menuconfig
}

main "$@"
