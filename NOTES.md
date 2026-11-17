# MP3 player firmware notes

## Hardware (from board photos)

| Part | Identification |
|---|---|
| SoC | "Jointbees MP3 R14181B6A0", QFN-48, really a Smartlink **SL6801** (string `SL6801` in bootloader). 2 MB flash in package, no external flash chip. |
| CPU | ARM Cortex-M with FPU (VFP instructions present). Log strings mention CPU0 and CPU1 (dual core). |
| Display | ST7789 TFT on a 20-pin FPC (`st7789s_lcd_init`), RGB565 |
| FM radio | RDA5807M (marked `5807M`) |
| Speaker amp | 8002D (SOP-8, class AB) |
| Other | microSD slot (SDIO), mic (MIC1), PCB trace antenna (Bluetooth), 5 buttons, slide switch, USB-C, 3.5 mm jack, 150 mAh LiPo |
| Test pads | `5V DM DP G ADC` (USB + ADC) next to R27-R30 |

## Recovery path

The boot ROM has a USB mass-storage-like bootloader (VID:PID `301a:2800`), entered by holding a
key while plugging USB. It can read/erase/write flash and run code in RAM (see
`smtlink_dump` in github.com/ilyakurdyukov/smartlink_flash). Since it lives in ROM, a bad flash
should always be recoverable. **Confirm this works on this unit before writing anything.**

## Memory map

| Address | What |
|---|---|
| `0x00000000` | Boot ROM (ROM memcpy at `0xb2a`) |
| `0x00800000`-`~0x0086C000` | RAM. App initial SP `0x0086B710`, bootloader SP `0x0083BE90` |
| `0x0081FBC0` | Bootloader vector table (loaded from flash `0x60`), reset at `0x00820000` |
| `0x00804800` | App vector table (FIRM RAM block), reset at `0x00804C40` |
| `0x0080CC40`-`0x0081AAEC` | App `.data`, copied from flash `0x39470`-`0x4731C` at startup (RAM = flash offset + `0x7D37D0`) |
| `0x0081AAEC`-`0x0086B710` | App `.bss` (zeroed by Reset_Handler) |
| `0x40000000`-`0x400FFFFF` | Peripheral registers (most register constants fall here) |
| `0x00C00000` | SPI flash, memory mapped (XIP). Flash offset `X` is at `0xC00000 + X` |

## Flash layout (2 MB)

| Flash offset | Size | Content |
|---|---|---|
| `0x000000` | 0x60 | `HLKJ` header: +4 load addr, +8 entry, +0xC code offset, +0x10 code size, +0x14 CRC16, +0x20 partition table offset |
| `0x000060` | 0xC8EC | Bootloader (FreeRTOS-based, LCD/SD init, firmware update from PC/SD) |
| `0x00D000` | 0x40 | Partition table: u32 count(3), ..., 16-byte entries from +0x10: name, offset, size, CRC16 |
| `0x00E000` | 0x174270 | `FIRM` main application |
| `0x183000` | 0x2038 | `TONE`: u32 size + name (`alarm1.mp3`), data at +0x400 (MP3 with ID3 tag) |
| `0x185038` | ~475 KB | **Free (0xFF)** |
| `0x1FC000` | 0x4000 | `PSMP` settings (NV storage, not checksummed) |

### FIRM header (0x30 bytes)

| Offset | Value | Meaning |
|---|---|---|
| +0x00 | 0x30 | header size |
| +0x04 | | build timestamp |
| +0x10 | 0x804C00 | unclear; disassembly shows the block's vectors at 0x804800 |
| +0x14 | 0x804C01 | entry |
| +0x18 | 0x1000 | size of RAM block (starts at FIRM+0x30) |
| +0x1C | | CRC16 of RAM block |

There is a 130 KB zero-filled run at flash `0x18A8E`-`0x39470` inside FIRM, right before the
`.data` load image. Its purpose is unknown, so the tools leave it alone.

The app calls into the boot ROM (e.g. `bl 0x2b0`, `b.w 0x2564`), so a ROM dump will be needed for
deeper reverse engineering: `smtlink_dump init read_mem 0 0x10000 rom.bin` (size is a guess).

### Checksums

CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Four checksums: bootloader, FIRM, TONE, and the
FIRM RAM block. No signatures. `tools/slfw.py fixcrc` recomputes all of them and reproduces the
original dump byte for byte.

