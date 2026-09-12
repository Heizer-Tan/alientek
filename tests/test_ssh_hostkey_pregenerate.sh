#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
recipe="${repo_root}/meta-alientek/recipes-core/images/alientek-image-base.bb"
script="${repo_root}/meta-alientek/recipes-core/images/files/pregenerate-ssh-hostkeys.sh"

grep -q 'alientek_pregenerate_ssh_hostkeys' "${recipe}"
grep -q 'inherit qemu' "${recipe}"
grep -q 'qemu_run_binary' "${recipe}"
grep -q 'openssh-keygen' "${recipe}"
grep -q 'PSEUDO_UNLOAD=1 id -u' "${recipe}"
grep -q 'yocto-keygen' "${recipe}"
! grep -q 'openssh-native' "${recipe}"
! grep -q 'HOSTTOOLS += "ssh-keygen"' "${recipe}"

# 宿主机可选自测脚本
if command -v ssh-keygen >/dev/null 2>&1; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "${tmp}"' EXIT
  sh "${script}" "${tmp}"
  test -f "${tmp}/etc/ssh/ssh_host_ed25519_key"
  test -f "${tmp}/etc/ssh/ssh_host_ecdsa_key"
  test -f "${tmp}/etc/ssh/ssh_host_rsa_key"
fi

echo "ssh host key pregenerate constraints ok"
