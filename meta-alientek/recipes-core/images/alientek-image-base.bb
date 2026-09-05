DESCRIPTION = "阿尔法第一期基础镜像：串口登录、SSH、双网口工具"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb

IMAGE_FEATURES += "ssh-server-openssh"

CORE_IMAGE_EXTRA_INSTALL += " \
    ethtool \
    iproute2 \
    iputils \
    i2c-tools \
    key-monitor \
    ap3216c-read \
    ap3216c-module \
"