## Software stack (from strings)

- FreeRTOS with an OAL layer and Linux-style device nodes: `/dev/lcd`, `/dev/audio0` (play),
  `/dev/audio1` (record), `/dev/fm`, `/dev/tp`, `/dev/usbd`, `/dev/uart0..2`, `/dev/key_io`,
  `/dev/key_onoff`, `/dev/kadc_ch1`, `/dev/timer`
- LVGL v8 (source path `F:\pen133_yp3_mp\spark2\src\gui8\lvgl\...`). The asserts include function
  names, which helps naming functions.
- FatFs (FAT32/exFAT), libjpeg, codecs: MP3, FLAC, APE, WMA, AAC, OGG/Vorbis, SBC/mSBC
- Bluetooth classic: A2DP source+sink, AVRCP, HFP
- Apps: music (DB with artist/album/favorites, lyrics `.lrc`), video (MJPEG AVI), photo viewer,
  ebook, English dictionary, voice recorder (WAV/ADPCM to `0:\record`), FM radio, alarm, EQ
- UI pages are `pageN_*_process` functions (N = 1..35+), with multilingual string tables
- Version string `yp3_2.0.43`

## Assets inside FIRM

### Images
15 LVGL v8 `lv_img_dsc_t` (`{u32 header (cf:5, w:11, h:11), u32 data_size, u8 *data}`), each followed
directly by its pixel data around flash `0x9D1E8`-`0xA6D24`: 9 menu icons 29x29, 4 app icons 50x50
(3 of them I8 indexed), one 16x16 and the 84x20 "welcome" banner. True color is RGB565 with bytes
swapped (LV_COLOR_16_SWAP=1). Chroma-keyed images use pure green `0x07E0` as transparent. Most other
UI graphics are glyphs from icon fonts (below), not bitmaps.

### UI strings
`const char *` table at flash `0x53154`: 10 languages x 219 strings (index 0 is the language's own
name). Language order: de, en, fr, it, ja, nl, pl, pt, ro, es. A 10-entry array of per-language
table pointers is at `0x48770` (copies at `0x48798` and `0x101FE8`). Strings are UTF-8.

### Fonts
LVGL v8 built with `LV_FONT_FMT_TXT_LARGE` (16-byte glyph descriptors:
`u32 bitmap_index, u32 adv_w, u16 box_w, u16 box_h, i16 ofs_x, i16 ofs_y`). Glyph bitmaps are
packed continuously, MSB first. Two kinds:

- "sl" fonts (9, all in `.data`): custom per-font `get_glyph_dsc`/`get_glyph_bitmap` functions
  (e.g. `0xD055BB`/`0xD05579`). Single cmap: `range_start` = first code, `range_length` = last code,
  `unicode_list` = sorted absolute u16 codes, binary searched; glyph id = index in that list.
  `adv_w` is in whole pixels. Layout in `.data`: dsc (0x18) + glyph cache (8) + `lv_font_t` (0x18).
  - `font_464b0`: the UI text font, 1 bpp, line height 15, 3237 glyphs (Latin, Latin-1, some
    Latin Extended, Hebrew, kana, ~2400 CJK). Missing: Polish/Romanian letters (ă ł ț ș ć ś ń ż ź Ł Ś Ș ţ),
    ¡ ¿ ° and 10 CJK characters used by the strings, so those render as gaps.
  - `font_46440`, `font_46470`: clock digits; the rest are FontAwesome-style icon fonts (U+F0xx).
- standard `lv_font_fmt_txt` fonts in flash (`get_glyph_dsc` = `0xD374ED`): small icon fonts in the
  Private Use Area (U+E6xx, iconfont.cn) and a few digit/ASCII fonts. Not handled by the tools yet.

### Other data
- Unicode/GBK conversion table around `0xB8000`-`0xC4000` (FatFs code page)
- `TONE` partition: alarm sound MP3

## Tools

All Python tools run with the project venv: `./.venv/Scripts/python -I tools/<tool>.py ...`

- `assets.py unpack firmware_2M.bin work`: export images (PNG), `strings.json`, fonts (PNG sheet +
  JSON) and the tone into `work/`.
