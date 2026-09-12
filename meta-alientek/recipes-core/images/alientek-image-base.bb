DESCRIPTION = "阿尔法第一期基础镜像：串口登录、SSH、双网口工具"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb

# do_rootfs 阶段用 qemu 跑目标机 ssh-keygen，预写入 host key
inherit qemu

IMAGE_FEATURES += "ssh-server-openssh"

CORE_IMAGE_EXTRA_INSTALL += " \
    curl \
    ethtool \
    iproute2 \
    iputils \
    i2c-tools \
    libubootenv \
    libubootenv-bin \
    key-monitor \
    ap3216c-read \
    ap3216c-module \
    ap3216c-logger \
    board-network \
    board-web \
    board-update-tools \
    ota-agent \
    swupdate \
    sqlite3 \
    board-ntpdate \
    busybox-hwclock \
    openssh-keygen \
"

# NFS root 场景下 eth1 由内核 ip= 参数配置，用户态网络文件仅保留 lo
# 构建时用 qemu+目标机 ssh-keygen 预生成 host key，避免板端首次启动缺熵卡死
# （openssh 无 -native；kas 容器也不一定有宿主机 ssh-keygen）

ROOTFS_POSTPROCESS_COMMAND += "alientek_disable_default_services; "
ROOTFS_POSTPROCESS_COMMAND += "alientek_pregenerate_ssh_hostkeys; "

do_rootfs[file-checksums] += "${THISDIR}/files/disable-default-services.sh:True"

alientek_disable_default_services() {
    sh "${THISDIR}/files/disable-default-services.sh" "${IMAGE_ROOTFS}"
}

alientek_pregenerate_ssh_hostkeys() {
    sshdir="${IMAGE_ROOTFS}/etc/ssh"
    mkdir -p "${sshdir}"
    if [ ! -x "${IMAGE_ROOTFS}/usr/bin/ssh-keygen" ]; then
        bbfatal "rootfs 缺少 /usr/bin/ssh-keygen，请确认已安装 openssh-keygen"
    fi

    # qemu_run_binary 带 PSEUDO_UNLOAD=1，目标 ssh-keygen 按宿主机真实 uid 查
    # rootfs passwd；fakeroot 下 id 是 0，需用 PSEUDO_UNLOAD 取真实 uid/gid
    host_uid="$(PSEUDO_UNLOAD=1 id -u)"
    host_gid="$(PSEUDO_UNLOAD=1 id -g)"
    tmp_user="yocto-keygen"
    echo "${tmp_user}:x:${host_uid}:${host_gid}::/tmp:/bin/false" >> "${IMAGE_ROOTFS}/etc/passwd"
    if ! grep -q "^[^:]*:[^:]*:${host_gid}:" "${IMAGE_ROOTFS}/etc/group"; then
        echo "${tmp_user}:x:${host_gid}:" >> "${IMAGE_ROOTFS}/etc/group"
    fi

    for t in ed25519 ecdsa rsa; do
        key="${sshdir}/ssh_host_${t}_key"
        if [ -f "${key}" ]; then
            continue
        fi
        ${@qemu_run_binary(d, '${IMAGE_ROOTFS}', '/usr/bin/ssh-keygen')} -q -t ${t} -f "${key}" -N ''
        chmod 600 "${key}"
        chmod 644 "${key}.pub"
    done

    sed -i "/^${tmp_user}:x:${host_uid}:${host_gid}:/d" "${IMAGE_ROOTFS}/etc/passwd"
    sed -i "/^${tmp_user}:x:${host_gid}:/d" "${IMAGE_ROOTFS}/etc/group"

    bbnote "OpenSSH host keys pregenerated under ${sshdir}"
}
