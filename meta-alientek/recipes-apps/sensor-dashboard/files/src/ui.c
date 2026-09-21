/* SPDX-License-Identifier: MIT */
/* LVGL 页面：主页卡片 + 光感/六轴详情（深色仪表盘风格） */

#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl/lvgl.h"

/* 由 lv_font_dashboard.c 提供，覆盖界面里用到的汉字 */
extern const lv_font_t lv_font_dashboard_20;

enum PageId {
	PAGE_HOME = 0,
	PAGE_AP,
	PAGE_ICM
};

struct UiState {
	struct DashboardCfg cfg;
	lv_obj_t *scrHome;
	lv_obj_t *scrAp;
	lv_obj_t *scrIcm;
	lv_obj_t *lblApSummary;
	lv_obj_t *lblIcmSummary;
	lv_obj_t *lblApDetail;
	lv_obj_t *lblIcmDetail;
	enum PageId page;
};

static struct UiState gUi;

/* 配色：深蓝灰底，避免默认白底+控制台黑块显得脏 */
#define COL_BG       0x121820
#define COL_CARD     0x1E2A38
#define COL_CARD_BOR 0x2E4055
#define COL_ACCENT   0x3A9BDC
#define COL_ACCENT2  0x2BB673
#define COL_TEXT     0xE8EEF4
#define COL_MUTED    0x8FA3B8
#define COL_DANGER   0xE05A5A

static void useDashboardFont(lv_obj_t *obj)
{
	lv_obj_set_style_text_font(obj, &lv_font_dashboard_20, 0);
}

static void styleScreen(lv_obj_t *scr)
{
	lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	useDashboardFont(scr);
}

static void styleTitle(lv_obj_t *lbl)
{
	useDashboardFont(lbl);
	lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
	lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
}

static void showPage(enum PageId id)
{
	gUi.page = id;
	if (id == PAGE_HOME)
		lv_screen_load(gUi.scrHome);
	else if (id == PAGE_AP)
		lv_screen_load(gUi.scrAp);
	else
		lv_screen_load(gUi.scrIcm);
}

static void onApCard(lv_event_t *e)
{
	(void)e;
	showPage(PAGE_AP);
}

static void onIcmCard(lv_event_t *e)
{
	(void)e;
	showPage(PAGE_ICM);
}

static void onBack(lv_event_t *e)
{
	(void)e;
	showPage(PAGE_HOME);
}

static void styleCardBtn(lv_obj_t *btn, uint32_t accent)
{
	lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(btn, 16, 0);
	lv_obj_set_style_border_width(btn, 2, 0);
	lv_obj_set_style_border_color(btn, lv_color_hex(COL_CARD_BOR), 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_pad_all(btn, 20, 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(accent), LV_STATE_PRESSED);
}

/* 卡片：标题 + 摘要同框，避免散落标签 */
static lv_obj_t *makeCard(lv_obj_t *parent, const char *title, uint32_t accent,
			  lv_event_cb_t cb, lv_obj_t **summaryOut)
{
	lv_obj_t *btn;
	lv_obj_t *col;
	lv_obj_t *titleLbl;
	lv_obj_t *sumLbl;
	lv_obj_t *hint;

	btn = lv_button_create(parent);
	lv_obj_set_size(btn, 920, 200);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
	styleCardBtn(btn, accent);

	col = lv_obj_create(btn);
	lv_obj_set_size(col, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(col, 0, 0);
	lv_obj_set_style_pad_all(col, 0, 0);
	lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(col, 10, 0);

	titleLbl = lv_label_create(col);
	useDashboardFont(titleLbl);
	lv_label_set_text(titleLbl, title);
	lv_obj_set_style_text_color(titleLbl, lv_color_hex(COL_TEXT), 0);

	sumLbl = lv_label_create(col);
	useDashboardFont(sumLbl);
	lv_label_set_text(sumLbl, "读取中…");
	lv_obj_set_style_text_color(sumLbl, lv_color_hex(COL_MUTED), 0);
	*summaryOut = sumLbl;

	hint = lv_label_create(col);
	useDashboardFont(hint);
	lv_label_set_text(hint, "点击进入详情 ›");
	lv_obj_set_style_text_color(hint, lv_color_hex(accent), 0);
	return btn;
}

static lv_obj_t *makeBackBtn(lv_obj_t *parent)
{
	lv_obj_t *btn;
	lv_obj_t *lbl;

	btn = lv_button_create(parent);
	lv_obj_set_size(btn, 200, 72);
	lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -28);
	lv_obj_add_event_cb(btn, onBack, LV_EVENT_CLICKED, NULL);
	lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
	lv_obj_set_style_radius(btn, 12, 0);
	lv_obj_set_style_border_width(btn, 2, 0);
	lv_obj_set_style_border_color(btn, lv_color_hex(COL_ACCENT), 0);
	lbl = lv_label_create(btn);
	useDashboardFont(lbl);
	lv_label_set_text(lbl, "‹  返回");
	lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
	lv_obj_center(lbl);
	return btn;
}

static void setSummaryText(lv_obj_t *lbl, const char *okText, int ok)
{
	if (ok) {
		lv_label_set_text(lbl, okText);
		lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
	} else {
		lv_label_set_text(lbl, "设备不可用");
		lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DANGER), 0);
	}
}

