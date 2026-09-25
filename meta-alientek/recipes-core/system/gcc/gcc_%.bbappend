# 目标 gcc 用 gcc-ar rcT 生成 thin archive。并行链接 cc1/cc1plus 时
# libbackend.a 里的 insn-recog.o 等成员对不上，出现 recog、add_clobbers、
# may_alias_p 等未定义引用。改用普通归档。
EXTRA_OEMAKE += "AR_FLAGS=rc"

do_compile:prepend() {
    # 丢掉上次失败留下的 thin libbackend.a，强制按 AR_FLAGS=rc 重打
    rm -f "${B}/gcc/libbackend.a"
}
