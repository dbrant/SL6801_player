// Post-analysis fixups that need a saved program: clears the "no return" flag that analysis puts on
// ROM stubs (rom_printf is the firmware's log function and does return).
//@category SL6801
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class SL6801Fixups extends GhidraScript {
	@Override
	public void run() throws Exception {
		for (Function f : currentProgram.getFunctionManager().getFunctions(toAddr(0), true)) {
			if (f.getEntryPoint().getOffset() >= 0x20000) break;
			if (f.hasNoReturn()) {
				f.setNoReturn(false);
				println("cleared noreturn on " + f.getName());
			}
		}
	}
}
