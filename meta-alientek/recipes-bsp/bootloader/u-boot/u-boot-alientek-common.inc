HOMEPAGE = "https://www.denx.de/wiki/U-Boot/WebHome"
SECTION = "bootloaders"

LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://Licenses/README;md5=2ca5f2c35c8cc335f0a19756634782f1"
PE = "1"

DEPENDS += "flex-native bison-native python3-setuptools-native git-native"

B = "${WORKDIR}/build"

inherit pkgconfig

do_configure[cleandirs] = "${B}"

# 发行包不是 git 树；u-boot-tools.inc 的 do_compile 会跑 git diff，必须先补一个最小 .git
do_configure:prepend() {
    if [ ! -d "${S}/.git" ]; then
        git -C "${S}" init --quiet
        git -C "${S}" config user.email "builder@localhost"
        git -C "${S}" config user.name "Yocto Builder"
        git -C "${S}" add -A
        git -C "${S}" commit --quiet -m "u-boot ${PV} from release tarball"
        git -C "${S}" tag -f -a "v${PV}" -m "v${PV}"
    fi
}
