/* Injected code lives in free flash (offset 0x1D0000) and runs in place at 0xC00000 + offset.
 * build.py sets the entry symbol to the app's first patch target. */

MEMORY
{
	CODE (rx) : ORIGIN = 0x00DD0000, LENGTH = 0x2C000 /* up to the PSMP settings at 0x1FC000 */
}

SECTIONS
{
	.text : {
		KEEP(*(.text.entry*))
		*(.text .text.*)
		*(.rodata .rodata.*)
	} > CODE

	.data : { *(.data .data.*) } > CODE
	.bss : { *(.bss .bss.* COMMON) } > CODE

	/DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
}

ASSERT(SIZEOF(.data) == 0, "writable globals are not supported: .data must be empty")
ASSERT(SIZEOF(.bss) == 0, "writable globals are not supported: .bss must be empty")
