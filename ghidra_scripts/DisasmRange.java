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

import java.util.ArrayList;

public class DisasmRange extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: DisasmRange.java <start> [<end>]");
            return;
        }

        // `+disasm` first: turn undefined bytes in the range into code before
        // listing them. Ghidra's auto-analysis misses Turbo Pascal's NESTED
        // procedures, because the only reference to one is a near CALL whose
        // target Ghidra renders with a 0x10000 bias (see reversing-notes) and
        // so never follows. Without this the listing comes back EMPTY for an
        // address that plainly holds code, which reads as "there is nothing
        // there" rather than "Ghidra did not look".
        boolean doDisasm = false;
        java.util.List<String> rest = new ArrayList<>();
        for (String a : args) {
            if (a.equals("+disasm")) doDisasm = true;
            else rest.add(a);
        }
        args = rest.toArray(new String[0]);
        if (args.length == 0) {
            println("usage: DisasmRange.java [+disasm] <start> [<end>]");
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

        if (doDisasm) {
            int made = 0;
            Address a = start;
            while (a != null && a.compareTo(end) <= 0) {
                if (getInstructionAt(a) == null) {
                    if (disassemble(a)) made++;
                }
                Instruction ins = getInstructionAt(a);
                if (ins == null) break;          // still undefined: real data
                a = ins.getMaxAddress().next();
            }
            println("// +disasm: created " + made + " instruction runs");
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
