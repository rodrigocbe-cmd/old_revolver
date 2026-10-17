// Lista as funcoes chamadas (diretamente, ate N niveis) a partir das funcoes dadas, uma por linha
// ("endereco nivel nome"). Chamadas por ponteiro (vtable) nao aparecem. Uso (headless, -readOnly):
//   -postScript Callees.java <saida.txt> <niveis> <end_hex> [<end_hex> ...]
// @category RDR
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.Map;

public class Callees extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int max = Integer.parseInt(args[1]);
        Map<Function, Integer> depth = new HashMap<>();
        ArrayDeque<Function> queue = new ArrayDeque<>();
        for (int i = 2; i < args.length; i++) {
            Function f = getFunctionContaining(toAddr(Long.parseLong(args[i], 16)));
            if (f != null && depth.putIfAbsent(f, 0) == null)
                queue.add(f);
        }
        while (!queue.isEmpty()) {
            Function f = queue.poll();
            int d = depth.get(f);
            if (d >= max)
                continue;
            for (Function c : f.getCalledFunctions(monitor)) {
                if (depth.putIfAbsent(c, d + 1) == null)
                    queue.add(c);
            }
        }
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (Map.Entry<Function, Integer> e : depth.entrySet())
                out.println(e.getKey().getEntryPoint() + " " + e.getValue() + " " + e.getKey().getName());
        }
    }
}
