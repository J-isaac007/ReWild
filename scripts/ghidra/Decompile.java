// Prints Ghidra's decompiled C of the functions containing the given addresses (decomp / Ghidra addresses: main at
// 0x7100000000; the runtime's 0x70... addresses are shifted up by 0x100000000 here). Read-only.
// Headless: -process main.nso -noanalysis -readOnly -postScript Decompile.java <addr> [addr...]
// @category reWild

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class Decompile extends GhidraScript {

	@Override
	protected void run() throws Exception {
		DecompInterface d = new DecompInterface();
		d.openProgram(currentProgram);
		for (String arg : getScriptArgs()) {
			long v = Long.parseUnsignedLong(arg.replaceFirst("^0x", ""), 16);
			if (Long.compareUnsigned(v, 0x7100000000L) < 0) {
				v += 0x100000000L;  // runtime layout -> decomp layout
			}
			Address a = toAddr(v);
			Function f = getFunctionContaining(a);
			if (f == null) {
				println("DECOMPILE " + a + ": no function");
				continue;
			}
			DecompileResults r = d.decompileFunction(f, 60, monitor);
			println("DECOMPILE " + a + " in " + f.getName(true) + " @ " + f.getEntryPoint() + "\n" +
				(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "failed: " + r.getErrorMessage()));
		}
		d.dispose();
	}
}
