/*
 * Firmware API for code injected into yp3_2.0.43 (SL6801). Every function here is an address in
 * the vendor firmware, called through a constant function pointer (ldr + blx). LVGL is v8.3 with
 * LV_COLOR_16_SWAP=1, LV_USE_USER_DATA=0 and 16-bit lv_coord_t.
 *
 * Injected code runs from flash and must not have writable globals (.data/.bss are rejected by the
 * linker script); keep state in memory from lv_mem_alloc.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FW_FN(addr, ret, args) ((ret(*) args)((uintptr_t)(addr) | 1u))

/* ---------------------------------------------------------------- LVGL types */

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_font_t lv_font_t;
typedef int16_t lv_coord_t;
typedef uint32_t lv_style_selector_t;

typedef union {
	uint16_t full;
} lv_color_t;

typedef struct {
	uint32_t header; /* cf:5, always_zero:3, reserved:2, w:11, h:11 */
	uint32_t data_size;
	const uint8_t *data;
} lv_img_dsc_t;

#define LV_IMG_CF_TRUE_COLOR 4
#define LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED 6
/* Palette of 16 lv_color32_t (b, g, r, a) followed by rows of 4-bit indices, first pixel in the
 * high nibble. Drawn with per-entry alpha, so opaque entries need a = 0xFF. */
#define LV_IMG_CF_INDEXED_4BIT 9
#define LV_IMG_HEADER(cf, w, h) ((uint32_t)(cf) | ((uint32_t)(w) << 10) | ((uint32_t)(h) << 21))

/* lv_group_t: frozen is bit 0 and editing bit 1 of the byte at offset 0x18. */
typedef struct {
	uint8_t _ll[12];
	lv_obj_t **obj_focus;
	void *focus_cb;
	void *edge_cb;
	uint8_t flags;
} lv_group_t;

/* lv_event_t layout (v8.3 without user data on objects). */
typedef struct {
	lv_obj_t *target;
	lv_obj_t *current_target;
	uint8_t code;
	void *user_data;
	void *param;
} lv_event_t;

typedef void (*lv_event_cb_t)(lv_event_t *e);

#define LV_EVENT_ALL 0
#define LV_EVENT_PRESSED 1
#define LV_EVENT_RELEASED 8
#define LV_EVENT_KEY 13
#define LV_EVENT_DELETE 33

#define LV_KEY_NEXT 9
#define LV_KEY_ENTER 10
#define LV_KEY_PREV 11
#define LV_KEY_RIGHT 19
#define LV_KEY_LEFT 20
#define LV_KEY_ESC 27
/* Physical buttons on this player, as seen in LV_EVENT_KEY. ENTER's KEY event arrives on release;
 * LV_EVENT_PRESSED arrives on press. */
#define FW_KEY_BACK 0x81 /* bottom button; the firmware's pages treat it as "leave page" */
#define FW_KEY_UP 0xA0   /* top button */
#define FW_KEY_CENTER LV_KEY_ENTER
#define FW_KEY_LEFT LV_KEY_LEFT
#define FW_KEY_RIGHT LV_KEY_RIGHT

#define LV_ALIGN_TOP_MID 2
#define LV_ALIGN_CENTER 9
#define LV_ALIGN_BOTTOM_MID 5

#define LV_LABEL_LONG_WRAP 0
#define LV_TEXT_ALIGN_CENTER 2

#define LV_OBJ_FLAG_HIDDEN (1u << 0)
#define LV_OBJ_FLAG_CLICKABLE (1u << 1)
#define LV_OBJ_FLAG_SCROLLABLE (1u << 4)

typedef struct _lv_timer_t {
	uint32_t period;
	uint32_t last_run;
	void (*timer_cb)(struct _lv_timer_t *t);
	void *user_data;
	int32_t repeat_count;
	uint8_t paused;
} lv_timer_t;
typedef void (*lv_timer_cb_t)(lv_timer_t *t);

static inline lv_color_t lv_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	lv_color_t c = {(uint16_t)((v >> 8) | (v << 8))}; /* LV_COLOR_16_SWAP */
	return c;
}

/* ---------------------------------------------------------------- LVGL functions */

#define lv_mem_alloc FW_FN(0x00D3909C, void *, (size_t size))
#define lv_mem_free FW_FN(0x00D39030, void, (void *p))
/* The LVGL heap is a 40 KB TLSF pool shared with the whole UI. The image cache is disabled
 * (LV_IMG_CACHE_DEF_SIZE 0), so images in RAM can be changed in place and redrawn. */

