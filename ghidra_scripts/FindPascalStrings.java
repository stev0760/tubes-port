// Find Turbo Pascal ShortStrings and wire up cross-references to them.
//
// Ghidra's string analyzer looks for NUL-terminated C strings and so misses
// almost everything in a Borland Pascal binary, where a string is a length
// byte followed by exactly that many characters and no terminator.
//
// Pass 1 locates and defines those strings. Pass 2 scans instruction operands
// for scalars equal to a string's segment offset and creates references, which
// is what makes the string-to-subsystem mapping visible in the UI.
//
// @category Tubes

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.SegmentedAddress;
import ghidra.program.model.data.PascalString255DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class FindPascalStrings extends GhidraScript {

    // Shorter candidates are overwhelmingly coincidence in binary data.
    private static final int MIN_LEN = 5;

    private static boolean printable(int raw) {
        int c = raw & 0xff;
        return (c >= 0x20 && c <= 0x7e) || c == 0x09 || c == 0x0d || c == 0x0a;
    }

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();

        List<Address> strings = new ArrayList<>();
        Map<String, String> textAt = new HashMap<>();
        int defined = 0;
        int skipped = 0;

        println("=== PASS 1: locating ShortStrings ===");

        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized() || monitor.isCancelled()) {
                continue;
            }
            int size = (int) blk.getSize();
            byte[] buf = new byte[size];
            try {
                blk.getBytes(blk.getStart(), buf);
            } catch (Exception e) {
                println("  skipping " + blk.getName() + ": " + e.getMessage());
                continue;
            }

            int hits = 0;
            int i = 0;
            while (i < size - 1) {
                int n = buf[i] & 0xff;
                if (n < MIN_LEN || i + 1 + n > size) {
                    i++;
                    continue;
                }
                boolean ok = true;
                for (int k = 1; k <= n; k++) {
                    if (!printable(buf[i + k])) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) {
                    i++;
                    continue;
                }

                Address addr = blk.getStart().add(i);

                if (listing.getInstructionContaining(addr) != null) {
                    skipped++;
                    i++;
                    continue;
                }

                // A previous run of this script may already have defined the
                // string. Adopt it rather than skipping, or pass 2 sees an
                // empty set and creates no references.
                Data existing = listing.getDefinedDataContaining(addr);
                if (existing != null) {
                    if (existing.getDataType() instanceof PascalString255DataType) {
                        strings.add(existing.getAddress());
                        Object ev = existing.getValue();
                        textAt.put(existing.getAddress().toString(),
                                ev == null ? "" : ev.toString());
                        hits++;
                        i += 1 + n;
                        continue;
                    }
                    skipped++;
                    i++;
                    continue;
                }

                try {
                    clearListing(addr, addr.add(n));
                    Data d = createData(addr, new PascalString255DataType());
                    if (d != null) {
                        strings.add(addr);
                        Object v = d.getValue();
                        textAt.put(addr.toString(), v == null ? "" : v.toString());
                        defined++;
                        hits++;
                        i += 1 + n;
                        continue;
                    }
                } catch (Exception e) {
                    // Overlaps something we cannot displace; leave it alone.
                }
                skipped++;
                i++;
            }
            if (hits > 0) {
                println(String.format("  %-14s %d strings", blk.getName(), hits));
            }
        }

        println("defined: " + defined + "   skipped: " + skipped);

        // Turbo Pascal stores string constants in each unit's code segment, so
        // code loads them as a CS-relative 16-bit offset. Index by
        // (segment, segment-offset): SegmentedAddress.getOffset() returns the
        // flat linear address, which is NOT what appears in the instruction.
        // Restricting matches to the referring instruction's own segment also
        // suppresses most coincidental scalar hits.
        Map<Integer, Map<Long, List<Address>>> bySeg = new HashMap<>();
        for (Address a : strings) {
            if (!(a instanceof SegmentedAddress)) {
                continue;
            }
            SegmentedAddress sa = (SegmentedAddress) a;
            bySeg.computeIfAbsent(sa.getSegment(), k -> new HashMap<>())
                 .computeIfAbsent((long) sa.getSegmentOffset(), k -> new ArrayList<>())
                 .add(a);
        }

        println("");
        println("=== PASS 2: synthesising references ===");

        int made = 0;
        int scanned = 0;
        for (Instruction ins : listing.getInstructions(true)) {
            if (monitor.isCancelled()) {
                break;
            }
            scanned++;
            if (!(ins.getAddress() instanceof SegmentedAddress)) {
                continue;
            }
            int seg = ((SegmentedAddress) ins.getAddress()).getSegment();
            Map<Long, List<Address>> inSeg = bySeg.get(seg);
            if (inSeg == null) {
                continue;
            }
            for (int op = 0; op < ins.getNumOperands(); op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (!(o instanceof Scalar)) {
                        continue;
                    }
                    List<Address> targets = inSeg.get(((Scalar) o).getUnsignedValue());
                    if (targets == null) {
                        continue;
                    }
                    for (Address t : targets) {
                        refMgr.addMemoryReference(ins.getAddress(), t,
                                RefType.DATA, SourceType.ANALYSIS, op);
                        made++;
                    }
                }
            }
        }
        println("instructions scanned: " + scanned);
        println("references created  : " + made);

        // Report: which functions touch the most text.
        println("");
        println("=== FUNCTIONS BY STRING USAGE ===");
        Map<String, List<String>> perFunc = new LinkedHashMap<>();
        for (Address a : strings) {
            for (ghidra.program.model.symbol.Reference r : getReferencesTo(a)) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f == null) {
                    continue;
                }
                String key = f.getName() + " @ " + f.getEntryPoint();
                String s = textAt.getOrDefault(a.toString(), "");
                if (s.length() > 40) {
                    s = s.substring(0, 40);
                }
                perFunc.computeIfAbsent(key, k -> new ArrayList<>()).add(s);
            }
        }

        List<String> keys = new ArrayList<>(perFunc.keySet());
        keys.sort(Comparator.comparingInt((String k) -> perFunc.get(k).size()).reversed());
        println("functions referencing strings: " + keys.size());

        for (int i = 0; i < Math.min(25, keys.size()); i++) {
            String k = keys.get(i);
            List<String> ss = perFunc.get(k);
            println("");
            println("  " + k + "   (" + ss.size() + ")");
            for (int j = 0; j < Math.min(8, ss.size()); j++) {
                println("      " + ss.get(j).replace("\r", "\\r").replace("\n", "\\n"));
            }
        }
    }
}
