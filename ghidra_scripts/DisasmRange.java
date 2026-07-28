// Dump raw disassembly for a function or an explicit address range.
//
// Usage (headless):
//   -postScript DisasmRange.java 1000:3a67            # whole function
//   -postScript DisasmRange.java 1000:3a67 1000:3c00  # explicit range
//
// The decompiler cannot be trusted for this binary's NESTED Pascal procedures:
// 1000:3a67 shares 1000:9e53's frame through a static link, and Ghidra folds
// both frames into one set of `local_XXX` names. Two different bases then
// print as the same expression, which has already produced wrong structure
// readings. The listing shows the real base register for every access, so
// anything about record layout must be settled here rather than in C.
//
// @category Tubes

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class DisasmRange extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: DisasmRange.java <start> [<end>]");
            return;
        }

        Address start = parse(args[0]);
        if (start == null) {
            println("cannot parse address: " + args[0]);
            return;
        }

        Address end;
        if (args.length > 1) {
            end = parse(args[1]);
        } else {
            Function f = getFunctionContaining(start);
            if (f == null) {
                println("no function at " + args[0] + " and no end address given");
                return;
            }
            start = f.getEntryPoint();
            end = f.getBody().getMaxAddress();
        }
        if (end == null) {
            println("cannot parse end address: " + args[1]);
            return;
        }

        println("//=================================================");
        println("// " + start + " .. " + end);
        println("//=================================================");

        InstructionIterator it = currentProgram.getListing().getInstructions(start, true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (ins.getAddress().compareTo(end) > 0) break;
            println(ins.getAddress() + "  " + ins.toString());
        }
    }

    private Address parse(String s) {
        return currentProgram.getAddressFactory().getAddress(s);
    }
}
