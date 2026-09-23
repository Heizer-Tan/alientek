# 文泉驿微米黑：约 5MiB，适合嵌入式；替代 ttf-wqy-zenhei（约 17MiB）
require recipes-graphics/ttf-fonts/ttf.inc

SUMMARY = "WenQuanYi Micro Hei - compact CJK sans font"
HOMEPAGE = "http://wenq.org/"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE_Apache2.txt;md5=400d2f704f0a4f27b035fb613c8ae0ec"

# Debian orig 与 SourceForge 0.2.0-beta 同源；清华镜像拉取更稳
SRC_URI = "https://mirrors.tuna.tsinghua.edu.cn/debian/pool/main/f/fonts-wqy-microhei/fonts-wqy-microhei_0.2.0-beta.orig.tar.gz"
SRC_URI[sha256sum] = "2802ac8023aa36a66ea6e7445854e3a078d377ffff42169341bd237871f7213e"

S = "${WORKDIR}/wqy-microhei"

PACKAGES = "${PN}"
FONT_PACKAGES = "${PN}"
FILES:${PN} = "${datadir}/fonts"
