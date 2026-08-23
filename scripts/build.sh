#!/usr/bin/env bash
# 用 kas-container 编译阿尔法镜像；不在 WSL2 宿主机原生跑 bitbake
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if ! command -v kas-container >/dev/null 2>&1; then
  echo "ERROR: 未找到 kas-container。请先安装 kas，并使用容器构建。" >&2
  echo "  pipx install kas   # 或发行版软件包" >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1 && ! command -v podman >/dev/null 2>&1; then
  echo "ERROR: 需要 docker 或 podman。" >&2
  exit 1
fi

exec kas-container build "$root/kas/alientek-alpha.yml" "$@"
