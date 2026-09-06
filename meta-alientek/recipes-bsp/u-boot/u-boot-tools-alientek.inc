do_compile() {
    # 与上游一致：先刷新 git 索引，避免版本后缀误判为 dirty
    cd "${S}"
    git diff
    cd "${B}"

    oe_runmake -C "${S}" tools-only_defconfig O="${B}"

    # 关闭与板级无关、且当前 native/target 链接会缺 PKCS11 符号的工具
    set_kconfig_disabled "${B}/.config" "CONFIG_CMD_LICENSE"
    set_kconfig_disabled "${B}/.config" "CONFIG_TOOLS_MKEFICAPSULE"
    if [ -n "${SED_CONFIG_EFI}" ]; then
        sed -i ${SED_CONFIG_EFI} "${B}/.config"
    fi

    oe_runmake -C "${S}" olddefconfig O="${B}"
    oe_runmake -C "${S}" cross_tools NO_SDL=1 O="${B}"
}

set_kconfig_disabled() {
    configFile="$1"
    configName="$2"

    if grep -q "^${configName}=" "${configFile}"; then
        sed -i "s/^${configName}=.*/# ${configName} is not set/" "${configFile}"
        return 0
    fi

    if grep -q "^# ${configName} is not set$" "${configFile}"; then
        return 0
    fi

    printf '# %s is not set\n' "${configName}" >> "${configFile}"
}

do_install() {
    install -d "${D}${bindir}"

    install -m 0755 tools/mkimage "${D}${bindir}/uboot-mkimage"
    ln -sf uboot-mkimage "${D}${bindir}/mkimage"

    install -m 0755 tools/mkenvimage "${D}${bindir}/uboot-mkenvimage"
    ln -sf uboot-mkenvimage "${D}${bindir}/mkenvimage"

    install -m 0755 tools/dumpimage "${D}${bindir}/uboot-dumpimage"
    ln -sf uboot-dumpimage "${D}${bindir}/dumpimage"

    install -m 0755 tools/fit_check_sign "${D}${bindir}/uboot-fit_check_sign"
    ln -sf uboot-fit_check_sign "${D}${bindir}/fit_check_sign"

    if [ -f tools/mkeficapsule ]; then
        install -m 0755 tools/mkeficapsule "${D}${bindir}/uboot-mkeficapsule"
        ln -sf uboot-mkeficapsule "${D}${bindir}/mkeficapsule"
    fi
}
