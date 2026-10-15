// Lista as funcoes de uma faixa de enderecos com tamanho, chamadas, chamadores e
// referencias a dados/strings. Uso (headless, -readOnly):
//   -postScript DumpRegion.java <inicio_hex> <fim_hex> <saida.txt>
// @category RDR
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class DumpRegion extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[0], 16));
        Address end = toAddr(Long.parseLong(args[1], 16));
        Listing listing = currentProgram.getListing();
        ReferenceManager refs = currentProgram.getReferenceManager();
        try (PrintWriter out = new PrintWriter(args[2], "UTF-8")) {
            for (Function f : listing.getFunctions(start, true)) {
                if (f.getEntryPoint().compareTo(end) >= 0) break;
                out.printf("== %s @ %s size=%d%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
                Set<String> callers = new TreeSet<>();
                for (Reference r : refs.getReferencesTo(f.getEntryPoint())) {
                    Function c = getFunctionContaining(r.getFromAddress());
                    callers.add(c == null ? r.getFromAddress().toString() : c.getName());
                }
                out.println("   callers(" + callers.size() + "): " + String.join(", ", callers.stream().limit(12).toList()));
                Set<String> callees = new TreeSet<>();
                for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getName());
                out.println("   calls: " + String.join(", ", callees));
                Set<String> data = new TreeSet<>();
                for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                    for (Reference r : ins.getReferencesFrom()) {
                        if (r.getReferenceType().isFlow()) continue;
                        Data d = listing.getDataAt(r.getToAddress());
                        String s = (d != null && d.hasStringValue()) ? " \"" + d.getDefaultValueRepresentation() + "\"" : "";
                        data.add(r.getToAddress() + s);
                    }
                    for (int i = 0; i < ins.getNumOperands(); i++) {
                        for (Object o : ins.getOpObjects(i)) {
                            if (o instanceof ghidra.program.model.scalar.Scalar sc) {
                                long v = sc.getUnsignedValue();
                                if (v >= 0x8000 && v != 0xffff) data.add(String.format("#0x%x", v));
                            }
                        }
                    }
                }
                out.println("   refs: " + String.join(", ", data));
            }
        }
    }
}
