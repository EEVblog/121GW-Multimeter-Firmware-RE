// Exports the analysed program as text for review:
//   <out>/functions.tsv  address, name, size, callers, callees
//   <out>/decomp.c       decompiled C of every function, in address order
//   <out>/strings.tsv    defined strings
//
// Script args: <output dir>
// @category 121GW
import java.io.File;
import java.io.PrintWriter;
import java.util.*;
import java.util.stream.Collectors;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]);
        out.mkdirs();

        // Without explicit options the native decompiler ignores read-only flags, so
        // peripheral addresses loaded from literal pools would show as *DAT_xxx.
        DecompileOptions opts = new DecompileOptions();
        opts.grabFromProgram(currentProgram);
        opts.setRespectReadOnly(true);
        DecompInterface di = new DecompInterface();
        di.setOptions(opts);
        di.openProgram(currentProgram);

        try (PrintWriter ft = new PrintWriter(new File(out, "functions.tsv"), "UTF-8");
             PrintWriter dc = new PrintWriter(new File(out, "decomp.c"), "UTF-8")) {
            ft.println("addr\tname\tsize\tcallers\tcallees");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                String callers = f.getCallingFunctions(monitor).stream()
                    .map(Function::getName).sorted().collect(Collectors.joining(","));
                String callees = f.getCalledFunctions(monitor).stream()
                    .map(Function::getName).sorted().collect(Collectors.joining(","));
                ft.printf("%s\t%s\t%d\t%s\t%s%n", f.getEntryPoint(), f.getName(),
                    f.getBody().getNumAddresses(), callers, callees);

                DecompileResults r = di.decompileFunction(f, 60, monitor);
                dc.printf("%n// ==== %s @ %s (%d bytes) ====%n", f.getName(),
                    f.getEntryPoint(), f.getBody().getNumAddresses());
                if (r != null && r.decompileCompleted()) {
                    dc.println(r.getDecompiledFunction().getC());
                } else {
                    dc.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
                }
            }
        }

        try (PrintWriter sw = new PrintWriter(new File(out, "strings.tsv"), "UTF-8")) {
            DataIterator d = currentProgram.getListing().getDefinedData(true);
            while (d.hasNext()) {
                Data x = d.next();
                if (x.hasStringValue()) {
                    sw.printf("%s\t%s%n", x.getAddress(), x.getDefaultValueRepresentation());
                }
            }
        }
        di.dispose();
        println("Export complete: " + out);
    }
}