- `assets.py pack firmware_2M.bin work out.bin`: apply edits from `work/` and fix all checksums.
  Prints the changed 4 KiB sectors and an `smtlink_dump` command that writes only those.
  - strings: edit `work/strings.json`; new text goes to the free flash area and the table is repointed.
  - images: edit the PNGs. Same size is replaced in place; a new size is stored in the free area
    and the descriptor updated (the UI layout code may still assume the old size).
  - fonts: set `"rebuild"` in `work/fonts/<font>.json`, e.g.
    `{"ttf": "C:/Windows/Fonts/arial.ttf", "size": 12, "mode": "add_missing", "add_chars": "ŹŻ"}`.
    `add_missing` renders only codepoints the font lacks (by default including every character used
    in `strings.json` for the UI font); `replace` re-renders every glyph the TTF has. Optional keys:
    `y_shift`, `line_height`, `base_line`, `include_strings`.
  - tone: replace `work/tone/alarm1.mp3`.
- `assets.py preview <image.bin> font_464b0 out.png "text" ...`: render text with a firmware font,
  using LVGL's glyph placement, to check edits before flashing.
- `slfw.py info | extract <dir> | fixcrc <out>`: parse layout, verify/fix checksums, extract partitions
- `find_lv_images.py`, `find_lv_fonts.py`, `render.py`, `findbase.py`: analysis helpers

Edited data lives in the free area after `TONE` (flash `0x186000`+, up to `0x1FC000`). Nothing in
the vendor firmware is known to use that area, but this has not been confirmed on the device yet.

## Ghidra

`ghidra/import.sh` builds `ghidra/project/sl6801.gpr` with `sl6801_app` and `sl6801_boot`, using
`ghidra/scripts/SL6801Setup.java` (memory map, vectors, function seeds from code pointers) and
`ghidra/scripts/SL6801Names.java` (names functions after their `__func__` strings, exports
`ghidra/out/<program>_functions.csv`).

First run: the app has 4811 functions, 354 named from `__func__` strings (LVGL core, vendor pages,
key manager, etc.). The boot program also disassembles parts of the app in the flash block; only
its `boot` block (0x81FBC0+) is the bootloader proper.

## Code injection (SDK)

- `sdk/include/fw.h`: firmware API. LVGL v8.3 functions by address (called through constant
  function pointers), style setters, group/event helpers, the UI font, and vendor hooks
  (view context at `*(0x81CE5C)`: key group at +0x30, APP page state at +0x1D4).
- `sdk/app.ld`: code at flash `0x1D0000` (runs at `0xDD0000`), up to `0x1FC000`. `.data`/`.bss`
  must be empty; keep state in `lv_mem_alloc` memory.
