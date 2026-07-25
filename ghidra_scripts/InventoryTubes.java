// Dump a function inventory for the Tubes binary.
// @category Tubes

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.lang.PrototypeModel;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class InventoryTubes extends GhidraScript {

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        println("=== PROGRAM ===");
        println("image base    : " + currentProgram.getImageBase());
        println("addr range    : " + mem.getMinAddress() + " - " + mem.getMaxAddress());
        println("language      : " + currentProgram.getLanguageID());
        println("compiler spec : " + currentProgram.getCompilerSpec().getCompilerSpecID());

        println("");
        println("=== CALLING CONVENTIONS ===");
        for (PrototypeModel pm : currentProgram.getCompilerSpec().getCallingConventions()) {
            println("  " + pm.getName());
        }

        println("");
        println("=== MEMORY BLOCKS ===");
        for (MemoryBlock b : mem.getBlocks()) {
            println(String.format("  %-14s %s - %s  (%d bytes)",
                    b.getName(), b.getStart(), b.getEnd(), b.getSize()));
        }

        List<Function> funcs = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) {
            funcs.add(f);
        }
        println("");
        println("=== FUNCTIONS: " + funcs.size() + " ===");

        long total = 0;
        for (Function f : funcs) {
            total += f.getBody().getNumAddresses();
        }
        println("total bytes covered by functions: " + total);

        funcs.sort(Comparator.comparingLong(
                (Function f) -> f.getBody().getNumAddresses()).reversed());

        println("");
        println("largest 20 by body size:");
        for (int i = 0; i < Math.min(20, funcs.size()); i++) {
            Function f = funcs.get(i);
            println(String.format("  %6d  %-20s %s",
                    f.getBody().getNumAddresses(), f.getEntryPoint(), f.getName()));
        }

        // Strings are the most useful anchors for locating game subsystems.
        println("");
        println("=== STRING-REFERENCING FUNCTIONS ===");
        Map<String, List<String>> refs = new LinkedHashMap<>();
        int nstr = 0;

        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext() && !monitor.isCancelled()) {
            Data d = di.next();
            DataType dt = d.getDataType();
            String tn = dt.getName().toLowerCase();
            if (!tn.contains("string") && !tn.contains("char")) {
                continue;
            }
            Object val = d.getValue();
            if (val == null) {
                continue;
            }
            String s = val.toString().trim();
            if (s.length() < 5) {
                continue;
            }
            nstr++;
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f == null) {
                    continue;
                }
                String key = f.getEntryPoint().toString();
                refs.computeIfAbsent(key, k -> new ArrayList<>())
                    .add(s.length() > 44 ? s.substring(0, 44) : s);
            }
        }

        println("defined strings >=5 chars : " + nstr);
        println("functions referencing them: " + refs.size());

        List<String> keys = new ArrayList<>(refs.keySet());
        keys.sort((a, b) -> refs.get(b).size() - refs.get(a).size());

        for (int i = 0; i < Math.min(20, keys.size()); i++) {
            String k = keys.get(i);
            List<String> ss = refs.get(k);
            println("");
            println("  " + k + "   (" + ss.size() + " string refs)");
            for (int j = 0; j < Math.min(6, ss.size()); j++) {
                println("      " + ss.get(j));
            }
        }
    }
}
