/*
 * Adds an icon to the free 5th slot of the "APP" page. Pressing ENTER on it opens a full-screen
 * overlay that shows the code of each key pressed; the back key (or ESC) closes it.
 */
#include "fw.h"
#include "icon.h"

#define APP_SLOT 4

typedef struct {
	lv_obj_t *root;
	lv_obj_t *keys;
	lv_obj_t *return_focus;
	uint32_t presses;
} app_t;

static char *put_str(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	return p;
}

static char *put_hex(char *p, uint32_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	p = put_str(p, "0x");
	for (int i = digits - 1; i >= 0; i--)
		*p++ = hex[(v >> (4 * i)) & 0xF];
	return p;
}

static char *put_dec(char *p, uint32_t v)
{
	char tmp[10];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		*p++ = tmp[--n];
	return p;
}

static void app_close(app_t *app)
{
	lv_group_t *g = fw_view_group();
	lv_obj_t *back = app->return_focus;
	lv_group_focus_freeze(g, false);
	lv_obj_del(app->root); /* also removes it from the group */
	lv_mem_free(app);
	if (back)
		lv_group_focus_obj(back);
}

static void app_event_cb(lv_event_t *e)
{
	app_t *app = lv_event_get_user_data(e);
	if (lv_event_get_code(e) != LV_EVENT_KEY)
		return;
	uint32_t key = *(uint32_t *)lv_event_get_param(e);
	if (key == FW_KEY_BACK || key == LV_KEY_ESC) {
		app_close(app);
		return;
	}
	char buf[48];
	char *p = put_str(buf, "key ");
	p = put_hex(p, key, 2);
	p = put_str(p, "\npresses: ");
	p = put_dec(p, ++app->presses);
	*p = 0;
	lv_label_set_text(app->keys, buf); /* copies the text */
}

static void app_open(lv_obj_t *return_focus)
{
	app_t *app = lv_mem_alloc(sizeof(app_t));
	if (!app)
		return;
	app->presses = 0;
	app->return_focus = return_focus;

	lv_obj_t *root = lv_obj_create(lv_layer_top());
	app->root = root;
	lv_obj_set_pos(root, 0, 0);
	lv_obj_set_size(root, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
	lv_obj_set_style_radius(root, 0, 0);
	lv_obj_set_style_border_width(root, 0, 0);
	lv_obj_set_style_bg_color(root, lv_rgb(0x10, 0x20, 0x60), 0);
	lv_obj_set_style_bg_opa(root, 255, 0);

	lv_obj_t *title = lv_label_create(root);
	lv_obj_set_style_text_font(title, FW_FONT_UI, 0);
	lv_obj_set_style_text_color(title, lv_rgb(0xFF, 0xD0, 0x40), 0);
	lv_label_set_text(title, "Custom code!");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

	lv_obj_t *keys = lv_label_create(root);
	app->keys = keys;
	lv_obj_set_style_text_font(keys, FW_FONT_UI, 0);
	lv_obj_set_style_text_color(keys, lv_rgb(0xFF, 0xFF, 0xFF), 0);
	lv_obj_set_style_text_align(keys, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(keys, "Press any key.\nBack closes.");
	lv_obj_align(keys, LV_ALIGN_CENTER, 0, 0);

	lv_obj_add_event_cb(root, app_event_cb, LV_EVENT_KEY, app);
	lv_group_t *g = fw_view_group();
	lv_group_add_obj(g, root);
	lv_group_focus_obj(root);
	lv_group_focus_freeze(g, true);
}

static void slot_event_cb(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_KEY && *(uint32_t *)lv_event_get_param(e) == LV_KEY_ENTER) {
		app_open(e->current_target);
		return;
	}
	page_expand_home_event_cb(e);
}

/* Replaces the call to page_expand_home_create in view_page_create. */
__attribute__((used, section(".text.entry"))) int hook_expand_home_create(void)
{
	int ret = page_expand_home_create();
	if (ret != 0)
		return ret;
	lv_obj_t *slot = fw_expand_page()->slot[APP_SLOT];
	lv_img_set_src(slot, &app_icon);
	lv_obj_remove_event_cb(slot, page_expand_home_event_cb);
	lv_obj_add_event_cb(slot, slot_event_cb, LV_EVENT_KEY, NULL);
	return 0;
}
