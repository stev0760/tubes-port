// Dump a whole-program structure map: units, call graph, and the strings each
// function references.
//
// The point is orientation. Borland Pascal emits one code segment per unit and
// leaves string literals in plain sight, so "which function shows the menu" is
// usually answerable by looking at what text it references. Run
// FindPascalStrings.java first - this script consumes the references it makes.
//
// Usage (headless):
//   -postScript MapProgram.java
//   -postScript MapProgram.java 1000:9e53      // just this subtree
//
// Output is deliberately grep-friendly and stable: one function per block,
// prefixed markers so a shell can slice it up.
//
// @category Tubes

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Deque;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

public class MapProgram extends GhidraScript {

    private FunctionManager fm;
    private Listing listing;

    @Override
    public void run() throws Exception {
        fm = currentProgram.getFunctionManager();
        listing = currentProgram.getListing();

        String[] args = getScriptArgs();
        Function root = null;
        if (args.length > 0) {
            root = functionAt(args[0]);
            if (root == null) {
                println("no function at " + args[0]);
                return;
            }
        }

        Map<String, List<Function>> byBlock = new TreeMap<>();
        for (Function f : fm.getFunctions(true)) {
            MemoryBlock blk = currentProgram.getMemory().getBlock(f.getEntryPoint());
            byBlock.computeIfAbsent(blk == null ? "?" : blk.getName(),
                                    k -> new ArrayList<>()).add(f);
        }

        println("=== UNITS ===");
        println("Borland Pascal emits one code segment per unit.");
        for (Map.Entry<String, List<Function>> e : byBlock.entrySet()) {
            println(String.format("  %-12s %3d functions", e.getKey(), e.getValue().size()));
        }

        Set<Function> wanted = new LinkedHashSet<>();
        if (root != null) {
            // Breadth-first so the printed order follows the call depth.
            Deque<Function> queue = new ArrayDeque<>();
            queue.add(root);
            while (!queue.isEmpty()) {
                Function f = queue.poll();
                if (!wanted.add(f)) continue;
                for (Function c : calleesOf(f)) queue.add(c);
            }
            println("");
            println("=== SUBTREE of " + name(root) + ": " + wanted.size() + " functions ===");
        } else {
            for (List<Function> fs : byBlock.values()) wanted.addAll(fs);
        }

        List<Function> ordered = new ArrayList<>(wanted);
        ordered.sort(Comparator.comparing(Function::getEntryPoint));

        println("");
        println("=== FUNCTIONS ===");
        for (Function f : ordered) {
            List<String> strings = stringsIn(f);
            Set<Function> callees = calleesOf(f);
            Set<Function> callers = callersOf(f);

            println("");
            println("@FUNC " + name(f) + "  size=" + f.getBody().getNumAddresses());
            if (!callers.isEmpty()) println("  @FROM " + join(callers));
            if (!callees.isEmpty()) println("  @CALLS " + join(callees));
            for (String s : strings) {
                println("  @STR " + s.replace("\r", "\\r").replace("\n", "\\n"));
            }
        }
        println("");
        println("total functions printed: " + ordered.size());
    }

    private Function functionAt(String spec) {
        Address a = currentProgram.getAddressFactory().getAddress(spec);
        return a == null ? null : fm.getFunctionContaining(a);
    }

    private String name(Function f) {
        return f.getName() + " @ " + f.getEntryPoint();
    }

    private String join(Set<Function> fs) {
        StringBuilder sb = new StringBuilder();
        for (Function f : fs) {
            if (sb.length() > 0) sb.append(", ");
            sb.append(f.getName());
        }
        return sb.toString();
    }

    private Set<Function> calleesOf(Function f) {
        // Prefer walking instructions over getCalledFunctions(): far calls in
        // 16-bit segmented code do not always land in the function's own
        // reference set.
        Set<Function> out = new LinkedHashSet<>();
        for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
            for (Reference r : ins.getReferencesFrom()) {
                if (!r.getReferenceType().isCall()) continue;
                Function t = fm.getFunctionAt(r.getToAddress());
                if (t != null && !t.equals(f)) out.add(t);
            }
        }
        return out;
    }

    private Set<Function> callersOf(Function f) {
        Set<Function> out = new LinkedHashSet<>();
        for (Reference r : getReferencesTo(f.getEntryPoint())) {
            if (!r.getReferenceType().isCall()) continue;
            Function c = fm.getFunctionContaining(r.getFromAddress());
            if (c != null && !c.equals(f)) out.add(c);
        }
        return out;
    }

    // Every ShortString the function refers to, in address order.
    //
    // Read straight out of memory rather than via Data.getValue(): for
    // PascalString255DataType that does not hand back a String, which is why
    // FindPascalStrings.java also decodes the bytes itself.
    private List<String> stringsIn(Function f) {
        Set<String> seen = new TreeSet<>();
        List<String> out = new ArrayList<>();
        for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
            for (Reference r : ins.getReferencesFrom()) {
                if (r.getReferenceType() != RefType.DATA) continue;
                String s = shortStringAt(r.getToAddress());
                if (s != null && s.length() >= 3 && seen.add(s)) out.add(s);
            }
        }
        return out;
    }

    private String shortStringAt(Address a) {
        try {
            int len = currentProgram.getMemory().getByte(a) & 0xff;
            if (len == 0 || len > 120) return null;
            byte[] buf = new byte[len];
            if (currentProgram.getMemory().getBytes(a.add(1), buf) != len) return null;
            StringBuilder sb = new StringBuilder();
            for (byte b : buf) {
                int c = b & 0xff;
                if (c < 0x20 || c > 0x7e) return null;   // not text
                sb.append((char) c);
            }
            return sb.toString().trim();
        } catch (Exception e) {
            return null;
        }
    }
}
