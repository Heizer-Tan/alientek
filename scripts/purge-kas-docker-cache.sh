#!/usr/bin/env bash
# 清掉 kas:5.5 最大层的本地坏缓存。毫秒源曾写入错误 digest，后续任何镜像站
# 都会复用同一条坏 blob，表现为「下载 100% 后 unexpected commit digest」。
set -euo pipefail

bad_expected="sha256:ce9286e23a8aa0df40d569accf65efcd02d53da5ec29c3d60d149713895479bc"
bad_actual="sha256:cd58133431c0fbbc4ec792bfe646439b35dd9cb8ce29aaf2a3d7cbb2254e7fbf"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "ERROR: 需要 root，请使用: sudo $0" >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: 未找到 docker" >&2
  exit 1
fi

docker image rm -f \
  ghcr.io/siemens/kas/kas:5.5 \
  ghcr.nju.edu.cn/siemens/kas/kas:5.5 \
  ghcr.1ms.run/siemens/kas/kas:5.5 \
  2>/dev/null || true

if command -v ctr >/dev/null 2>&1; then
  for ns in moby default docker; do
    ctr -n "$ns" content rm "$bad_expected" 2>/dev/null || true
    ctr -n "$ns" content rm "$bad_actual" 2>/dev/null || true
  done
fi

# overlay2 / containerd 内容库里按 digest 命名的文件
while IFS= read -r path; do
  rm -rf "$path"
done < <(find /var/lib/containerd /var/lib/docker -xdev \( -name "*ce9286e23a8a*" -o -name "*cd58133431c0*" \) 2>/dev/null || true)

docker builder prune -af >/dev/null 2>&1 || true
docker system prune -af >/dev/null 2>&1 || true

if command -v systemctl >/dev/null 2>&1; then
  systemctl restart docker || service docker restart
else
  service docker restart
fi

echo "已清理坏层并重启 docker。请再执行："
echo "  docker pull ghcr.nju.edu.cn/siemens/kas/kas:5.5"
echo "若仍失败，改用宿主 kas（不拉镜像）："
echo "  KAS_USE_HOST=1 ./scripts/build.sh"