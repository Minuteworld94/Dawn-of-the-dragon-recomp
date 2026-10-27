// Decompiles the functions containing the given addresses and writes the C to a file.
// Usage (headless):  -postScript DecompileFuncs.java <out-file> <addr> [<addr> ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("DecompileFuncs: need <out-file> and at least one address");
            return;
        }
        PrintWriter out = new PrintWriter(new FileWriter(args[0]));

        // The compiler's register save/restore helpers (__savegprlr_N / __restgprlr_N etc.,
        // 0x8262E400-0x8262E800) get flagged "no return" by auto-analysis, which truncates every
        // function that calls one in its prologue. Undo that before decompiling anything.
        Address helpersLo = currentProgram.getAddressFactory().getAddress("0x8262E400");
        Address helpersHi = currentProgram.getAddressFactory().getAddress("0x8262E800");
        for (Function h : currentProgram.getFunctionManager().getFunctions(helpersLo, true)) {
            if (h.getEntryPoint().compareTo(helpersHi) >= 0) break;
            if (h.hasNoReturn()) h.setNoReturn(false);
        }

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try {
            for (int i = 1; i < args.length; i++) {
                Address a = currentProgram.getAddressFactory().getAddress(args[i]);
                Function f = (a == null) ? null : getFunctionContaining(a);
                // A body of a couple of instructions means it was cut short by the bad
                // no-return flag: rebuild it so the whole function is decompiled.
                if (f != null && f.getBody().getNumAddresses() <= 12) {
                    Address entry = f.getEntryPoint();
                    removeFunction(f);
                    disassemble(entry);
                    f = createFunction(entry, null);
                }
                if (f == null && a != null) {
                    // Ghidra only knows the .pdata functions; vtable-only entry points are
                    // missing, so disassemble and define one here.
                    disassemble(a);
                    f = createFunction(a, null);
                }
                if (f == null) {
                    out.println("// ==== " + args[i] + ": no function here");
                    continue;
                }
                out.println("// ======== requested " + args[i] + " -> " + f.getName()
                        + " @ " + f.getEntryPoint() + "  size=" + f.getBody().getNumAddresses());
                DecompileResults r = di.decompileFunction(f, 180, monitor);
                if (r.decompileCompleted()) {
                    out.println(r.getDecompiledFunction().getC());
                } else {
                    out.println("// decompile failed: " + r.getErrorMessage());
                }
                out.flush();
            }
        } finally {
            di.dispose();
            out.close();
        }
        println("DecompileFuncs: wrote " + args[0]);
    }
}
