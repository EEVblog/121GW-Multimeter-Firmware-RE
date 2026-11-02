// Applies names from a TSV file: <address> <func|label> <name> [comment]
// Lines starting with '#' are ignored. Functions are created if missing.
//
// Script args: <names.tsv>
// @category 121GW
import java.io.BufferedReader;
import java.io.FileReader;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        int funcs = 0, labels = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                String[] f = line.split("\t");
                if (f.length < 3) {
                    continue;
                }
                Address a = toAddr(Long.parseLong(f[0].replace("0x", ""), 16));
                if (f[1].equals("func")) {
                    Function fn = getFunctionAt(a);
                    if (fn == null) {
                        disassemble(a);
                        fn = createFunction(a, f[2]);
                    }
                    if (fn != null) {
                        fn.setName(f[2], SourceType.USER_DEFINED);
                        funcs++;
                    }
                } else {
                    createLabel(a, f[2], true, SourceType.USER_DEFINED);
                    labels++;
                }
                if (f.length > 3 && !f[3].isEmpty()) {
                    currentProgram.getListing().setComment(a, CodeUnit.PLATE_COMMENT, f[3]);
                }
            }
        }
        println("ApplyNames: " + funcs + " functions, " + labels + " labels");
    }
}
