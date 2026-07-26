// List functions containing an instruction with a given scalar operand.
//
// Useful for tracking DS-relative globals, which Ghidra does not turn into
// references in 16-bit segmented code, so the usual xref view is empty.
//
// Usage (headless):
//   -postScript FindScalarRefs.java 0xd24 [0x2056 ...]
//
// @category Tubes

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class FindScalarRefs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: FindScalarRefs.java <value> [<value>...]");
            return;
        }

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        for (String a : args) {
            long want = Long.decode(a);
            println("");
            println("=== instructions with scalar " + a + " ===");
            Map<String, List<String>> hits = new LinkedHashMap<>();

            for (Instruction ins : listing.getInstructions(true)) {
                if (monitor.isCancelled()) {
                    break;
                }
                boolean match = false;
                for (int op = 0; op < ins.getNumOperands() && !match; op++) {
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar
                                && ((Scalar) o).getUnsignedValue() == want) {
                            match = true;
                            break;
                        }
                    }
                }
                if (!match) {
                    continue;
                }
                Function f = fm.getFunctionContaining(ins.getAddress());
                String key = (f == null)
                        ? "(no function)"
                        : f.getName() + " @ " + f.getEntryPoint();
                hits.computeIfAbsent(key, k -> new ArrayList<>())
                    .add(ins.getAddress() + "  " + ins.toString());
            }

            for (Map.Entry<String, List<String>> e : hits.entrySet()) {
                println("  " + e.getKey() + "   (" + e.getValue().size() + ")");
                for (int i = 0; i < Math.min(4, e.getValue().size()); i++) {
                    println("      " + e.getValue().get(i));
                }
            }
            if (hits.isEmpty()) {
                println("  none");
            }
        }
    }
}