#define lv_disp_get_layer_top FW_FN(0x00D27EF0, lv_obj_t *, (void *disp))
#define lv_disp_get_scr_act FW_FN(0x00D27E88, lv_obj_t *, (void *disp))
#define lv_disp_get_hor_res FW_FN(0x00D377B8, lv_coord_t, (void *disp))
#define lv_disp_get_ver_res FW_FN(0x00D377E0, lv_coord_t, (void *disp))
#define lv_layer_top() lv_disp_get_layer_top(NULL)

#define lv_obj_create FW_FN(0x00D2A250, lv_obj_t *, (lv_obj_t * parent))
#define lv_obj_del FW_FN(0x00D2E3B4, void, (lv_obj_t * obj))
#define lv_obj_set_size FW_FN(0x00D2B27E, void, (lv_obj_t * obj, lv_coord_t w, lv_coord_t h))
#define lv_obj_set_width FW_FN(0x00D2B21E, void, (lv_obj_t * obj, lv_coord_t w))
#define lv_obj_set_height FW_FN(0x00D2B24E, void, (lv_obj_t * obj, lv_coord_t h))
#define lv_obj_set_pos FW_FN(0x00D2B338, void, (lv_obj_t * obj, lv_coord_t x, lv_coord_t y))
#define lv_obj_align FW_FN(0x00D2B406, void, (lv_obj_t * obj, uint8_t align, lv_coord_t x, lv_coord_t y))
#define lv_obj_add_flag FW_FN(0x00D2A268, void, (lv_obj_t * obj, uint32_t f))
#define lv_obj_clear_flag FW_FN(0x00D2A2AE, void, (lv_obj_t * obj, uint32_t f))
#define lv_obj_invalidate FW_FN(0x00D2B770, void, (lv_obj_t * obj))

#define lv_timer_create FW_FN(0x00D39F34, lv_timer_t *, (lv_timer_cb_t cb, uint32_t period, void *user_data))
#define lv_timer_del FW_FN(0x00D39FA4, void, (lv_timer_t * t)) /* allowed inside the timer's own callback */
#define lv_tick_get FW_FN(0x00D37980, uint32_t, (void))
static inline uint32_t lv_tick_elaps(uint32_t prev) { return lv_tick_get() - prev; }
#define lv_obj_add_event_cb \
	FW_FN(0x00D280C8, void *, (lv_obj_t * obj, lv_event_cb_t cb, uint8_t filter, void *user_data))
#define lv_obj_remove_event_cb FW_FN(0x00D28148, bool, (lv_obj_t * obj, lv_event_cb_t cb))

#define lv_obj_set_style_align FW_FN(0x00D2E074, void, (lv_obj_t * obj, uint32_t v, lv_style_selector_t s))
#define lv_obj_set_style_pad_top FW_FN(0x00D2E080, void, (lv_obj_t * obj, lv_coord_t v, lv_style_selector_t s))
#define lv_obj_set_style_pad_bottom FW_FN(0x00D2E08C, void, (lv_obj_t * obj, lv_coord_t v, lv_style_selector_t s))
#define lv_obj_set_style_pad_left FW_FN(0x00D2E098, void, (lv_obj_t * obj, lv_coord_t v, lv_style_selector_t s))
#define lv_obj_set_style_pad_right FW_FN(0x00D2E0A4, void, (lv_obj_t * obj, lv_coord_t v, lv_style_selector_t s))
#define lv_obj_set_style_radius FW_FN(0x00D2E0C8, void, (lv_obj_t * obj, lv_coord_t v, lv_style_selector_t s))
#define lv_obj_set_style_bg_color FW_FN(0x00D2E0F6, void, (lv_obj_t * obj, lv_color_t v, lv_style_selector_t s))
#define lv_obj_set_style_bg_opa FW_FN(0x00D2E118, void, (lv_obj_t * obj, uint8_t v, lv_style_selector_t s))
#define lv_obj_set_style_border_color FW_FN(0x00D2E12C, void, (lv_obj_t * obj, lv_color_t v, lv_style_selector_t s))
#define lv_obj_set_style_border_width FW_FN(0x00D2E158, void, (lv_obj_t * obj, lv_coord_t v, lv_style_selector_t s))
#define lv_obj_set_style_text_color FW_FN(0x00D2E16E, void, (lv_obj_t * obj, lv_color_t v, lv_style_selector_t s))
#define lv_obj_set_style_text_font \
	FW_FN(0x00D2E192, void, (lv_obj_t * obj, const lv_font_t *v, lv_style_selector_t s))
