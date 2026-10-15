// Decompila as funcoes que contem os enderecos dados e grava o C num arquivo. Uso (headless, -readOnly):
//   -postScript DecompileFuncs.java <saida.txt> <end_hex> [<end_hex> ...]
// @category RDR
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class DecompileFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionContaining(toAddr(Long.parseLong(args[i], 16)));
                if (f == null) { out.println("// sem funcao em " + args[i]); continue; }
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                out.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
                out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// falhou: " + r.getErrorMessage());
            }
        }
        di.dispose();
    }
}
