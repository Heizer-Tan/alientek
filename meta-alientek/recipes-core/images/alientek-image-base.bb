DESCRIPTION = "阿尔法第一期基础镜像：串口登录、SSH、双网口工具"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb

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
    swupdate \
    sqlite3 \
    board-ntpdate \
    busybox-hwclock \
"

# NFS root 场景下 eth1 由内核 ip= 参数配置，用户态网络文件仅保留 lo

ROOTFS_POSTPROCESS_COMMAND += "alientek_disable_default_services; "

do_rootfs[file-checksums] += "${THISDIR}/files/disable-default-services.sh:True"

alientek_disable_default_services() {
    sh "${THISDIR}/files/disable-default-services.sh" "${IMAGE_ROOTFS}"
}
