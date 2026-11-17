/* Helpers shared by all apps (sdk/src). */
#pragma once
#include "fw.h"

/* Implemented in sdk/src/libc.c (no C library is linked). */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);

/* ---------------------------------------------------------------- APP menu entries
 * Each app defines app_menu_items[] / app_menu_item_count; sdk/src/menu.c appends them as rows
 * to the firmware's "APP" menu (hook_home_menu_create, patched over the call at 0xD04C04). */
typedef struct {
	const char *symbol; /* UTF-8 of a FontAwesome glyph present in FW_FONT_ICONS */
	const char *text;
	void (*open)(lv_obj_t *return_focus);
} fw_menu_item_t;

extern const fw_menu_item_t app_menu_items[];
extern const uint8_t app_menu_item_count;

#define FW_SYMBOL_PLAY "\xEF\x81\x8B"  /* U+F04B */
#define FW_SYMBOL_OK "\xEF\x80\x8C"    /* U+F00C */
#define FW_SYMBOL_AUDIO "\xEF\x80\x81" /* U+F001 */

/* ---------------------------------------------------------------- full-screen overlays
 * A full-screen object on the top layer that takes all key input until it is deleted.
 * on_delete runs when the root object is deleted for any reason (fw_overlay_close or the firmware
 * tearing down the UI), which is the place to stop timers and free app state. */
typedef struct {
	lv_obj_t *root;
	lv_group_t *group;
	lv_obj_t *return_focus;
	void (*on_delete)(void *ctx);
	void *ctx;
} fw_overlay_t;

fw_overlay_t *fw_overlay_open(lv_obj_t *return_focus, lv_color_t bg, void (*on_delete)(void *ctx), void *ctx);
/* Deletes the overlay (running on_delete) and gives focus back. Do not touch app state afterwards. */
void fw_overlay_close(fw_overlay_t *ov);

/* ---------------------------------------------------------------- small helpers */
char *fw_put_str(char *p, const char *s);
char *fw_put_dec(char *p, uint32_t v);
char *fw_put_hex(char *p, uint32_t v, int digits);

/* Label in the UI font. */
lv_obj_t *fw_label(lv_obj_t *parent, const char *text, lv_color_t color);
/* Plain rectangle without the theme's padding, border or scrolling. */
lv_obj_t *fw_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_color_t color);
