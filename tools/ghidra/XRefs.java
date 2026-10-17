// Lista as referencias (xrefs) para os enderecos dados, com a funcao de origem. Uso (headless, -readOnly):
//   -postScript XRefs.java <saida.txt> <end_hex> [<end_hex> ...]
// @category RDR
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

public class XRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                out.println("// ===== refs para " + args[i]);
                for (Reference r : getReferencesTo(toAddr(Long.parseLong(args[i], 16)))) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    out.println(r.getFromAddress() + " " + r.getReferenceType() + " em " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
                }
            }
        }
    }
}
