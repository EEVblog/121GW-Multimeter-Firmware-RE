// Lists memory blocks with permissions, and the data defined at an optional address.
// @category 121GW
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.mem.MemoryBlock;

public class Blocks extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            println(String.format("BLOCK %-12s %s-%s r=%b w=%b x=%b vol=%b init=%b",
                b.getName(), b.getStart(), b.getEnd(), b.isRead(), b.isWrite(),
                b.isExecute(), b.isVolatile(), b.isInitialized()));
        }
        for (String a : getScriptArgs()) {
            Data d = getDataAt(toAddr(a));
            println("DATA " + a + " = " + (d == null ? "none" : d.getDataType().getName() + " " + d.getValue()));
        }
    }
}
