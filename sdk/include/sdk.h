/* Helpers shared by all apps (sdk/src). */
#pragma once
#include "fw.h"

/* Implemented in sdk/src/libc.c (no C library is linked). */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);

/* ---------------------------------------------------------------- APP menu entries
 * FW_MENU_ITEM(symbol, text, open) adds a row to the firmware's "APP" menu; sdk/src/menu.c
 * appends all rows (hook_home_menu_create, patched over the call at 0xD04C04). The entries are
 * collected by the linker script, so several apps can be linked into one image. */
typedef struct {
	const char *symbol; /* UTF-8 of a FontAwesome glyph present in FW_FONT_ICONS */
	const char *text;
	void (*open)(lv_obj_t *return_focus);
} fw_menu_item_t;

#define FW_MENU_ITEM(symbol, text, open)                                                            \
	static const fw_menu_item_t fw_menu_item_##open __attribute__((used, section(".fw_menu"))) = { \
		symbol, text, open}

#define FW_SYMBOL_PLAY "\xEF\x81\x8B"  /* U+F04B */
#define FW_SYMBOL_OK "\xEF\x80\x8C"    /* U+F00C */
#define FW_SYMBOL_AUDIO "\xEF\x80\x81" /* U+F001 */
#define FW_SYMBOL_BARS "\xEF\x83\x89"  /* U+F0C9 */

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

/* ---------------------------------------------------------------- sound effects
 * A 44.1 kHz stream on /dev/audio0 that plays synthesized tones, fed by its own task the way the
 * video player feeds its audio. fw_sound_open returns NULL when the device is in use (e.g. by the
 * music player) or cannot be opened; the other functions accept NULL and then do nothing.
 * A sound starts within about 50 ms of fw_sound_play. */
enum { FW_WAVE_SQUARE, FW_WAVE_TRIANGLE, FW_WAVE_NOISE };

/* One tone: frequency slides linearly from f0 to f1 Hz (noise: the rate it changes at), volume
 * 0-255 decays to 0 over the tone's length. vol 0 gives a rest. */
typedef struct {
	uint16_t f0, f1;
	uint16_t ms;
	uint8_t wave, vol;
} fw_tone_t;

typedef struct fw_sound fw_sound_t;

fw_sound_t *fw_sound_open(void);
/* Plays tones[0..count-1] one after another; up to 4 sounds play at once. The tones must stay
 * valid while they play (static const tables). */
void fw_sound_play(fw_sound_t *s, const fw_tone_t *tones, uint8_t count);
/* Stops the sound task and closes the device. */
void fw_sound_close(fw_sound_t *s);
#define FW_SFX(tones) (tones), (uint8_t)(sizeof(tones) / sizeof((tones)[0]))

/* ---------------------------------------------------------------- small helpers */
char *fw_put_str(char *p, const char *s);
char *fw_put_dec(char *p, uint32_t v);
char *fw_put_hex(char *p, uint32_t v, int digits);

/* Label in the UI font. */
lv_obj_t *fw_label(lv_obj_t *parent, const char *text, lv_color_t color);
/* Plain rectangle without the theme's padding, border or scrolling. */
lv_obj_t *fw_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_color_t color);
