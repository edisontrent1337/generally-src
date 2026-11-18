// Export per-function facts for classifying game vs library code (TSV).
// Columns: addr, name, size, n_callers, callers, callees (internal addrs or DLL::name), strings
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import java.io.*;
import java.util.*;

public class ExportFuncInfo extends GhidraScript {
  String calleeName(Function f) {
    if (f.isThunk()) { Function t = f.getThunkedFunction(true); if (t != null) f = t; }
    if (f.isExternal()) {
      ExternalLocation el = f.getExternalLocation();
      return (el != null ? el.getLibraryName() : "EXT") + "::" + f.getName();
    }
    return f.getEntryPoint().toString();
  }
  public void run() throws Exception {
    PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]));
    Listing L = currentProgram.getListing();
    ReferenceManager rm = currentProgram.getReferenceManager();
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      TreeSet<String> callers = new TreeSet<>(), callees = new TreeSet<>(), strs = new TreeSet<>();
      for (Function c : f.getCallingFunctions(monitor)) callers.add(c.getEntryPoint().toString());
      for (Function c : f.getCalledFunctions(monitor)) callees.add(calleeName(c));
      for (Instruction ins : L.getInstructions(f.getBody(), true)) {
        for (Reference r : ins.getReferencesFrom()) {
          // indirect calls through the IAT
          Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(r.getToAddress());
          if (s != null && s.isExternal()) callees.add(s.getParentNamespace().getName() + "::" + s.getName());
          Data d = L.getDataAt(r.getToAddress());
          if (d != null && d.hasStringValue()) {
            String v = String.valueOf(d.getValue()).replace("\t", " ").replace("\n", "\\n").replace("\r", "");
            if (v.length() > 60) v = v.substring(0, 60);
            strs.add(v);
          }
        }
      }
      w.println(f.getEntryPoint() + "\t" + f.getName(true) + "\t" + f.getBody().getNumAddresses() + "\t"
        + callers.size() + "\t" + String.join(",", callers) + "\t" + String.join(",", callees) + "\t"
        + String.join(" | ", strs));
    }
    w.close();
  }
}
