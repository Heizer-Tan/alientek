# 主线 U-Boot 2025.04（scarthgap 默认仍是 2024.01）
# 用官方 tarball，避免 git clone denx（国内很慢）
HOMEPAGE = "https://www.denx.de/wiki/U-Boot/WebHome"
DESCRIPTION = "U-Boot for Alientek i.MX6ULL Alpha (mainline v2025.04)"
SECTION = "bootloaders"
DEPENDS += "flex-native bison-native python3-setuptools-native"

LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://Licenses/README;md5=2ca5f2c35c8cc335f0a19756634782f1"
PE = "1"

PV = "2025.04"
SRC_URI = "https://ftp.denx.de/pub/u-boot/u-boot-${PV}.tar.bz2"
SRC_URI[sha256sum] = "439d3bef296effd54130be6a731c5b118be7fddd7fcc663ccbc5fb18294d8718"

S = "${WORKDIR}/u-boot-${PV}"
B = "${WORKDIR}/build"

inherit pkgconfig

DEPENDS += "git-native"

do_configure[cleandirs] = "${B}"

# 发行包不是 git 树；u-boot-tools.inc 的 do_compile 会跑 git diff，必须先有 .git
do_configure:prepend() {
    if [ ! -d ${S}/.git ]; then
        git -C ${S} init --quiet
        git -C ${S} config user.email "builder@localhost"
        git -C ${S} config user.name "Yocto Builder"
        git -C ${S} add -A
        git -C ${S} commit --quiet -m "u-boot ${PV} from release tarball"
        git -C ${S} tag -f -a "v${PV}" -m "v${PV}"
    fi
}
