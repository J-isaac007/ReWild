// Applies zeldaret/botw function names to a BotW v1.5.0 main program.
// Reads data/uking_functions.csv (Address,Quality,Size,Name).
//
// Two modes, so auto-analysis does the heavy lifting in between:
//   labels    (run as -preScript, before analysis): adds each decomp name as a
//             label and marks every address as an entry point. Analysis then
//             creates the functions itself, and the demangler sees every name.
//   functions (run as -postScript, after analysis): creates any function that
//             analysis missed, makes sure it carries the decomp name, and tags
//             it with the decomp status: decomp_O (matching), decomp_m / decomp_M
//             (minor/major mismatch), decomp_W (WIP), decomp_L (library),
//             decomp_U (not decompiled).
//
// Args: <path to uking_functions.csv> [labels|functions]  (default: functions)
// @category reWild

import java.io.BufferedReader;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyDecompNames extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File csv = args.length > 0 ? new File(args[0]) : askFile("uking_functions.csv", "Apply");
		boolean labelsOnly = args.length > 1 && args[1].equalsIgnoreCase("labels");
		SymbolTable symbols = currentProgram.getSymbolTable();

		int rows = 0, created = 0, named = 0, unmapped = 0, failed = 0;
		Map<String, Integer> byStatus = new TreeMap<>();

		try (BufferedReader in = Files.newBufferedReader(csv.toPath(), StandardCharsets.UTF_8)) {
			String line;
			while ((line = in.readLine()) != null) {
				if (!line.startsWith("0x")) {
					continue;
				}
				monitor.checkCancelled();
				String[] parts = line.split(",", 4);
				Address addr = toAddr(Long.parseUnsignedLong(parts[0].substring(2), 16));
				String status = parts[1];
				long size = Long.parseLong(parts[2]);
				String name = parts.length > 3 ? parts[3].trim() : "";
				rows++;
				byStatus.merge(status, 1, Integer::sum);

				if (currentProgram.getMemory().getBlock(addr) == null) {
					unmapped++;
					continue;
				}

				if (labelsOnly) {
					if (!name.isEmpty() && symbols.getGlobalSymbol(name, addr) == null) {
						Symbol s = symbols.createLabel(addr, name, SourceType.IMPORTED);
						s.setPrimary();
						named++;
					}
					symbols.addExternalEntryPoint(addr);
					continue;
				}

				Function fn = getFunctionAt(addr);
				if (fn == null) {
					// Analysis missed it: disassemble the decomp's exact body and create it.
					AddressSet body = new AddressSet(addr, addr.add(Math.max(size, 4) - 1));
					new DisassembleCommand(body, null, true).applyTo(currentProgram, monitor);
					fn = createFunction(addr, null);
					if (fn == null) {
						failed++;
						continue;
					}
					created++;
				}
				// The demangler may already have renamed it (e.g. "PlayerCaught::~PlayerCaught");
				// only replace default names.
				if (!name.isEmpty() && fn.getSymbol().getSource() == SourceType.DEFAULT) {
					try {
						fn.setName(name, SourceType.IMPORTED);
						named++;
					}
					catch (Exception e) {
						createLabel(addr, name, false, SourceType.IMPORTED);
					}
				}
				fn.addTag("decomp_" + status);
			}
		}

		println(String.format("ApplyDecompNames (%s): %,d rows, %,d functions created, %,d named, %,d not in memory, %,d failed",
			labelsOnly ? "labels" : "functions", rows, created, named, unmapped, failed));
		println("  by status: " + byStatus);
	}
}
