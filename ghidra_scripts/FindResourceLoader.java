// Identify the resource-loading routine by argument flow rather than by
// shared callees.
//
// A shared-callee histogram finds graphics helpers, because every function
// that loads a resource also draws with it. Instead: a Pascal call site
// pushes the resource-name string and then calls, so the loader is whatever
// is CALLed shortly after each reference to a resource name.
//
// @category Tubes

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.PascalString255DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class FindResourceLoader extends GhidraScript {

    private static final int LOOKAHEAD = 12;

    private static boolean isResourceName(String s) {
        String u = s.toUpperCase();
        return u.endsWith(".GFX") || u.endsWith(".CSP")
                || u.endsWith(".RES") || u.endsWith(".SCR");
    }

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Map<Address, Integer> callTargets = new HashMap<>();
        Map<Address, List<String>> namesFor = new HashMap<>();
        int sites = 0;

        for (Data d : listing.getDefinedData(true)) {
            if (monitor.isCancelled()) {
                break;
            }
            if (!(d.getDataType() instanceof PascalString255DataType)) {
                continue;
            }
            Object v = d.getValue();
            if (v == null) {
                continue;
            }
            String s = v.toString().trim();
            if (!isResourceName(s)) {
                continue;
            }

            for (Reference r : getReferencesTo(d.getAddress())) {
                Instruction ins = listing.getInstructionAt(r.getFromAddress());
                if (ins == null) {
                    continue;
                }
                sites++;
                // Walk forward to the first call.
                Instruction cur = ins;
                for (int k = 0; k < LOOKAHEAD && cur != null; k++) {
                    cur = listing.getInstructionAfter(cur.getAddress());
                    if (cur == null) {
                        break;
                    }
                    if (!cur.getFlowType().isCall()) {
                        continue;
                    }
                    for (Address t : cur.getFlows()) {
                        callTargets.merge(t, 1, Integer::sum);
                        namesFor.computeIfAbsent(t, x -> new ArrayList<>()).add(s);
                    }
                    break;
                }
            }
        }

        println("resource-name reference sites: " + sites);
        println("");
        println("=== CALL TARGETS FOLLOWING A RESOURCE NAME ===");

        List<Map.Entry<Address, Integer>> ranked = new ArrayList<>(callTargets.entrySet());
        ranked.sort(Comparator.comparingInt(
                (Map.Entry<Address, Integer> e) -> e.getValue()).reversed());

        for (int i = 0; i < Math.min(10, ranked.size()); i++) {
            Address a = ranked.get(i).getKey();
            Function f = fm.getFunctionAt(a);
            List<String> ns = namesFor.get(a);
            println(String.format("  %-20s %3d sites  body=%s",
                    a.toString(), ranked.get(i).getValue(),
                    f == null ? "?" : String.valueOf(f.getBody().getNumAddresses())));
            StringBuilder sb = new StringBuilder("      ");
            for (int j = 0; j < Math.min(6, ns.size()); j++) {
                sb.append(ns.get(j)).append(" ");
            }
            println(sb.toString());
        }

        if (ranked.isEmpty()) {
            println("no candidates found");
            return;
        }

        DecompInterface ifc = new DecompInterface();
        ifc.setOptions(new DecompileOptions());
        ifc.toggleCCode(true);
        if (!ifc.openProgram(currentProgram)) {
            println("decompiler failed to open: " + ifc.getLastMessage());
            return;
        }

        // Decompile the top candidates plus everything they call, one level
        // deep - the loader usually delegates the actual read.
        for (int i = 0; i < Math.min(2, ranked.size()); i++) {
            Function f = fm.getFunctionAt(ranked.get(i).getKey());
            if (f == null) {
                continue;
            }
            List<Function> group = new ArrayList<>();
            group.add(f);
            for (Function c : f.getCalledFunctions(monitor)) {
                if (c.getBody().getNumAddresses() < 400) {
                    group.add(c);
                }
            }
            for (Function g : group) {
                try {
                    g.setCallingConvention("__stdcall16far");
                } catch (Exception e) {
                    // keep inferred convention
                }
                println("");
                println("//=================================================");
                println("// " + g.getName() + " @ " + g.getEntryPoint()
                        + "  (" + g.getBody().getNumAddresses() + " bytes)");
                println("//=================================================");
                DecompileResults res = ifc.decompileFunction(g, 120, monitor);
                if (res == null || !res.decompileCompleted()) {
                    println("// FAILED: "
                            + (res == null ? "null" : res.getErrorMessage()));
                    continue;
                }
                println(res.getDecompiledFunction().getC());
            }
        }
        ifc.dispose();
    }
}
