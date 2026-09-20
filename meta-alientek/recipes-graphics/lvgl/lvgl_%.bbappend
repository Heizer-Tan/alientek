# 阿尔法板：LVGL 走 fbdev+evdev（默认 meta-oe 是 drm）
PACKAGECONFIG = "fbdev"

# 仪表盘 UI 需要更大内存与中文标签字体
LVGL_CONFIG_LV_MEM_SIZE = "(256 * 1024U)"
LVGL_CONFIG_LV_USE_LOG = "1"
LVGL_CONFIG_LV_LOG_PRINTF = "1"

# 用 GitHub archive 代替 git mirror（避免整仓 clone 过慢）
# 对应 commit 与上游 SRCREV 一致：e1c0b21b2723d391b885de4b2ee5cc997eccca91
SRC_URI = "\
	https://github.com/lvgl/lvgl/archive/e1c0b21b2723d391b885de4b2ee5cc997eccca91.tar.gz;downloadfilename=lvgl-e1c0b21b2723d391b885de4b2ee5cc997eccca91.tar.gz \
	file://0002-fix-sdl-handle-both-LV_IMAGE_SRC_FILE-and-LV_IMAGE_S.patch \
	file://0007-fix-cmake-generate-versioned-shared-libraries.patch \
	file://0008-fix-fbdev-set-resolution-prior-to-buffer.patch \
	"
SRC_URI[sha256sum] = "9074c31aa4ec77382b1a055a4d97081e565c96e530d0886acc798f047cc6ba63"
S = "${WORKDIR}/lvgl-e1c0b21b2723d391b885de4b2ee5cc997eccca91"

# 本地固定 tar 包，忽略「勿用 GitHub archive」QA
INSANE_SKIP:${PN} += "src-uri-bad"

do_configure:append() {
    # 启用内置 CJK 字体；sed 里 \& 才是字面量 &（否则 & 会展开成整段匹配）
    sed -r \
        -e 's|^([[:space:]]*#define LV_FONT_SIMSUN_16_CJK[[:space:]]).*|\11|' \
        -e 's|^([[:space:]]*#define LV_FONT_DEFAULT[[:space:]]).*|#define LV_FONT_DEFAULT \&lv_font_simsun_16_cjk|' \
        -i "${S}/lv_conf.h"
}
