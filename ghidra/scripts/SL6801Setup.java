// Memory map and entry points for the SL6801 (yp3_2.0.43) flash dump. Run as a headless -preScript
// on a raw import of the 2 MB dump at base 0xC00000 with processor ARM:LE:32:Cortex.
// Programs whose name contains "boot" get the bootloader layout, all others the app layout.
//@category SL6801
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import java.io.ByteArrayInputStream;

public class SL6801Setup extends GhidraScript {
	static final long FLASH = 0xC00000L;
	byte[] flash;

	MemoryBlock init(String name, long addr, long flashOff, int len, boolean exec) throws Exception {
		byte[] b = new byte[len];
		System.arraycopy(flash, (int) flashOff, b, 0, len);
		MemoryBlock m = currentProgram.getMemory().createInitializedBlock(name, toAddr(addr),
			new ByteArrayInputStream(b), len, monitor, false);
		m.setPermissions(true, true, exec);
		m.setComment(String.format("copied from flash offset 0x%x", flashOff));
		return m;
	}

	MemoryBlock uninit(String name, long addr, long len, boolean volatil) throws Exception {
		MemoryBlock m = currentProgram.getMemory().createUninitializedBlock(name, toAddr(addr), len, false);
		m.setPermissions(true, true, false);
		m.setVolatile(volatil);
		return m;
	}

	int u32(long off) {
		return (flash[(int) off] & 0xff) | (flash[(int) off + 1] & 0xff) << 8 | (flash[(int) off + 2] & 0xff) << 16
			| (flash[(int) off + 3] & 0xff) << 24;
	}

	void vectors(long ramAddr, long flashOff, int count) throws Exception {
		Address a = toAddr(ramAddr);
		for (int i = 0; i < count; i++) {
			createData(a.add(4 * i), PointerDataType.dataType);
		}
		createLabel(a, "vector_table", true, SourceType.IMPORTED);
		for (int i = 1; i < count; i++) {
			long v = u32(flashOff + 4L * i) & 0xffffffffL;
			if ((v & 1) == 0 || v == 0) continue;
			Address t = toAddr(v & ~1L);
			if (currentProgram.getMemory().contains(t)) {
				String n = i == 1 ? "Reset_Handler" : String.format("vector_%d", i);
				addEntryPoint(t);
				createFunction(t, i == 1 ? n : null);
			}
		}
	}

	@Override
	public void run() throws Exception {
		Memory mem = currentProgram.getMemory();
		MemoryBlock fb = mem.getBlock(toAddr(FLASH));
		fb.setName("flash");
		fb.setPermissions(true, false, true);
		flash = new byte[(int) fb.getSize()];
		mem.getBytes(toAddr(FLASH), flash);
		boolean boot = currentProgram.getName().toLowerCase().contains("boot");

		// The boot ROM is not dumped yet. Fill it with "bx lr" so calls into it are treated as
		// returning functions instead of cutting off the caller's decompilation.
		byte[] rom = new byte[0x20000];
		for (int i = 0; i < rom.length; i += 2) {
			rom[i] = 0x70;
			rom[i + 1] = 0x47;
		}
		MemoryBlock romBlock = mem.createInitializedBlock("rom_stub", toAddr(0), new ByteArrayInputStream(rom),
			rom.length, monitor, false);
		romBlock.setPermissions(true, false, true);
		romBlock.setComment("placeholder: every halfword is bx lr until the real ROM is dumped");
		for (long[] f : new long[][] { { 0x2b0 }, { 0x2564 }, { 0xb2a } }) {
			disassemble(toAddr(f[0]));
		}
		createFunction(toAddr(0x2b0), "rom_printf");
		createFunction(toAddr(0x2564), "rom_2564");
		createFunction(toAddr(0xb2a), "rom_memcpy");
		uninit("periph", 0x40000000L, 0x100000, true);
		uninit("scs", 0xE000E000L, 0x1000, true);

		if (boot) {
			fb.setExecute(false); // keep the bootloader analysis out of the app code
			// HLKJ header: load 0x81FBC0 from flash 0x60, 0xC8EC bytes
			uninit("ram_lo", 0x800000, 0x81FBC0 - 0x800000, false);
			init("boot", 0x81FBC0, 0x60, 0xC8EC, true);
			uninit("ram_hi", 0x81FBC0 + 0xC8EC, 0x870000 - (0x81FBC0 + 0xC8EC), false);
			vectors(0x81FBC0, 0x60, 0x5B);
		} else {
			// FIRM: 0x1000-byte block at flash 0xE030 runs at 0x804800; .data image at flash 0x39470
			// is copied to 0x80CC40..0x81AAEC; .bss is 0x81AAEC..0x86B710 (initial SP).
			uninit("ram_lo", 0x800000, 0x4800, false);
			init("ram_vec", 0x804800, 0xE030, 0x1000, true);
			uninit("ram_gap", 0x805800, 0x80CC40 - 0x805800, false).setExecute(true);
			init(".data", 0x80CC40, 0x39470, 0x81AAEC - 0x80CC40, true);
			uninit(".bss", 0x81AAEC, 0x86B710 - 0x81AAEC, false);
			uninit("ram_hi", 0x86B710, 0x870000 - 0x86B710, false);
			vectors(0x804800, 0xE030, 0x5B);
			createLabel(toAddr(FLASH + 0xE000), "FIRM_header", true, SourceType.IMPORTED);
			createLabel(toAddr(FLASH + 0x53154), "ui_strings", true, SourceType.IMPORTED);
			createLabel(toAddr(FLASH + 0x48770), "ui_language_tables", true, SourceType.IMPORTED);

			// Seed functions from Thumb pointers into the two code ranges of FIRM.
			long[][] code = { { FLASH + 0xE030, FLASH + 0x18A8E }, { FLASH + 0xD6000, FLASH + 0x182270 } };
			int seeded = 0;
			for (long o = 0xE000; o < 0x182270; o += 4) {
				long v = u32(o) & 0xffffffffL;
				if ((v & 1) == 0) continue;
				for (long[] r : code) {
					if (v >= r[0] && v < r[1]) {
						Address t = toAddr(v & ~1L);
						if (getFunctionAt(t) == null && disassemble(t)) {
							createFunction(t, null);
							seeded++;
						}
					}
				}
			}
			println("seeded functions from pointers: " + seeded);
		}
		createLabel(toAddr(FLASH), "HLKJ_header", true, SourceType.IMPORTED);
	}
}
