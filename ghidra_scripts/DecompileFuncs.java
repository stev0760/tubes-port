// Decompile named functions and, optionally, their callees.
//
// Usage (headless):
//   -postScript DecompileFuncs.java 2407:0146 21ea:035b
//   -postScript DecompileFuncs.java +callees 2407:0146
//
// Sets the Pascal calling convention before decompiling, since Borland Pascal
// is callee-cleans and Ghidra's inferred cdecl signatures come out wrong.
//
// @category Tubes

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class DecompileFuncs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: DecompileFuncs.java [+callees] <addr> [<addr>...]");
            return;
        }

        boolean withCallees = false;
        List<String> addrs = new ArrayList<>();
        for (String a : args) {
            if (a.equals("+callees")) {
                withCallees = true;
            } else {
                addrs.add(a);
            }
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        Set<Function> targets = new LinkedHashSet<>();

        for (String s : addrs) {
            Address a = currentProgram.getAddressFactory().getAddress(s);
            if (a == null) {
                println("// bad address: " + s);
                continue;
            }
            Function f = fm.getFunctionAt(a);
            if (f == null) {
                f = fm.getFunctionContaining(a);
            }
            if (f == null) {
                println("// no function at " + s);
                continue;
            }
            targets.add(f);
            if (withCallees) {
                for (Function c : f.getCalledFunctions(monitor)) {
                    if (c.getBody().getNumAddresses() < 500) {
                        targets.add(c);
                    }
                }
            }
        }

        DecompInterface ifc = new DecompInterface();
        ifc.setOptions(new DecompileOptions());
        ifc.toggleCCode(true);
        if (!ifc.openProgram(currentProgram)) {
            println("// decompiler failed to open: " + ifc.getLastMessage());
            return;
        }

        for (Function f : targets) {
            if (monitor.isCancelled()) {
                break;
            }
            try {
                f.setCallingConvention("__stdcall16far");
            } catch (Exception e) {
                // keep whatever Ghidra inferred
            }
            println("");
            println("//=================================================");
            println("// " + f.getName() + " @ " + f.getEntryPoint()
                    + "  (" + f.getBody().getNumAddresses() + " bytes)");
            println("//=================================================");
            DecompileResults res = ifc.decompileFunction(f, 180, monitor);
            if (res == null || !res.decompileCompleted()) {
                println("// FAILED: "
                        + (res == null ? "null" : res.getErrorMessage()));
                continue;
            }
            println(res.getDecompiledFunction().getC());
        }
        ifc.dispose();
    }
}
