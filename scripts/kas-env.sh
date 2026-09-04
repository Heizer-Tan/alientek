# 被 source：选定 kas / kas-container 与国内镜像。
# 调用方须已设置 root，并 cd 到仓库根目录。
# shellcheck shell=bash

kas_yml="${root}/kas/alientek-alpha.yml"
kas_image_official="ghcr.io/siemens/kas/kas:5.5"
kas_image_mirror="ghcr.nju.edu.cn/siemens/kas/kas:5.5"
kas_container_engine=""

kas_using_host() {
  [[ "${KAS_USE_HOST:-0}" == "1" ]]
}

require_kas_command() {
  if kas_using_host; then
    if ! command -v kas >/dev/null 2>&1; then
      echo "ERROR: KAS_USE_HOST=1 但未找到 kas。请先: uv tool install kas" >&2
      exit 1
    fi
    echo "INFO: KAS_USE_HOST=1，在宿主跑 kas（不拉 Docker 镜像）。" >&2
    return 0
  fi
  if ! command -v kas-container >/dev/null 2>&1; then
    echo "ERROR: 未找到 kas-container。请先安装 kas，并使用容器构建。" >&2
    echo "  pipx install kas   # 或发行版软件包" >&2
    exit 1
  fi
  if command -v docker >/dev/null 2>&1; then
    kas_container_engine=docker
  elif command -v podman >/dev/null 2>&1; then
    kas_container_engine=podman
  else
    echo "ERROR: 需要 docker 或 podman。" >&2
    exit 1
  fi
}

kas_has_image() {
  "$kas_container_engine" image inspect "$1" >/dev/null 2>&1
}

# 国内 ghcr.io 常 TLS 超时。优先南大镜像；可用 KAS_CONTAINER_IMAGE 覆盖。
export_kas_container_image() {
  if kas_using_host || [[ -n "${KAS_CONTAINER_IMAGE:-}" ]]; then
    return 0
  fi
  if kas_has_image "$kas_image_official"; then
    export KAS_CONTAINER_IMAGE="$kas_image_official"
  elif kas_has_image "$kas_image_mirror"; then
    "$kas_container_engine" tag "$kas_image_mirror" "$kas_image_official"
    export KAS_CONTAINER_IMAGE="$kas_image_official"
  else
    export KAS_CONTAINER_IMAGE="$kas_image_mirror"
    echo "INFO: 本地没有 ${kas_image_official}，从南大镜像拉取 ${kas_image_mirror}" >&2
    echo "INFO: 不要用 ghcr.1ms.run 拉此镜像，该源曾把最大层下坏（digest 不匹配）。" >&2
    echo "INFO: 若南大 IPv6 被 RST：echo '210.28.130.20 ghcr.nju.edu.cn' | sudo tee -a /etc/hosts" >&2
  fi
}

prepare_kas_env() {
  require_kas_command
  export_kas_container_image
}