static void refreshHome(void)
{
	struct ApSample ap;
	struct IcmSample icm;
	char buf[96];

	if (readApSample(gUi.cfg.apDev, &ap) == 0 && ap.valid) {
		snprintf(buf, sizeof(buf), "ALS  %u    PS  %u", ap.als, ap.ps);
		setSummaryText(gUi.lblApSummary, buf, 1);
	} else {
		setSummaryText(gUi.lblApSummary, NULL, 0);
	}

	if (readIcmSample(gUi.cfg.icmDev, &icm) == 0 && icm.valid) {
		snprintf(buf, sizeof(buf), "az  %.3f g    T  %.1f °C", icm.az_g,
			 icm.temp_c);
		setSummaryText(gUi.lblIcmSummary, buf, 1);
	} else {
		setSummaryText(gUi.lblIcmSummary, NULL, 0);
	}
}

static void refreshAp(void)
{
	struct ApSample ap;
	char buf[192];

	if (readApSample(gUi.cfg.apDev, &ap) == 0 && ap.valid) {
		snprintf(buf, sizeof(buf),
			 "IR\n%u\n\nALS\n%u\n\nPS\n%u", ap.ir, ap.als, ap.ps);
		lv_label_set_text(gUi.lblApDetail, buf);
		lv_obj_set_style_text_color(gUi.lblApDetail,
					    lv_color_hex(COL_TEXT), 0);
	} else {
		lv_label_set_text(gUi.lblApDetail, "设备不可用");
		lv_obj_set_style_text_color(gUi.lblApDetail,
					    lv_color_hex(COL_DANGER), 0);
	}
}

static void refreshIcm(void)
{
	struct IcmSample icm;
	char buf[320];

	if (readIcmSample(gUi.cfg.icmDev, &icm) == 0 && icm.valid) {
		snprintf(buf, sizeof(buf),
			 "加速度 (g)\n"
			 "  X  %7.3f    Y  %7.3f    Z  %7.3f\n\n"
			 "角速度 (°/s)\n"
			 "  X  %7.2f    Y  %7.2f    Z  %7.2f\n\n"
			 "温度    %.2f °C",
			 icm.ax_g, icm.ay_g, icm.az_g, icm.gx_dps, icm.gy_dps,
			 icm.gz_dps, icm.temp_c);
		lv_label_set_text(gUi.lblIcmDetail, buf);
		lv_obj_set_style_text_color(gUi.lblIcmDetail,
					    lv_color_hex(COL_TEXT), 0);
	} else {
		lv_label_set_text(gUi.lblIcmDetail, "设备不可用");
		lv_obj_set_style_text_color(gUi.lblIcmDetail,
					    lv_color_hex(COL_DANGER), 0);
	}
}

static void onTimer(lv_timer_t *t)
{
	(void)t;
	if (gUi.page == PAGE_HOME)
		refreshHome();
	else if (gUi.page == PAGE_AP)
		refreshAp();
	else
		refreshIcm();
}

static void buildHome(void)
{
	lv_obj_t *title;
	lv_obj_t *sub;
	lv_obj_t *card;

	gUi.scrHome = lv_obj_create(NULL);
	styleScreen(gUi.scrHome);

	title = lv_label_create(gUi.scrHome);
	lv_label_set_text(title, "传感器总览");
	styleTitle(title);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

	sub = lv_label_create(gUi.scrHome);
	useDashboardFont(sub);
	lv_label_set_text(sub, "选择模块查看实时数据");
	lv_obj_set_style_text_color(sub, lv_color_hex(COL_MUTED), 0);
	lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 68);

	card = makeCard(gUi.scrHome, "光感  AP3216C", COL_ACCENT2, onApCard,
			&gUi.lblApSummary);
	lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 120);

	card = makeCard(gUi.scrHome, "六轴  ICM20608", COL_ACCENT, onIcmCard,
			&gUi.lblIcmSummary);
	lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 350);
}

static void buildDetailPage(lv_obj_t **scr, const char *titleText,
			    lv_obj_t **detailLbl)
{
	lv_obj_t *title;
	lv_obj_t *panel;

	*scr = lv_obj_create(NULL);
	styleScreen(*scr);

	title = lv_label_create(*scr);
	lv_label_set_text(title, titleText);
	styleTitle(title);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

	panel = lv_obj_create(*scr);
	lv_obj_set_size(panel, 920, 400);
	lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 90);
	lv_obj_set_style_bg_color(panel, lv_color_hex(COL_CARD), 0);
	lv_obj_set_style_radius(panel, 16, 0);
	lv_obj_set_style_border_width(panel, 2, 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(COL_CARD_BOR), 0);
	lv_obj_set_style_pad_all(panel, 28, 0);
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	*detailLbl = lv_label_create(panel);
	useDashboardFont(*detailLbl);
	lv_label_set_text(*detailLbl, "读取中…");
	lv_obj_set_style_text_color(*detailLbl, lv_color_hex(COL_MUTED), 0);
	lv_obj_set_style_text_line_space(*detailLbl, 6, 0);
	lv_obj_center(*detailLbl);

	makeBackBtn(*scr);
}

static void buildAp(void)
{
	buildDetailPage(&gUi.scrAp, "光感  AP3216C", &gUi.lblApDetail);
}

static void buildIcm(void)
{
	buildDetailPage(&gUi.scrIcm, "六轴  ICM20608", &gUi.lblIcmDetail);
}

void uiInit(const struct DashboardCfg *cfg)
{
	unsigned period;

	memset(&gUi, 0, sizeof(gUi));
	gUi.cfg = *cfg;
	buildHome();
	buildAp();
	buildIcm();
	showPage(PAGE_HOME);
	refreshHome();
	period = cfg->homeIntervalMs < cfg->detailIntervalMs
			 ? cfg->homeIntervalMs
			 : cfg->detailIntervalMs;
	if (period < 100)
		period = 100;
	lv_timer_create(onTimer, period, NULL);
}
