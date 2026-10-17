// Arvore de chamadores (ate N niveis) das funcoes que contem os enderecos dados. Uso (headless, -readOnly):
//   -postScript Callers.java <saida.txt> <niveis> <end_hex> [<end_hex> ...]
// Referencias de dados (ex.: ponteiros em vtables) aparecem como "DATA" e nao sao seguidas.
// @category RDR
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class Callers extends GhidraScript {
    private PrintWriter out;
    private final Set<Long> seen = new HashSet<>();

    private void walk(Function f, int depth, int max) {
        String pad = "  ".repeat(depth);
        if (!seen.add(f.getEntryPoint().getOffset())) {
            out.println(pad + f.getName() + " (ja listada)");
            return;
        }
        out.println(pad + f.getName() + "@" + f.getEntryPoint());
        if (depth >= max)
            return;
        for (Reference r : getReferencesTo(f.getEntryPoint())) {
            Function c = getFunctionContaining(r.getFromAddress());
            if (r.getReferenceType().isCall() && c != null) {
                walk(c, depth + 1, max);
            } else {
                out.println(pad + "  <- " + r.getReferenceType() + " de " + r.getFromAddress()
                        + (c == null ? "" : " em " + c.getName()));
            }
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int max = Integer.parseInt(args[1]);
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            out = w;
            for (int i = 2; i < args.length; i++) {
                Function f = getFunctionContaining(toAddr(Long.parseLong(args[i], 16)));
                out.println("// ===== chamadores de " + args[i]);
                if (f != null)
                    walk(f, 0, max);
            }
        }
    }
}