#define lv_obj_set_style_text_align FW_FN(0x00D2E19E, void, (lv_obj_t * obj, uint8_t v, lv_style_selector_t s))

#define lv_label_create FW_FN(0x00D3F1FC, lv_obj_t *, (lv_obj_t * parent))
#define lv_label_set_text FW_FN(0x00D3FCA8, void, (lv_obj_t * obj, const char *text))
#define lv_label_set_long_mode FW_FN(0x00D3FE18, void, (lv_obj_t * obj, uint8_t mode))
#define lv_img_create FW_FN(0x00D3E764, lv_obj_t *, (lv_obj_t * parent))
#define lv_img_set_src FW_FN(0x00D3E77C, void, (lv_obj_t * obj, const void *src))

#define lv_event_get_code FW_FN(0x00D28098, uint8_t, (lv_event_t * e))
#define lv_event_get_param FW_FN(0x00D2809C, void *, (lv_event_t * e))
static inline void *lv_event_get_user_data(lv_event_t *e) { return e->user_data; }

#define lv_group_get_default FW_FN(0x00D284D0, lv_group_t *, (void))
#define lv_group_add_obj FW_FN(0x00D28580, void, (lv_group_t * g, lv_obj_t * obj))
#define lv_group_focus_obj FW_FN(0x00D28714, void, (lv_obj_t * obj))
#define lv_group_get_focused FW_FN(0x00D286AC, lv_obj_t *, (lv_group_t * g))
static inline void lv_group_focus_freeze(lv_group_t *g, bool en)
{
	g->flags = (uint8_t)((g->flags & ~1u) | (en ? 1u : 0u));
}

/* ---------------------------------------------------------------- vendor firmware */

#define fw_log FW_FN(0x000002B0, int, (const char *fmt, ...)) /* boot ROM printf (debug UART) */
#define get_lang_str FW_FN(0x00D01F8C, const char *, (int id))

/* The UI's 12 px, 1 bpp multilingual font (lv_font_t in .data). */
#define FW_FONT_UI ((const lv_font_t *)0x00819C98)
/* 23 px anti-aliased clock font; only has "/0123456789:CELMOW". */
#define FW_FONT_DIGITS ((const lv_font_t *)0x00C610F8)

/* Cortex-M SysTick current value, handy as a random seed. */
#define FW_SYSTICK_VAL (*(volatile uint32_t *)0xE000E018)

/* View context: *(FW_VIEW_CTX) points to a struct holding the key group and per-page state. */
#define FW_VIEW_CTX (*(uint8_t **)0x0081CE5C)
#define fw_view_group() (*(lv_group_t **)(FW_VIEW_CTX + 0x30))

#define lv_obj_get_parent(obj) (*(lv_obj_t **)((uint8_t *)(obj) + 4))
#define lv_obj_get_width FW_FN(0x00D2B4C2, lv_coord_t, (lv_obj_t * obj))
#define lv_obj_set_style_border_side FW_FN(0x00D2E164, void, (lv_obj_t * obj, uint8_t v, lv_style_selector_t s))
#define LV_LABEL_LONG_SCROLL_CIRCULAR 3
#define LV_LABEL_LONG_CLIP 4
#define LV_BORDER_SIDE_BOTTOM 1
#define LV_BORDER_SIDE_TOP 2

/* Vendor list helpers used by the menu pages. */
#define fw_list_item_create FW_FN(0x00D0267C, lv_obj_t *, (lv_obj_t * list)) /* full-width bordered row */
#define fw_list_item_layout FW_FN(0x00D32618, void, (lv_obj_t * obj, int main, int cross, int track)) /* flex */
#define FW_FONT_ICONS ((const lv_font_t *)0x00C810C4) /* 24 FontAwesome symbols (U+F001...) */

/* "APP" page (page id 0x53), opened from the home grid: a list of Alarm and Pictures. */
typedef struct {
	lv_obj_t *item[2];
	lv_obj_t *text[2];
	lv_obj_t *icon[2];
	uint8_t sel;
} fw_home_menu_page_t;
#define fw_home_menu_page() (*(fw_home_menu_page_t **)(FW_VIEW_CTX + 0x1DC))
#define page_home_menu_create FW_FN(0x00D0FC64, int, (void))
#define page_home_menu_event_cb FW_FN(0x00D0FB2C, void, (lv_event_t * e))
#define FW_CALL_HOME_MENU_CREATE 0x00D04C04 /* "bl page_home_menu_create" in view_page_create */

/* Page 0x51 (page_expand_home_create, 2x3 icon grid) exists in the code but is not reachable
 * from the UI on this model. */
