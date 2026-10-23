// Prints the Phase 2 baseline counts for the current program:
// functions (by decomp status tag), names, imports, indirect calls/branches,
// SVCs, NVN references, and error bookmarks (instructions Ghidra couldn't model).
// Read-only; run headless with -process <program> -noanalysis -readOnly.
// @category reWild

import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Bookmark;
import ghidra.program.model.listing.BookmarkType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class BaselineReport extends GhidraScript {

	@Override
	protected void run() throws Exception {
		Map<String, Integer> byTag = new TreeMap<>();
		int functions = 0, untagged = 0, defaultNamed = 0, namespaced = 0, thunks = 0;
		for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
			monitor.checkCancelled();
			functions++;
			if (fn.isThunk()) {
				thunks++;
			}
			if (fn.getSymbol().getSource() == SourceType.DEFAULT) {
				defaultNamed++;
			}
			if (!fn.getParentNamespace().isGlobal()) {
				namespaced++;
			}
			boolean tagged = false;
			for (FunctionTag t : fn.getTags()) {
				if (t.getName().startsWith("decomp_")) {
					byTag.merge(t.getName(), 1, Integer::sum);
					tagged = true;
				}
			}
			if (!tagged) {
				untagged++;
			}
		}

		Map<String, Integer> mnemonics = new TreeMap<>();
		int instructions = 0;
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext()) {
			monitor.checkCancelled();
			Instruction ins = it.next();
			instructions++;
			String m = ins.getMnemonicString().toLowerCase();
			if (m.equals("blr") || m.equals("br") || m.equals("svc") || m.startsWith("ldxr") ||
				m.startsWith("ldaxr") || m.startsWith("stxr") || m.startsWith("stlxr") || m.startsWith("cas") ||
				m.startsWith("ldadd") || m.startsWith("swp")) {
				mnemonics.merge(m, 1, Integer::sum);
			}
		}

		int externals = 0, nvn = 0;
		SymbolIterator ext = currentProgram.getSymbolTable().getExternalSymbols();
		while (ext.hasNext()) {
			Symbol s = ext.next();
			externals++;
			if (s.getName().toLowerCase().startsWith("nvn")) {
				nvn++;
			}
		}

		Map<String, Integer> errors = new TreeMap<>();
		for (Bookmark b : (Iterable<Bookmark>) () -> currentProgram.getBookmarkManager().getBookmarksIterator(BookmarkType.ERROR)) {
			errors.merge(b.getCategory(), 1, Integer::sum);
		}

		println(String.format("BASELINE functions=%d thunks=%d untagged=%d defaultNamed=%d namespaced=%d", functions, thunks, untagged, defaultNamed, namespaced));
		println("BASELINE tags=" + byTag);
		println(String.format("BASELINE instructions=%d", instructions));
		println("BASELINE mnemonics=" + mnemonics);
		println(String.format("BASELINE externals=%d nvnExternals=%d", externals, nvn));
		println("BASELINE errorBookmarks=" + errors);
	}
}
