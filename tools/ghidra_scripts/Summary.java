// Prints a one-line summary of the current program: memory, function count, entry.
// @category 121GW
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.FunctionIterator;

public class Summary extends GhidraScript {
    @Override
    public void run() throws Exception {
        int count = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            it.next();
            count++;
        }
        println(String.format("SUMMARY %s lang=%s min=%s max=%s functions=%d",
                currentProgram.getName(),
                currentProgram.getLanguageID(),
                currentProgram.getMinAddress(),
                currentProgram.getMaxAddress(),
                count));
    }
}
