// Lists every call site of the given functions and the function containing each call.
// Usage (headless):  -postScript FindCallers.java <out-file> <addr> [<addr> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(args[0]));
        try {
            for (int i = 1; i < args.length; i++) {
                Address target = toAddr(Long.decode(args[i]));
                out.println("== callers of " + target);
                int n = 0;
                for (Reference r : getReferencesTo(target)) {
                    Address from = r.getFromAddress();
                    Function f = getFunctionContaining(from);
                    out.println(String.format("  %s  %-10s in %s", from, r.getReferenceType(),
                            f == null ? "(no function)" : f.getName() + " @ " + f.getEntryPoint()));
                    n++;
                }
                out.println("  total: " + n);
            }
        } finally {
            out.close();
        }
    }
}
