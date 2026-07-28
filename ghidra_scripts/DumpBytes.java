// Dump initialised bytes/words at an address, for reading DATA tables.
//
// Usage (headless):
//   -postScript DumpBytes.java 2785:0000 64      # 64 bytes from DGROUP:0
//
// The game's playfield geometry lives in DGROUP as four consecutive six-word
// tables rather than as immediates, which is why no tube x ever appears in a
// comparison in the game loop. Reading them here beats inferring them from
// symmetry - column 1's feed tube was carried as "inferred" for a whole
// session because no atom happened to use it while sampling.
//
// @category Tubes

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class DumpBytes extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DumpBytes.java <addr> <count>");
            return;
        }
        Address a = currentProgram.getAddressFactory().getAddress(args[0]);
        int n = Integer.parseInt(args[1]);
        byte[] buf = new byte[n];
        int got = currentProgram.getMemory().getBytes(a, buf);

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < got; i += 16) {
            sb.setLength(0);
            sb.append(a.add(i)).append("  ");
            for (int j = 0; j < 16 && i + j < got; ++j) {
                sb.append(String.format("%02x ", buf[i + j] & 0xff));
            }
            sb.append("   words:");
            for (int j = 0; j + 1 < 16 && i + j + 1 < got; j += 2) {
                int w = (buf[i + j] & 0xff) | ((buf[i + j + 1] & 0xff) << 8);
                sb.append(String.format(" %5d", w));
            }
            println(sb.toString());
        }
    }
}
