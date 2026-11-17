# SL6801 MP3 player: firmware reverse engineering and custom apps

Tools and notes for modifying the firmware of a cheap AliExpress MP3 player with a 1.8" color
screen, built around a chip labeled "Jointbees MP3 R14181B6A0" (really a Smartlink **SL6801**).
The vendor firmware is version `yp3_2.0.43`.

What works so far, all tested on the device:

- **Assets**: unpack and repack images, UI strings (10 languages), fonts and the alarm tone, with
  all checksums fixed. Edited data goes to free flash.
- **Custom code**: C apps are compiled to run in place from free flash and call the firmware's own
  LVGL and UI functions. A patched call adds their rows to the firmware's "APP" menu.
- **Apps**: *Key test* (shows key codes), *Flutter* (a side-scrolling flapping game) and
  *Mp3tris* (a falling-block puzzle).
- **Flashing from Windows** over USB with `smtlink_dump`.
- **Ghidra** project setup with ~350 functions named automatically, plus headless decompilation
  scripts.

`firmware_2M.bin` is the original 2 MB flash dump of this unit. Keep it unmodified: every image is
built from it.

## Contents

- [Quick start](#quick-start)
- [Repository layout](#repository-layout)
- [Workflows](#workflows): [assets](#editing-assets), [apps](#building-apps),
  [flashing](#flashing), [writing an app](#writing-a-new-app),
  [finding firmware functions](#finding-firmware-functions)
- [Reference](#reference): [hardware](#hardware), [memory map](#memory-map),
  [flash layout](#flash-layout), [software stack](#software-stack), [assets](#assets-inside-firm),
  [SDK internals](#sdk-internals), [buttons](#buttons-and-key-events), [Ghidra](#ghidra),
  [Windows flashing setup](#windows-flashing-setup)
- [Gotchas](#gotchas) and [open questions](#open-questions)

## Quick start

The environment is Windows with Git Bash; paths below are the ones used here.

| Need | Used here |
|---|---|
| Python 3 venv in `.venv` with Pillow (`ziglang` optional) | `python -m venv .venv` then `.venv/Scripts/python -m pip install -r requirements.txt` |
| Arm GCC | xPack arm-none-eabi-gcc 15.2.1 in `C:/Tools/arm-gnu-toolchain` (or set `ARM_GCC_BIN` to its `bin`) |
| Flasher | `smartlink_flash/smtlink_dump.exe`, built in MSYS2 (see [Windows flashing setup](#windows-flashing-setup)) |
| USB driver | WinUSB for `301A:2800`, installed with Zadig |
| Ghidra (optional) | `C:/Tools/ghidra` (or `GHIDRA_HOME`), JDK via `JAVA_HOME` |

Build the apps, pack them with the edited assets in `work/`, and flash:

```sh
.venv/Scripts/python sdk/build.py sdk/apps/flutter sdk/apps/mp3tris sdk/apps/keytest
.venv/Scripts/python tools/assets.py pack firmware_2M.bin work out.bin --app sdk/build/app.json
# put the player in bootloader mode, then:
smartlink_flash/smtlink_dump.exe init write_flash 0 0 0 out.bin
```

The new rows appear under **APP** on the home screen, below Alarm and Pictures.

Before the first write to a new unit, back up its flash:
`smartlink_flash/smtlink_dump.exe init flash_id read_flash 0 2M backup.bin`.
To restore the stock firmware, flash `firmware_2M.bin` (or the backup) the same way.

## Repository layout

| Path | What |
|---|---|
| `firmware_2M.bin` | Original flash dump (tracked; never modified) |
| `work/` | Unpacked assets (`assets.py unpack`), including a custom 96x96 welcome banner |
| `tools/slfw.py` | Flash image parser: layout, partitions, checksums |
| `tools/assets.py` | Asset unpack/pack/preview, and app injection (`pack --app`) |
| `tools/img2c.py` | PNG to `lv_img_dsc_t` C header (used by the SDK build) |
| `tools/xref.py` | Finds BL/B.W calls to given addresses, or (`--data`) code that loads a given pointer such as a string |
| `tools/avicheck.py` | Checks whether an `.avi` file meets the firmware video player's requirements |
| `tools/find_lv_images.py`, `find_lv_fonts.py`, `render.py`, `findbase.py`, `survey.py` | Analysis helpers used while mapping the firmware (each takes the dump as first argument) |
| `sdk/include/fw.h` | Firmware API: LVGL and vendor functions by address, types, constants |
| `sdk/include/sdk.h`, `sdk/src/` | Shared app code: APP menu hook, full-screen overlay, helpers, libc |
| `sdk/app.ld`, `sdk/build.py`, `sdk/patches.json` | Linker script, build script, firmware call sites to patch |
| `sdk/apps/keytest`, `flutter`, `mp3tris` | Apps |
| `sdk/test/sfx_preview.c` | Renders an app's sound effects to a WAV file on the PC |
| `ghidra/` | Headless Ghidra import and decompile scripts (`project/`, `input/`, `out/` are generated) |
| `smartlink_flash/` | Local clone and build of the flasher (not in git) |

## Workflows

All Python tools run with the project venv, e.g. `.venv/Scripts/python -I tools/assets.py ...`.

### Editing assets

```sh
.venv/Scripts/python -I tools/assets.py unpack firmware_2M.bin work   # only needed once
# edit files in work/
.venv/Scripts/python -I tools/assets.py pack firmware_2M.bin work out.bin
```

`pack` applies everything that differs from the original, fixes all checksums, prints the changed
flash ranges and the command to flash the result.

- **Strings**: edit `work/strings.json` (10 languages x 219 strings). New text is stored in free
  flash and the string table is repointed.
- **Images**: edit the PNGs in `work/images/`, keeping the file name. A same-size image is
  replaced in place. A new size is stored in free flash and the descriptor updated, but the UI
  layout code may still assume the old size (the welcome banner `img_a5ff8_84x20.png` works fine
  at 96x96). Pixels with alpha < 128 become transparent in chroma-keyed images.
- **Fonts**: set `"rebuild"` in `work/fonts/<font>.json`, e.g.
  `{"ttf": "C:/Windows/Fonts/arial.ttf", "size": 12, "mode": "add_missing", "add_chars": "ŹŻ"}`.
  `add_missing` renders only codepoints the font lacks (for the UI font this includes, by default,
  every character used in `strings.json`); `replace` re-renders every glyph the TTF has. Optional
  keys: `y_shift`, `line_height`, `base_line`, `include_strings`. New glyph bitmaps are appended in
  free flash; the original bitmaps stay where they are.
- **Tone**: replace `work/tone/alarm1.mp3`.

`assets.py preview <image.bin> font_464b0 out.png "text" ...` renders text with a firmware font
(using LVGL's glyph placement) to check font edits before flashing.

`slfw.py <image> info` prints the layout and verifies all checksums; `slfw.py <image> extract <dir>`
writes the partitions; `slfw.py <image> fixcrc <out>` recomputes the checksums.

### Building apps

```sh
.venv/Scripts/python sdk/build.py [--zig] [-o OUT] <app dir>...
```

- Links one or more apps into one image. Their menu rows appear in the order the apps are given,
  e.g. `sdk/build.py sdk/apps/flutter sdk/apps/mp3tris` for just the two games.
- Uses arm-none-eabi-gcc by default, or `zig cc` (from the `ziglang` pip package) with `--zig`.
  Both produce working code (Thumb-2, Cortex-M4, soft-float ABI).
- Each app directory's `*.c` files are compiled separately with the app's own include path. Each
  `assets/X.png` becomes a header `X.h` defining an `lv_img_dsc_t` named `img_X`, visible to that
  app only.
- `sdk/src/*.c` is linked into every image.
- Output in `sdk/build/` (default): `app.bin`, `app.json`, and with gcc `app.map` and `app.lst`
  (disassembly interleaved with source, useful for checking firmware addresses).

`app.json` holds the load address, flash offset and the BL patches. Pass it to
`assets.py pack ... --app sdk/build/app.json`. The pack step:
- checks that the target flash region is erased in the original dump;
- checks that each patched BL still calls the expected original function;
- writes the code and the patched BLs.

### Flashing

1. Put the player in bootloader mode: turn it off, then hold the boot key while connecting USB.
   It enumerates as `301A:2800`.
2. Run `smartlink_flash/smtlink_dump.exe init write_flash 0 0 0 out.bin`.

Flash the whole image with `write_flash 0 0 0 <file>`. Only sectors that differ are rewritten,
so this is fast.

The USB bootloader is in the chip's boot ROM, so a bad flash can always be recovered by
flashing a good image again. This has happened once and was recovered this way.

### Writing a new app

Create `sdk/apps/<name>/main.c`. A minimal full-screen app that closes on Back:

```c
#include "sdk.h"

typedef struct {
	fw_overlay_t *ov;
} app_t;

static void on_delete(void *ctx)
{
	lv_mem_free(ctx); /* runs however the overlay goes away; stop timers here too */
}

static void key_cb(lv_event_t *e)
{
	app_t *app = lv_event_get_user_data(e);
	uint32_t key = *(uint32_t *)lv_event_get_param(e);
	if (key == FW_KEY_BACK || key == LV_KEY_ESC)
		fw_overlay_close(app->ov); /* frees app via on_delete */
}

static void open(lv_obj_t *return_focus)
{
	app_t *app = lv_mem_alloc(sizeof(app_t));
	if (!app)
		return;
	app->ov = fw_overlay_open(return_focus, lv_rgb(0x10, 0x20, 0x60), on_delete, app);
	if (!app->ov) {
		lv_mem_free(app);
		return;
	}
	lv_obj_align(fw_label(app->ov->root, "Hello", lv_rgb(0xFF, 0xFF, 0xFF)), LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(app->ov->root, key_cb, LV_EVENT_KEY, app);
}

FW_MENU_ITEM(FW_SYMBOL_OK, "Hello", open);
```

Rules and tips:

- **No writable globals.** Code runs from flash; the linker script rejects `.data` and `.bss`.
  Keep all state in memory from `lv_mem_alloc` and free it in `on_delete`. `static const` tables
  are fine.
- **Memory is tight.** The LVGL heap is 40 KB for the whole UI. Prefer a few objects; for many
  small elements draw into an indexed image instead (see Mp3tris).
- **Animation**: use `lv_timer_create` and `lv_tick_get`; see Flutter for a fixed-step loop that
  catches up when the timer fires late.
- **Input**: see [Buttons and key events](#buttons-and-key-events). Center needs special handling
  because its `LV_EVENT_KEY` only arrives on release.
- **Menu icons**: `FW_FONT_ICONS` has only these FontAwesome codepoints: F001 F008 F00C F013 F01D
  F028 F02D F02E F04A F04B F04C F04E F095 F09B F0C7 F0C9 F115 F130 F131 F1C5 F21D F260 F28C F294.
  `sdk.h` defines `FW_SYMBOL_AUDIO`, `OK`, `PLAY` and `BARS`.
- **Sprites**: put PNGs in `assets/`; `#include "X.h"` and use `&img_X` with `lv_img_set_src`.
  Use hard alpha (the firmware draws chroma-keyed images, not alpha blending). Flutter's `art.py`
  shows how to draw sprites with Pillow.
- **Sound effects**: see [Audio](#audio). The usual pattern:
  - `fw_sound_open()` when the app opens;
  - `fw_sound_play(snd, FW_SFX(table))` for each effect;
  - `fw_sound_close()` in `on_delete`.

  Effects are tables of `fw_tone_t`. Both games keep theirs in `sfx.h`. To listen to them on the
  PC, build `sdk/test/sfx_preview.c` (instructions at its top), which writes all of an app's
  effects to a WAV file.
- **Host testing**: game logic can be compiled and run on the PC with MSYS2 gcc. `#undef` the
  `fw.h` macros the app uses, define stubs, then `#include "main.c"`.
  `sdk/apps/mp3tris/test/host_test.c` drives the game with fake events and ticks, and `show.py`
  renders its image dumps to PNG.

### Finding firmware functions

1. Build the Ghidra project once: `ghidra/import.sh`.
2. Look up names in `ghidra/out/sl6801_app.bin_functions.csv`. Functions are named from their
   `__func__` strings, which covers most of LVGL's core and the vendor UI pages.
3. Decompile without the GUI: `ghidra/decompile.sh app out.c <name|0xaddr>...`.
4. Find callers: `.venv/Scripts/python tools/xref.py firmware_2M.bin 0xADDR...`. To find the
   code that uses a string, search the dump for the string's offset and look up its address
   (`0xC00000 + offset`) with `tools/xref.py --data firmware_2M.bin 0xADDR`. Decompiling the
   reported instruction address shows the function.
5. Add the function to `fw.h` as `#define name FW_FN(0xADDR, ret, (args))`. Compare against the
   LVGL v8.3 source to confirm the signature, then check the literal in `sdk/build/app.lst`
   after building.

Unnamed LVGL functions can usually be identified from a named caller. For example,
`lv_img_set_src` calls `lv_obj_invalidate` first and last. Style setters can be identified by
their v8.3 property IDs (e.g. `BG_COLOR` 0x20, `TEXT_COLOR` 0x457, `TEXT_FONT` 0x1459).

## Reference

### Hardware

From photos of the board:

| Part | Identification |
|---|---|
| SoC | "Jointbees MP3 R14181B6A0", QFN-48, really a Smartlink **SL6801** (string `SL6801` in the bootloader). 2 MB flash in the package, no external flash chip. |
| CPU | ARM Cortex-M4 class with FPU (VFP instructions present). Log strings mention CPU0 and CPU1 (dual core). |
| Display | 128x160 ST7789 TFT on a 20-pin FPC (`st7789s_lcd_init`), RGB565 |
| FM radio | RDA5807M (marked `5807M`) |
| Speaker amp | 8002D (SOP-8, class AB) |
| Other | microSD slot (SDIO), mic (MIC1), PCB trace antenna (Bluetooth), 5 buttons, slide switch, USB-C, 3.5 mm jack, 150 mAh LiPo |
| Test pads | `5V DM DP G ADC` (USB + ADC) next to R27-R30 |

USB IDs: `301A:2800` in bootloader mode (boot ROM, serial `20201111000001`), `301A:2801` as a card
reader in normal operation.

### Memory map

| Address | What |
|---|---|
| `0x00000000` | Boot ROM (ROM memcpy at `0xB2A`, printf to the debug UART at `0x2B0`) |
| `0x00800000`-`~0x0087FFFF` | RAM. App initial SP `0x0086B710`, bootloader SP `0x0083BE90` |
| `0x00804800` | App vector table (FIRM RAM block), reset at `0x00804C40` |
| `0x0080CC40`-`0x0081AAEC` | App `.data`, copied from flash `0x39470`-`0x4731C` at startup (RAM = flash offset + `0x7D37D0`) |
| `0x0081AAEC`-`0x0086B710` | App `.bss` (zeroed by Reset_Handler) |
| `0x0081FBC0` | Bootloader vector table (loaded from flash `0x60`), reset at `0x00820000` |
| `0x00876000` | LVGL heap (40 KB TLSF pool) |
| `0x00C00000` | SPI flash, memory mapped (XIP). Flash offset `X` is at `0xC00000 + X` |
| `0x40000000`-`0x400FFFFF` | Peripheral registers |
| `0xE000E018` | SysTick current value (used as a random seed) |

### Flash layout

| Flash offset | Size | Content |
|---|---|---|
| `0x000000` | 0x60 | `HLKJ` header: +4 load address, +8 entry, +0xC code offset, +0x10 code size, +0x14 CRC16, +0x20 partition table offset |
| `0x000060` | 0xC8EC | Bootloader (FreeRTOS-based; LCD/SD init, firmware update from PC/SD) |
| `0x00D000` | 0x40 | Partition table: u32 count (3), ..., 16-byte entries from +0x10: name, offset, size, CRC16 |
| `0x00E000` | 0x174270 | `FIRM` main application |
| `0x183000` | 0x2038 | `TONE`: u32 size + name (`alarm1.mp3`), data at +0x400 (MP3 with ID3 tag) |
| `0x185038` | ~475 KB | Free (0xFF). Used by these tools: asset data from `0x186000`, app code from `0x1D0000` |
| `0x1FC000` | 0x4000 | `PSMP` settings (non-volatile storage, not checksummed) |

The free area is not touched by the vendor firmware as far as is known, and data placed there
reads back fine at run time.

**FIRM header** (0x30 bytes):

| Offset | Value | Meaning |
|---|---|---|
| +0x00 | 0x30 | header size |
| +0x04 | | build timestamp |
| +0x10 | 0x804C00 | unclear; disassembly shows the block's vectors at 0x804800 |
| +0x14 | 0x804C01 | entry |
| +0x18 | 0x1000 | size of the RAM block (starts at FIRM+0x30) |
| +0x1C | | CRC16 of the RAM block |

The rest of FIRM runs in place from flash. There is a 130 KB zero-filled run at flash
`0x18A8E`-`0x39470`, right before the `.data` load image; its purpose is unknown, so the tools
leave it alone.

**Checksums**: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over the bootloader, FIRM, TONE and
the FIRM RAM block. There are no signatures. `slfw.py fixcrc` reproduces the original dump byte
for byte.

### Software stack

From strings and decompilation:

- FreeRTOS with an OAL layer and Linux-style device nodes: `/dev/lcd`, `/dev/audio0` (play),
  `/dev/audio1` (record), `/dev/fm`, `/dev/tp`, `/dev/usbd`, `/dev/uart0..2`, `/dev/key_io`,
  `/dev/key_onoff`, `/dev/kadc_ch1`, `/dev/timer`.
- LVGL v8.3 (source path `F:\pen133_yp3_mp\spark2\src\gui8\lvgl\...`), configured with
  `LV_COLOR_16_SWAP=1`, `LV_USE_USER_DATA=0`, 16-bit `lv_coord_t`, `LV_FONT_FMT_TXT_LARGE`, and
  the image cache disabled. Asserts include function names, which helps naming functions.
- FatFs (R0.14 style) with exFAT enabled:
  - `check_fs` 0xD5609C recognizes the exFAT boot sector. The volume mount 0xD573E8 has the full
    exFAT branch: version 1.00, 512-byte sectors, one FAT, allocation bitmap. `f_mkfs` 0xD58F38
    can create exFAT.
  - Partitions: an unpartitioned card or the 4 MBR primary partitions; no GPT handling was found.
  - The SD driver's ioctl 3 (0xD46EB4) reads the boot sector and reports the volume's "dirty"
    flag (FAT32 byte 0x41, exFAT VolumeFlags byte 0x6A). For a partitioned exFAT card it returns
    1 when the flag is set. Who uses that result is not known yet.
  - The bootloader has its own FatFs copy with the same exFAT signature check.
- libjpeg, codecs: MP3, FLAC, APE, WMA, AAC, OGG/Vorbis, SBC/mSBC.
- Bluetooth classic: A2DP source and sink, AVRCP, HFP.
- Apps: music (database with artist/album/favorites, `.lrc` lyrics), video (MJPEG AVI), photo
  viewer, ebook, English dictionary, voice recorder (WAV/ADPCM to `0:\record`), FM radio, alarm,
  EQ.
- UI pages are created by `view_page_create` (0xD04A1C), a switch on page id; page handlers are
  `pageN_*_process` functions.

The app also calls into the boot ROM (e.g. `bl 0x2b0`, `b.w 0x2564`). A ROM dump would help
deeper work: `smtlink_dump init read_mem 0 0x10000 rom.bin` (size is a guess). Until then the
Ghidra setup fills the ROM with `bx lr` stubs.

### Assets inside FIRM

**Images**: 15 LVGL `lv_img_dsc_t` (`{u32 header (cf:5, w:11, h:11), u32 data_size, u8 *data}`),
each followed by its pixels, around flash `0x9D1E8`-`0xA6D24`: 9 menu icons 29x29, 4 app icons
50x50 (3 of them 8-bit indexed), one 16x16, and the 84x20 "welcome" banner. True color is RGB565
with bytes swapped. Chroma-keyed images use pure green `0x07E0` as transparent. Most other UI
graphics are glyphs from icon fonts.

**UI strings**: `const char *` table at flash `0x53154`: 10 languages x 219 UTF-8 strings (index 0
is the language's own name). Order: de, en, fr, it, ja, nl, pl, pt, ro, es. A 10-entry array of
per-language table pointers is at `0x48770` (copies at `0x48798` and `0x101FE8`).
`get_lang_str(id)` (0xD01F8C) returns a string in the current language.

**Fonts**: 16-byte glyph descriptors
(`u32 bitmap_index, u32 adv_w, u16 box_w, u16 box_h, i16 ofs_x, i16 ofs_y`), glyph bitmaps packed
continuously, MSB first. Two kinds:

- "sl" fonts (9, all in `.data`): custom per-font `get_glyph_dsc`/`get_glyph_bitmap` functions
  (e.g. `0xD055BB`/`0xD05579`). A single cmap: `range_start` = first code, `range_length` = last
  code, `unicode_list` = sorted absolute u16 codes, binary searched; glyph id = index in that list.
  `adv_w` is in whole pixels. Layout in `.data`: dsc (0x18) + glyph cache (8) + `lv_font_t` (0x18).
  - `font_464b0`: the UI text font (`lv_font_t` at RAM `0x819C98`), 1 bpp, line height 15, 3237
    glyphs (Latin, Latin-1, some Latin Extended, Hebrew, kana, ~2400 CJK). Missing glyphs used by
    the strings: Polish/Romanian letters (ă ł ț ș ć ś ń ż ź Ł Ś Ș ţ), ¡ ¿ °, and 10 CJK
    characters. These render as gaps unless added with a font rebuild.
  - `font_46440`, `font_46470`: clock digits; the others are FontAwesome-style icon fonts.
- Standard `lv_font_fmt_txt` fonts in flash (`get_glyph_dsc` = `0xD374ED`): small icon fonts in
  the Private Use Area (U+E6xx, iconfont.cn) and a few digit/ASCII fonts, e.g. `0xC610F8`
  (23 px clock digits, only `/0123456789:CELMOW`). The asset tools don't handle these yet.

**Other data**: Unicode/GBK conversion table around `0xB8000`-`0xC4000` (FatFs code page); the
`TONE` partition holds the alarm MP3.

### SDK internals

**Calling the firmware.** `fw.h` declares each function as a macro that casts its address (with the
Thumb bit) to a function pointer, so calls compile to `ldr` + `blx` with no linking against the
firmware. Code is linked at `0xDD0000` (flash `0x1D0000`, up to `0x1FC000`, 176 KB) and runs in
place.

**APP menu hook** (`sdk/src/menu.c`).
- The home grid's "APP" item opens page 0x53, `page_home_menu_create` (0xD0FC64): a list with
  Alarm and Pictures (string ids 7 and 4).
- Its rows are made by `fw_list_item_create` (0xD0267C), each with a FontAwesome symbol label
  (font `0xC810C4`) and a text label. The page state is at `*(0x81CE5C) + 0x1DC`.
- `sdk/patches.json` redirects the `bl page_home_menu_create` at `0xD04C04` in `view_page_create`
  to `hook_home_menu_create`. The hook calls the original, then appends one row per
  `FW_MENU_ITEM`. Its second entry installs the Center long-press hook (see
  [Buttons and key events](#buttons-and-key-events)). The build keeps every patch target when it
  discards unused code.
- The linker script collects the `FW_MENU_ITEM` entries between `__fw_menu_start` and
  `__fw_menu_end`.
- Each row's key handler opens the app on ENTER and passes other keys to
  `page_home_menu_event_cb` (0xD0FB2C), so navigation and Back keep working.
- Page 0x51 (`page_expand_home_create`, a 2x3 icon grid) exists in the code but is not reachable
  on this model.

**Overlays** (`sdk/src/overlay.c`).
- `fw_overlay_open` creates a full-screen object on `lv_layer_top()`, adds it to the view's key
  group (`*(*(0x81CE5C) + 0x30)`), focuses it and freezes the group, so the app gets all key
  events.
- `fw_overlay_close` deletes it and gives focus back to the menu row.
- `on_delete` runs from `LV_EVENT_DELETE`, also if the firmware tears down the UI by itself, so
  it is the one place to stop timers and free memory.

**LVGL heap.** `lv_mem_init` (0xD39014) creates a 40 KB TLSF pool for the whole UI. The image cache
is disabled (`_lv_img_cache_open` decodes on every draw), so an `lv_img_dsc_t` in RAM can be
changed in place and redrawn with `lv_obj_invalidate`. Mp3tris draws its board as a 70x140
`LV_IMG_CF_INDEXED_4BIT` image (16-color palette of `b, g, r, a` entries, then 4-bit pixels with
the first pixel in the high nibble; alpha must be 0xFF).

**Useful addresses** (all in `fw.h`): `lv_mem_alloc` 0xD3909C, `lv_mem_free` 0xD39030,
`lv_obj_create` 0xD2A250, `lv_obj_del` 0xD2E3B4, `lv_obj_invalidate` 0xD2B770, `lv_label_create`
0xD3F1FC, `lv_img_create` 0xD3E764, `lv_img_set_src` 0xD3E77C, `lv_timer_create` 0xD39F34,
`lv_timer_del` 0xD39FA4, `lv_tick_get` 0xD37980, `lv_obj_add_event_cb` 0xD280C8, and the style
setters from 0xD2E074.

### Pages and navigation

- **View side:** `view_page_create` (0xD04A1C) switches on the view page id (list of `page_*_create`
  functions in the Ghidra CSV). View event callbacks report key actions to the logic side with
  `FUN_00D04430(page, action, index, ...)`.
- **Logic side:** the "presenter" `pageN_*` functions decide what happens. They navigate with
  `FUN_00CEEAFC(current, action, index, target_page)` and go back with `FUN_00CEEFE0`.
- **Numbering:** presenter numbers equal view page ids for the low pages (e.g. `page20_*` is
  video play, 0x14). For the highest pages they are one higher: `page81_*` is the password menu
  (0x50), `page82_*` view 0x51, and `page84_*` the APP list (0x53). Where the offset starts
  hasn't been pinned down.
- **Home grid** (page 1, `FUN_00CE1E9C`): Music → 0x02, Video → 0x13, Ebook → 0x0C,
  Recorder → 0x18, FM → 0x1A, APP → 0x53, Folder, Bluetooth → 0x23, Settings → 0x28. Index 10,
  Pictures → 0x15, is handled but has no icon; Pictures moved into APP.
- **APP list** (0x53): Alarm → 0x1E, Pictures → 0x15.
- **Settings** (0x28): Language 0x29, Date/time 0x2A, Speaker 0x4C, Brightness 0x2D, Screen off
  0x2E, Auto power-off 0x30, Sleep timer 0x4D, Device info 0x33, Storage 0x4E, then the actions
  Restore factory settings, Update Playlist, and Format SD card.

### Hidden features

These pages are complete in the firmware, but no menu navigates to them on this model:

- **Expand home** (0x51): a 2x3 icon grid with 4 icons: Pictures, Alarm, FM, and "Aa" with a
  magnifier (dictionary). Its parts don't agree with each other:
  - the label list is Ebook, Pictures, FM, Recorder, Settings;
  - its presenter (`page82_img_process`, 0xCED5AC) sends the slots to Video, Pictures, Ebook,
    Recorder, FM, Settings and Music.
  It looks like an abandoned layout from another product variant.
- **Dictionary** (0x37 `page_dict_input` keyboard entry, 0x38 `page_dict_play` result with a
  pronunciation "Play" button):
  - Data comes from `0:\dict` ("No Dictionary Data Detected" otherwise).
  - Lookup `FUN_00CE0C20` binary-searches an index of 48-byte records: a 32-byte word, then
    offset/length into a text file and into an audio file (up to 80 KB per word). The first 76
    records are cached in RAM.
  - The data file names have not been found yet.
  - The navigation code has a case for entering 0x37 (`pstr_enter_page56`), but nothing calls
    it with 0x37. The word display page also special-cases a few words ("eat", "keep", "know",
    "spend", "dead", ...).
- **Power-on password** (0x50 `page_pass_menu`: "Power-on password" on/off and "Change
  password"; 0x4F `page_pass_input`): stored in NV settings (`pstr_pass_nv_save`). The menu itself
  is not linked from Settings.
- **Key light** (0x4B `page_set_keylight`, string "Key light"): probably for models with
  backlit keys.

Showing any of these needs a logic-side change: an APP row's ENTER handler (view side) can't
navigate on its own. The APP presenter (`page84_*`, 0xCEDA8C) ignores indexes above 1.

### Video player

The home screen's "Video" item plays only one specific format, so most video files are ignored
or rejected. The rules below come from the decompiled scanner (0xD53AD8) and parser
(0xCF2DBC, 0xCF3500), and `tools/avicheck.py` applies the same checks to a file.

**Finding files.**
- The media database scan (`0:\DBROOT\db_video.db`) lists files by extension only, and the only
  video extension is `.avi` (case-insensitive).
- It searches the whole card up to 9 folders deep and lists at most 5000 files.
- It skips `0:\record`, `0:\DBROOT`, `0:\dict` and `System Volume Information`.

**Playing them.** A listed file plays only if:
- It is a RIFF AVI with `hdrl`/`avih` first, followed directly by the stream lists. The video
  stream is `vids` with codec `MJPG` (each frame a baseline JPEG, decoded with libjpeg).
- The video is **exactly 128x160** (the screen in portrait); any other size fails with "width and
  height failed".
- Any audio track is **stereo** and is played as 16-bit PCM.
- `movi` starts in the first 30 KB, and every chunk in it is a video or audio chunk of at most
  30 KB, ending at `idx1`.
- The frame rate comes from `avih`.

**The player mirrors the picture horizontally.** A file that looks right on a PC shows up
left-right flipped on the device; this was seen on the player itself, and the cause in the
firmware hasn't been found. To compensate, mirror the video during conversion with `hflip`
(the commands below include it). Remove `hflip` if your player shows the video the right way
round without it.

ffmpeg's AVI output meets these rules. To convert a video for portrait viewing, letterboxed:

```sh
ffmpeg -i in.mp4 -vf "scale=128:160:force_original_aspect_ratio=decrease,pad=128:160:(ow-iw)/2:(oh-ih)/2,hflip,fps=20" \
  -c:v mjpeg -q:v 5 -pix_fmt yuvj420p -c:a pcm_s16le -ar 44100 -ac 2 out.avi
```

To watch with the player turned sideways, scale to 160x128, rotate, then mirror:
`-vf "scale=160:128:force_original_aspect_ratio=decrease,pad=160:128:(ow-iw)/2:(oh-ih)/2,transpose=1,hflip,fps=20"`
(use `transpose=2` to rotate the other way). `hflip` must come after `transpose`, because the
device mirrors the picture as it appears on the screen.

Not yet confirmed on the device: what frame rate the decoder keeps up with, and when the
database is rescanned after files are copied to the card.

### Audio

**Device layer.** Drivers are nodes in a list at 0x81D40C. The calls are `fw_dev_open(name,
flags, params)` (0xD44604), `fw_dev_close(&node)` (0xD44674) and `fw_dev_write(node, buf, count,
0)` (0xD446BE).
- Opening a device that is already open returns the same node without reopening it. Closing that
  node would close the other user's stream, so `sdk/src/sound.c` checks the node's "open" flag
  first and stays silent if the device is busy.

**`/dev/audio0`** (driver functions: open 0xD46434, write 0xD4628C, close 0xD462BC) takes 10
words of open parameters:
- channels, bytes per sample, sample rate, period (frames), periods in the ring buffer,
  start threshold (frames), 0, 0, event callback, callback argument;
- rates of 16 kHz or less fail, and mono is converted to stereo;
- every write must be exactly one period;
- a start threshold of 0 means the whole ring must fill before output starts.

The ring is written by `FUN_00813982`, which waits (for up to 3 s) only when the ring is full.
The video player (`FUN_00CF3500`) and the music player's sink (`FUN_00D4968C`) both use this
device.

The event callback gets (arg, 1) after open and (arg, 2) on close. The video player's callback
(0xCF34C5) switches the speaker amplifier ("PA") on through a boot-ROM routine when the Speaker
setting is on, and off again on close.

**How the firmware's players use it.** Both feed the device from a dedicated task, keep the ring
full with blocking writes, and close only after stopping that task. The video player
(`FUN_00CF3500`, close `FUN_00CF3744`) opens a stream of 256-frame periods × 16 with its PA
callback and runs `FUN_00CF3324` in a task. The music player's sink is `FUN_00D4968C`.

**Closing safely.** The playback DMA interrupt (0x8167E8) keeps a state byte at +0x21 of the
hardware object that `*(void **)0x81E320` points to: 0 idle, 1 priming (it loops 16 bytes of
silence), 2 playing, 3 starved. While playing, each interrupt consumes the finished period and
starts the next one from the ring, and switches to "starved" when less than a period is left.
Closing the device stops the stream (which starts one more period from the ring), releases the
DMA channel and frees the ring, but does not stop the interrupt. If the ring still holds data,
the next interrupt starts a transfer from the freed ring with the cleared channel handle. When the
SDK closed with a full ring, opening the audio device after the resulting reboot rebooted the
player again until a power cycle; waiting for the ring to run dry first stopped that. So close
only once the ring has run dry (state 3). The video player gets this for free, because
it frees its buffers and closes the file between stopping its task and closing the device.

**Don't touch `/dev/audio0` from the UI task.** The UI runs in `watch_view_task` (0xCD8FE0),
which calls `lv_timer_handler` (0xD39FC8) in a loop; app timers and key events run inside it.
Measured with an earlier version of the Sound test app, which ran each device call from a UI
timer and logged it on screen first:
- a write from the UI task that has to wait for ring space hangs for about 10 s and then reboots,
  every time;
- opening or closing from the UI task sometimes crashes, even with nothing played;
- joining the sound task, or sleeping while the ring drains, from the UI task hung on exit.

The same writes from a separate task always worked. The cause isn't known. The music player
opens, writes and closes from its own task. The video player opens and closes from presenter
code, outside LVGL timers and events. Do all device calls from your own task, and finish from a
UI timer once that task is done. The Sound test app (`sdk/apps/soundtest`) now only checks the SDK
sound module: it opens it, beeps and closes it three times, logging each step.

**Volume.** `fw_audio_volume()` (0xCDF884) returns the user's volume in the low byte. The video
player scales samples with `fw_gain_apply(gain, volume, buf, frames)` (0xD47C10) on a gain object
from `fw_gain_create(0, rate, 2, 2)`.

**OS layer.** The firmware wraps FreeRTOS (all in `fw.h`):
- tasks: `fw_task_create(entry, arg, name, priority, stack)` (0xCD88A4) and `fw_task_join`
  (0xCD8960), which waits for `entry` to return;
- binary semaphores: create, take (-1 on timeout), give, delete (0xCD86DE, 0xCD874C, 0xCD876E,
  0xCD86E8);
- firmware heap: `fw_os_calloc` and `fw_os_free` (0xCD8610, 0xCD8654);
- `fw_os_sleep(ms)` (0xCD86B4).

On failure, the allocation and task functions return `printf`'s return value instead of NULL.
`fw_is_ram()` checks results.

**SDK sound module** (`sdk/src/sound.c`) follows the video player:
- its state is on the firmware heap;
- a task at priority 6 owns the device. It opens it at 44.1 kHz stereo, 8 periods of 256 frames,
  with the video player's PA callback, then renders one period at a time with blocking writes;
- `fw_sound_play` hands requests to the task through a small lock-free queue;
- `fw_sound_close` only signals a semaphore and returns. The task fills the ring with silence,
  waits for it to run dry (`FW_AUDIO_HW` state, polled with `fw_os_sleep`), closes the device,
  sets a flag and returns. A 20 ms LVGL timer then joins the task, which no longer blocks, and
  frees the state, about 0.1 s after the game has closed.

It mixes up to 4 synthesized voices: square, triangle or noise, with a linear frequency slide and
a decaying envelope. The games have no sound while the music player has the audio device open.
Whether the music player keeps the device open while paused or stopped hasn't been tested yet.

### Buttons and key events

The keypad driver's read callback (`FUN_00D02018`) turns the key manager's (key id, event)
pairs into the codes below. Short presses and the Left, Right and Up long presses were measured
with the Key test app; the rest come from the driver code.
- **Holding a key** sends the long-start code once, then the repeat code a few times per second,
  then the long-release code when the key is let go.
- **`fw.h` names:** `FW_KEY_<KEY>` for the short press, plus `_LONG`, `_REPEAT` and `_LONG_UP`.

| Button | Key id | Short | Long start | Repeat | Long release | `fw.h` name |
|---|---|---|---|---|---|---|
| Left | 0x42 | 0x14 (`LV_KEY_LEFT`) | 0x85 | 0x87 | 0x86 | `FW_KEY_LEFT` |
| Right | 0x43 | 0x13 (`LV_KEY_RIGHT`) | 0x88 | 0x8A | 0x89 | `FW_KEY_RIGHT` |
| Top | 0x25 | 0xA0 | 0xA1 | 0xA3 | 0xA2 | `FW_KEY_UP` |
| Center | 0x21 | 0x0A (`LV_KEY_ENTER`) | 0x8B | (0x8D) | (0x8C) | `FW_KEY_CENTER` |
| Bottom | 0x45 | 0x81 (the firmware's "leave page") | 0x82 | 0x84 | 0x83 | `FW_KEY_BACK` |

The driver also maps keys this player doesn't have:
- 0x20: `LV_KEY_DOWN`, long 0x99/0x9B/0x9A;
- 0x22: `LV_KEY_UP`, long 0x96/0x98/0x97;
- 0x34 "record": 0x92, long 0x93/0x95/0x94.

The key manager (`FUN_00CDD5C0`, a state machine logging "key status is work" and so on) names
some other ids: 0x23/0x24 volume down/up, 0x37 "onoff" and 0x47 "power". Its state byte is at
0x81BC50 (1 = screen on), and it hands keys to the keypad driver with `FUN_00D0227C(id, event)`.
The driver holds a single pending pair (at 0x823DF4).

**Back long press.** With the screen on, the key manager opens the volume control on a Back long
start (`FUN_00CDD5C0`, branch at 0xCDDD08) instead of passing it on. While the volume control is
open, Left/Right adjust the volume. Back's repeat and long release reach the keypad driver
normally.

**Center is different.** Its button goes through the power management chip, which reports only
three raw events to the key manager: press (1), short release (2), and long hold (4). The key
manager turns 4 into a press of the "power" key (0x47), which powers the player off when the
status byte read by `FUN_00D27A5C` is 0 or 1, and turns the screen off for 2 to 5. That byte is
firmware state (at 0x81D14C) with its own dispatcher (`FUN_00D27158`), probably Bluetooth, so the
SDK leaves it alone. So Center never produces repeat or long-release codes, and holding it powers
the player off.

The SDK patches the key manager's "keys ready" check (`bl 0xCF09D4` at 0xCDD5E6, entry in
`sdk/patches.json`) to `hook_key_event` (`sdk/src/keys.c`):
- If the focused object has `FW_OBJ_FLAG_CENTER_LONG` (`LV_OBJ_FLAG_USER_1`), the hook rewrites
  raw event 4 as a Center long start (0x21, 0x40), which arrives as `FW_KEY_CENTER_LONG` (0x8B).
  Confirmed on the device; no repeat or release code follows.
- If the focused object has `FW_OBJ_FLAG_BACK_LONG` (`LV_OBJ_FLAG_USER_2`) and the screen is on,
  the hook passes a Back long start straight to the keypad driver, so the app gets
  `FW_KEY_BACK_LONG` (0x82). It then replaces the event with raw 0, an unknown key that the key
  manager ignores, so the volume control doesn't open.
- SDK overlays set both flags on their root. Clear a flag on `ov->root` to restore the firmware's
  action.
- Not yet confirmed: whether the power chip forces the player off after an even longer Center
  hold.

The games exit on `FW_KEY_BACK_LONG` and ignore a short Back, so a stray press doesn't end a
game. In Mp3tris, every Center code except a release drops the piece one row: the press, and
`FW_KEY_CENTER_LONG` or `_REPEAT`. A press on a landed piece locks it.

Center (ENTER) works differently from the others:
- `LV_EVENT_PRESSED` arrives when it goes down.
- `LV_EVENT_RELEASED`, and then its `LV_EVENT_KEY`, arrive when it is let go.

For an immediate reaction, act on `PRESSED`. Use the `KEY` event only as a fallback when no
`PRESSED` came first; Flutter and Mp3tris both do this. Event codes in this build: PRESSED 1,
RELEASED 8, KEY 13, DELETE 33.

Page handlers also check 9/11 (NEXT/PREV), 27 (ESC), and the repeat codes 0x87, 0x8A, 0x98, 0x9B
and 0xA3 and record code 0x92 from the table above.

### Ghidra

- `ghidra/import.sh [firmware.bin]` builds `ghidra/project/sl6801.gpr` with two programs:
  - `sl6801_app`: the main firmware, 4811 functions, 354 named.
  - `sl6801_boot`: the bootloader. Only its block at 0x81FBC0+ is the bootloader proper.
- The scripts in `ghidra/scripts/`:
  - `SL6801Setup.java` sets up the memory map (flash, RAM copies, a stub ROM), vectors, and
    function seeds from code pointers.
  - `SL6801Names.java` names functions after their `__func__` strings and exports
    `ghidra/out/<program>_functions.csv`.
  - `SL6801Fixups.java` clears the "no return" flag on ROM stubs such as `rom_printf` (0x2B0).
- `ghidra/decompile.sh <app|boot> <out.c> <name|0xaddr>...` decompiles from the saved project
  without re-analysis.

### Windows flashing setup

- **Driver.** In bootloader mode the device (`301A:2800`) binds to Windows' USB mass-storage
  driver. In Zadig, use Options > List All Devices and replace that driver with WinUSB for this
  VID/PID only. Card reader mode (`301A:2801`) is unaffected.
- **Flasher.** `smartlink_flash/` is a clone of github.com/ilyakurdyukov/smartlink_flash. Use a
  version that includes the `write_flash` file offset fix; older builds ignore `<file_offset>`,
  which only matters when writing selected regions of an image.
- **Building it.** In MSYS2 UCRT64, run `pacman -S mingw-w64-ucrt-x86_64-libusb make`, then
  `make CC=gcc` from a UCRT64 shell. Copy `libusb-1.0.dll` next to `smtlink_dump.exe` so it runs
  from any shell.
- **Using it.** Always start a command with `init`, e.g.
  `smartlink_flash/smtlink_dump.exe init flash_id read_flash 0 2M backup.bin`.

## Gotchas

- **No writable globals in apps** (see [Writing a new app](#writing-a-new-app)).
- **Center's KEY event comes on release**; use `LV_EVENT_PRESSED` for responsiveness.
- **`LV_EVENT_DELETE` is 33** in this build (not 34).
- **Images resized by `pack`** may be cropped or misplaced if the UI code hardcodes the original size.
- **`slfw.Image` copies its input buffer**; code that edits an image must write to `img.data`.

## Open questions

- What the 130 KB zero run inside FIRM is for, and FIRM header field +0x10.
- Whether the vendor firmware ever writes to the free flash area (no problems seen so far).
- A boot ROM dump, to name the ROM functions the app calls.
- Somewhere persistent to store app data (e.g. high scores) without globals: a free field in the
  `PSMP` settings or a spare pointer in the firmware's RAM.
