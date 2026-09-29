SUMMARY = "持久 data 分区：扩容、fstab 与 LED DTB 切换脚本"
DESCRIPTION = "LABEL=data 挂载于 /data；OTA 不覆盖。首启可将分区扩到盘尾。"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} = "libubootenv-bin parted e2fsprogs util-linux-blkid"

SRC_URI = " \
    file://switch-led-dtb \
    file://switch-led-dtb.wrapper \
    file://board-data-grow \
    file://board-data.init \
"

inherit update-rc.d

INITSCRIPT_NAME = "board-data"
# 早于多数业务；在 local_fs 之后扩展 data
INITSCRIPT_PARAMS = "defaults 15"

do_install() {
    # data 分区内容（wic --rootfs-dir=${IMAGE_ROOTFS}/data）
    install -d "${D}/data/bin"
    install -m 0755 "${WORKDIR}/switch-led-dtb" "${D}/data/bin/switch-led-dtb"

    # rootfs：封装、扩容、seed、init
    install -d "${D}${sbindir}"
    install -m 0755 "${WORKDIR}/switch-led-dtb.wrapper" \
        "${D}${sbindir}/switch-led-dtb"
    install -m 0755 "${WORKDIR}/board-data-grow" \
        "${D}${sbindir}/board-data-grow"

    install -d "${D}${datadir}/board-data"
    install -m 0644 "${WORKDIR}/switch-led-dtb" \
        "${D}${datadir}/board-data/switch-led-dtb"

    install -d "${D}${sysconfdir}/init.d"
    install -m 0755 "${WORKDIR}/board-data.init" \
        "${D}${sysconfdir}/init.d/board-data"
}

# 构建期把 LABEL=data 写入 fstab（根分区内；挂载点由 init mkdir）
pkg_postinst:${PN}() {
#!/bin/sh
    fstab="/etc/fstab"
    if [ -n "$D" ]; then
        fstab="$D/etc/fstab"
    fi
    if [ -f "$fstab" ] && ! grep -q 'LABEL=data' "$fstab"; then
        printf '\n# board-data：持久分区（OTA 不覆盖）\n' >> "$fstab"
        printf 'LABEL=data  /data  ext4  defaults,nofail  0  2\n' >> "$fstab"
    fi
}

FILES:${PN} += " \
    /data \
    /data/bin \
    /data/bin/switch-led-dtb \
    ${datadir}/board-data \
    ${sysconfdir}/init.d/board-data \
"
