#!/bin/sh
# 可选：在有宿主机 ssh-keygen 时本地验证预生成逻辑。
# 正式镜像构建走 alientek-image-base.bb 里的 qemu + 目标机 ssh-keygen。
# 用法: pregenerate-ssh-hostkeys.sh ROOTFS [ssh-keygen路径]
set -eu

rootfs="${1:?rootfs required}"
keygen="${2:-ssh-keygen}"
ssh_dir="${rootfs}/etc/ssh"

if ! command -v "${keygen}" >/dev/null 2>&1 && [ ! -x "${keygen}" ]; then
    echo "ERROR: ssh-keygen not found: ${keygen}" >&2
    exit 1
fi

mkdir -p "${ssh_dir}"

gen_key() {
    type="$1"
    key="${ssh_dir}/ssh_host_${type}_key"
    if [ -f "${key}" ]; then
        return 0
    fi
    "${keygen}" -q -t "${type}" -f "${key}" -N ""
    chmod 600 "${key}"
    chmod 644 "${key}.pub"
}

gen_key ed25519
gen_key ecdsa
gen_key rsa

echo "INFO: OpenSSH host keys ready under ${ssh_dir}"
