// One-time repair of the Ghidra project (run once; the program is saved afterwards).
//
// Problem: auto-analysis marks the compiler's register save/restore helpers
// (__savegprlr_N / __restgprlr_N / fpr / vmx variants, 0x8262E400-0x8262E800) as "no return".
// Nearly every function's prologue is `mflr r12 ; bl __savegprlr_N`, so Ghidra stops
// disassembling right after that call and the function ends up 8 bytes long.
//
// Fix: clear no-return on the helpers, drop the fall-through/flow overrides on every call into
// them, disassemble the code that follows, and recompute every function body.
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.util.ArrayList;
import java.util.List;

public class FixSaveRestHelpers extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address lo = toAddr(0x8262E400L);
        Address hi = toAddr(0x8262E800L);
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        int helpers = 0;
        for (Function h : fm.getFunctions(new AddressSet(lo, hi), true)) {
            if (h.hasNoReturn()) {
                h.setNoReturn(false);
                helpers++;
            }
        }

        List<Address> resume = new ArrayList<>();
        int calls = 0;
        InstructionIterator it = listing.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            Address[] flows = ins.getFlows();
            if (flows.length != 1) continue;
            if (flows[0].compareTo(lo) < 0 || flows[0].compareTo(hi) >= 0) continue;
            calls++;
            if (ins.getFlowOverride() != FlowOverride.NONE) ins.setFlowOverride(FlowOverride.NONE);
            ins.clearFallThroughOverride();
            Address next = ins.getAddress().add(ins.getLength());
            if (listing.getInstructionAt(next) == null) resume.add(next);
        }

        int disassembled = 0;
        for (Address a : resume) {
            if (monitor.isCancelled()) break;
            if (listing.getInstructionAt(a) == null && disassemble(a)) disassembled++;
        }

        int fixed = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            long before = f.getBody().getNumAddresses();
            CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
            if (f.getBody().getNumAddresses() != before) fixed++;
        }

        println(String.format("FixSaveRestHelpers: helpers un-noreturned=%d, helper calls=%d, "
                + "resumed disassembly at %d sites, function bodies changed=%d",
                helpers, calls, disassembled, fixed));
    }
}