- `sdk/build.py [--zig] [-o OUT] <app dir>...`: links one or more apps into a single image, e.g.
  `sdk/build.py sdk/apps/flutter sdk/apps/mp3tris`. Compiles with arm-none-eabi-gcc (xPack 15.2.1
  in `C:/Tools/arm-gnu-toolchain`, override with `ARM_GCC_BIN`) or with `zig cc` (ziglang pip
  package). Thumb-2, Cortex-M4, soft-float ABI. Each app is compiled separately (its
  `assets/X.png` become headers `X.h` with `img_X`, visible to that app only). Writes
  `sdk/build/app.bin`, `app.json`, and with gcc also `app.map` and `app.lst` (disassembly with
  source). `sdk/patches.json` (plus an app's optional `patches.json`) lists BL call sites to
  redirect to app symbols, with the expected original target as a safety check.
- `tools/assets.py pack firmware_2M.bin work out.bin --app sdk/build/app.json`: writes the code
  and patches the BLs. Asset data then only uses `0x186000` up to the app.
- `sdk/include/sdk.h` + `sdk/src/`: shared code linked into every image. `menu.c` is the APP menu
  hook; an app adds a row with `FW_MENU_ITEM(symbol, text, open)` (collected by the linker
  script into `__fw_menu_start`..`__fw_menu_end`, in the order the apps were given). `overlay.c`
  gives a full-screen overlay that owns key input and calls `on_delete` when torn down. Also small
  formatting helpers, `fw_label`, `fw_rect`, and memset/memcpy/memmove/strlen.
- Apps: `sdk/apps/hello` (key test), `sdk/apps/flutter` (game: Center flaps, Back exits;
  `art.py` draws the sprites into `assets/`), `sdk/apps/mp3tris` (falling blocks: Left/Right
  move, Up rotates, holding Center falls fast, Back exits).
- LVGL timers: `lv_timer_create` 0xD39F34, `lv_timer_del` 0xD39FA4, `lv_tick_get` 0xD37980.
- LVGL heap: `lv_mem_init` (0xD39014) creates a 40 KB TLSF pool at 0x876000, shared with the whole
  UI, so apps should not allocate large buffers or hundreds of objects. The image cache is
  disabled (`_lv_img_cache_open` decodes every time), so an `lv_img_dsc_t` in RAM can be changed
  in place and redrawn with `lv_obj_invalidate` (0xD2B770). Mp3tris draws its board as a 70x140
  `LV_IMG_CF_INDEXED_4BIT` image (about 5 KB).
- Host testing: an app's `main.c` can be compiled with MSYS2 gcc by `#undef`-ing the `fw.h`
  macros it uses and defining stubs, then `#include "main.c"`. See
  `sdk/apps/mp3tris/test/host_test.c` (and `show.py`, which renders its image dumps to PNG).
- `tools/img2c.py`: PNG to `lv_img_dsc_t` C header. `tools/xref.py`: find BL/B.W callers.

### APP menu hook (`sdk/src/menu.c`)

`view_page_create` (0xD04A1C) is a switch calling each page's create function. The home grid's
"APP" item opens page 0x53, `page_home_menu_create`: a list with Alarm and Pictures (string ids
7 and 4), rows made by `fw_list_item_create` (0xD0267C) with a FontAwesome symbol label
(font `0xC810C4`) and a text label. The `bl` at `0xD04C04` is redirected to
`hook_home_menu_create`, which calls the original and appends one row per `FW_MENU_ITEM`, each
with its own key handler (ENTER opens the app, other keys go to `page_home_menu_event_cb`). The app is a
full-screen object on `lv_layer_top()`, added to the view's key group, focused, with the group
frozen.

Page 0x51 (`page_expand_home_create`, a 2x3 icon grid with 4 icons) is not reachable from the
UI on this model.

Physical buttons (measured with the key test app): Left 0x14, Right 0x13, Top 0xA0, Center 0x0A
(LV_KEY_ENTER; its KEY event comes on release, LV_EVENT_PRESSED on press), Bottom 0x81 (back).

Key codes seen in page handlers: 9/11 NEXT/PREV, 10 ENTER, 19/20 RIGHT/LEFT, 27 ESC, and vendor
codes 0x81 (leave page; "down" on the home grid), 0x87, 0x8A, 0x92, 0x98, 0x9B, 0xA0, 0xA3.

### Ghidra helpers

`ghidra/decompile.sh app out.c <name|0xaddr>...` decompiles functions from the saved project.
`SL6801Fixups.java` clears the "no return" flag from `rom_printf` (0x2B0, a boot ROM function;
the ROM block is filled with `bx lr` stubs until it is dumped).

## Flashing from Windows

- Driver: in bootloader mode the device (`301A:2800`) binds to Windows' USB mass-storage driver.
  Zadig (Options > List All Devices) replaced it with WinUSB for that VID/PID only; card reader
  mode (`301A:2801`) is unaffected.
- `smartlink_flash/` (not in git) is a clone of github.com/ilyakurdyukov/smartlink_flash (commit
  49d51d1) with `patches/smtlink_dump-write_flash-offset.patch` applied (`git apply`), built
  in MSYS2 UCRT64 (`pacman -S mingw-w64-ucrt-x86_64-libusb make`, then `make CC=gcc` from a UCRT64
  shell). `libusb-1.0.dll` is copied next to `smtlink_dump.exe` so it runs from any shell.
- Upstream bug: `write_flash <addr> <file_offset> <size> <file>` ignores `<file_offset>` and always
  takes data from the start of the file. The local copy is patched (`mem[src_offs + i + j]` in
  `write_flash()`). Flashing a whole image with `write_flash 0 0 0 image.bin` works either way and
  only rewrites sectors that differ.
- In bootloader mode always start with `init`, e.g.
  `smartlink_flash/smtlink_dump.exe init flash_id read_flash 0 2M backup.bin`.
