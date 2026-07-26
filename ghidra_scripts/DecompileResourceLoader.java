// Locate and decompile the TUBES.RES resource loader.
//
// Strategy: find every function that references a resource name (TUBES.RES,
// *.GFX, *.CSP) or a resource error message, then histogram their callees.
// The routine they all funnel through is the loader itself.
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
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class DecompileResourceLoader extends GhidraScript {

    private static boolean isResourceString(String s) {
        String u = s.toUpperCase();
        return u.contains(".RES") || u.contains(".GFX") || u.contains(".CSP")
                || u.contains(".SCR") || u.contains("RESOURCE");
    }

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        // Functions that mention resource names or resource errors.
        Map<Function, List<String>> seeds = new HashMap<>();
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
            if (s.length() < 4 || !isResourceString(s)) {
                continue;
            }
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) {
                    seeds.computeIfAbsent(f, k -> new ArrayList<>()).add(s);
                }
            }
        }

        println("=== FUNCTIONS MENTIONING RESOURCES: " + seeds.size() + " ===");
        for (Map.Entry<Function, List<String>> e : seeds.entrySet()) {
            println(String.format("  %-22s %d refs  e.g. %s",
                    e.getKey().getEntryPoint().toString(),
                    e.getValue().size(),
                    e.getValue().get(0)));
        }

        // Whatever they all call is the loader.
        Map<Function, Integer> callees = new HashMap<>();
        for (Function f : seeds.keySet()) {
            for (Function c : f.getCalledFunctions(monitor)) {
                callees.merge(c, 1, Integer::sum);
            }
        }

        List<Map.Entry<Function, Integer>> ranked = new ArrayList<>(callees.entrySet());
        ranked.sort(Comparator.comparingInt(
                (Map.Entry<Function, Integer> e) -> e.getValue()).reversed());

        println("");
        println("=== SHARED CALLEES (loader candidates) ===");
        for (int i = 0; i < Math.min(12, ranked.size()); i++) {
            Function c = ranked.get(i).getKey();
            println(String.format("  %-22s called by %2d of %d   body=%d",
                    c.getEntryPoint().toString(), ranked.get(i).getValue(),
                    seeds.size(), c.getBody().getNumAddresses()));
        }

        // Pascal is callee-cleans; tell the decompiler before it runs.
        Set<Function> toDecompile = new HashSet<>();
        for (int i = 0; i < Math.min(3, ranked.size()); i++) {
            toDecompile.add(ranked.get(i).getKey());
        }
        for (Function f : seeds.keySet()) {
            if (f.getBody().getNumAddresses() < 900) {
                toDecompile.add(f);
            }
        }

        for (Function f : toDecompile) {
            try {
                f.setCallingConvention("__stdcall16far");
            } catch (Exception e) {
                // fall back to whatever Ghidra inferred
            }
        }

        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(true);
        if (!ifc.openProgram(currentProgram)) {
            println("decompiler failed to open: " + ifc.getLastMessage());
            return;
        }

        List<Function> ordered = new ArrayList<>(toDecompile);
        ordered.sort(Comparator.comparingLong(f -> f.getBody().getNumAddresses()));

        for (Function f : ordered) {
            if (monitor.isCancelled()) {
                break;
            }
            println("");
            println("//===================================================");
            println("// " + f.getName() + " @ " + f.getEntryPoint()
                    + "   (" + f.getBody().getNumAddresses() + " bytes)");
            List<String> ss = seeds.get(f);
            if (ss != null) {
                Set<String> uniq = new HashSet<>(ss);
                int shown = 0;
                for (String s : uniq) {
                    println("//   uses: " + s);
                    if (++shown >= 10) {
                        break;
                    }
                }
            }
            println("//===================================================");

            DecompileResults res = ifc.decompileFunction(f, 120, monitor);
            if (res == null || !res.decompileCompleted()) {
                println("// DECOMPILE FAILED: "
                        + (res == null ? "null" : res.getErrorMessage()));
                continue;
            }
            println(res.getDecompiledFunction().getC());
        }

        ifc.dispose();
    }
}
