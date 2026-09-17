#!/usr/bin/env bash
# 进入 kas / kas-container 交互 shell，以便手工跑 bitbake
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
# shellcheck source=kas-env.sh
source "$root/scripts/kas-env.sh"

print_usage() {
  echo "用法: $0 [-c '命令']" >&2
  echo "  无参数：进入交互式 kas shell（已配置 alientek-alpha）" >&2
  echo "  -c CMD ：在 kas 环境中执行一条命令后退出" >&2
  echo "示例:" >&2
  echo "  $0" >&2
  echo "  $0 -c 'bitbake python3 -c package_write_rpm'" >&2
  echo "  已在容器内时请直接运行 bitbake，不要再套一层。" >&2
}

refuse_nested_container() {
  if [[ -f /.dockerenv || -f /run/.containerenv ]]; then
    echo "ERROR: 已在 kas 容器内。直接执行 bitbake 即可，勿再运行 $0。" >&2
    exit 1
  fi
}

warn_if_bitbake_locked() {
  local lock="$root/build/bitbake.lock"
  if [[ -f "$lock" ]]; then
    echo "WARN: 发现 build/bitbake.lock，可能已有 bitbake 在跑。" >&2
    echo "  同一时刻只能有一个 bitbake；冲突时先结束旧 shell/构建。" >&2
  fi
}

run_cmd=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      print_usage
      exit 0
      ;;
    -c)
      if [[ $# -lt 2 ]]; then
        echo "ERROR: -c 需要命令字符串" >&2
        exit 1
      fi
      run_cmd="$2"
      shift 2
      ;;
    -c=*)
      run_cmd="${1#-c=}"
      shift
      ;;
    *)
      echo "ERROR: 未知参数: $1" >&2
      print_usage
      exit 1
      ;;
  esac
done

refuse_nested_container
prepare_kas_env
warn_if_bitbake_locked

echo "INFO: 进入 kas shell（配置: $kas_yml）" >&2
if [[ -n "$run_cmd" ]]; then
  if kas_using_host; then
    exec kas shell "$kas_yml" -c "$run_cmd"
  fi
  exec kas-container shell "$kas_yml" -c "$run_cmd"
fi

if kas_using_host; then
  exec kas shell "$kas_yml"
fi
exec kas-container shell "$kas_yml"
