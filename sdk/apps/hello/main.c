/*
 * Adds a "Custom app" row to the "APP" menu (Alarm, Pictures). Pressing ENTER on it opens a
 * full-screen overlay that shows the code of each key pressed; the back key (or ESC) closes it.
 */
#include "fw.h"

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

static void item_event_cb(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_KEY && *(uint32_t *)lv_event_get_param(e) == LV_KEY_ENTER) {
		app_open(e->current_target);
		return;
	}
	page_home_menu_event_cb(e); /* navigation and back work regardless of which row has focus */
}

/* Adds a row styled like the page's own rows (see page_home_menu_create). */
static void add_menu_item(lv_obj_t *list, const char *symbol, const char *text)
{
	lv_coord_t h = lv_disp_get_ver_res(NULL);
	lv_obj_t *item = fw_list_item_create(list);
	lv_obj_set_height(item, h / 7);
	lv_obj_set_style_border_side(item, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_add_event_cb(item, item_event_cb, LV_EVENT_KEY, NULL);
	lv_group_add_obj(fw_view_group(), item);

	lv_obj_t *icon = lv_label_create(item);
	lv_obj_set_style_text_font(icon, FW_FONT_ICONS, 0);
	lv_label_set_text(icon, symbol);

	lv_obj_t *label = lv_label_create(item);
	lv_obj_set_width(label, lv_disp_get_hor_res(NULL) - 4 - lv_obj_get_width(icon));
	lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
	lv_label_set_text(label, text);
	fw_list_item_layout(item, 0, 0, 2);
}

/* Replaces the call to page_home_menu_create in view_page_create. */
__attribute__((used, section(".text.entry"))) int hook_home_menu_create(void)
{
	int ret = page_home_menu_create();
	if (ret != 0)
		return ret;
	lv_obj_t *list = lv_obj_get_parent(fw_home_menu_page()->item[0]);
	add_menu_item(list, "\xEF\x80\x8C" /* U+F00C check mark */, "Custom app");
	return 0;
}
