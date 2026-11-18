// Export every instruction with its raw p-code, flows and the function list, for the
// static recompiler (port/lift.py). Before exporting, disassembles + creates functions at
// code addresses referenced through relocations (vtables, callbacks) that analysis missed.
// Output (text, one record per line):
//   R <name> <regoffset hex> <size>            register map
//   B <name> <start> <end> <x|->               memory blocks
//   FN <entry hex> <name>
//   I <addr> <len> <bytes hex> | <listing text>
//   F <target hex> ...                          flows of preceding instruction (branches/calls/computed)
//   P <opcode> <out|-> <in> ...                p-code op; varnode = space:offset:size (offset hex)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.reloc.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class ExportPcode extends GhidraScript {
  String vn(Varnode v) {
    if (v == null) return "-";
    String sp = v.getAddress().getAddressSpace().getName();
    String s;
    if (v.isConstant()) s = "c";
    else if (v.isRegister()) s = "r";
    else if (v.isUnique()) s = "u";
    else if (sp.equals("ram")) s = "m";
    else s = sp;
    return s + ":" + Long.toHexString(v.getOffset()) + ":" + v.getSize();
  }

  public void run() throws Exception {
    String out = getScriptArgs()[0];
    Listing L = currentProgram.getListing();
    Memory mem = currentProgram.getMemory();
    AddressSpace ram = currentProgram.getAddressFactory().getDefaultAddressSpace();

    // 1. functions at relocation targets in executable memory
    int made = 0;
    for (int pass = 0; pass < 3; pass++) {
      Iterator<Relocation> it = currentProgram.getRelocationTable().getRelocations();
      List<Address> cand = new ArrayList<>();
      while (it.hasNext()) {
        Relocation r = it.next();
        Address at = r.getAddress();
        MemoryBlock ab = mem.getBlock(at);
        if (ab == null) continue;
        long val;
        try { val = mem.getInt(at) & 0xffffffffL; } catch (Exception e) { continue; }
        Address tgt = ram.getAddress(val);
        MemoryBlock tb = mem.getBlock(tgt);
        if (tb == null || !tb.isExecute()) continue;
        if (L.getInstructionContaining(tgt) != null) continue;
        if (L.getDefinedDataContaining(tgt) != null) continue;
        if (ab.isExecute()) {
          Instruction ins = L.getInstructionContaining(at);
          if (ins == null) continue;
          String m = ins.getMnemonicString();
          if (!(m.equals("PUSH") || m.equals("MOV"))) continue;
          // only immediate operands (not memory operands)
          boolean imm = false;
          for (int i = 0; i < ins.getNumOperands(); i++) {
            Object[] o = ins.getOpObjects(i);
            if ((ins.getOperandType(i) & ghidra.program.model.lang.OperandType.ADDRESS) != 0 &&
                (ins.getOperandType(i) & ghidra.program.model.lang.OperandType.INDIRECT) == 0 &&
                (ins.getOperandType(i) & ghidra.program.model.lang.OperandType.DYNAMIC) == 0) imm = true;
            if ((ins.getOperandType(i) & ghidra.program.model.lang.OperandType.SCALAR) != 0 &&
                (ins.getOperandType(i) & ghidra.program.model.lang.OperandType.DYNAMIC) == 0) imm = true;
          }
          if (!imm) continue;
        }
        cand.add(tgt);
      }
      int before = made;
      for (Address a : cand) {
        if (L.getInstructionContaining(a) != null) continue;
        if (disassemble(a)) {
          if (getFunctionAt(a) == null && createFunction(a, null) != null) made++;
        }
      }
      println("pass " + pass + ": candidates " + cand.size() + ", functions created " + (made - before));
      if (made == before) break;
    }

    PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)));
    for (Register r : currentProgram.getLanguage().getRegisters()) {
      w.println("R " + r.getName() + " " + Long.toHexString(r.getOffset()) + " " + r.getMinimumByteSize());
    }
    for (MemoryBlock b : mem.getBlocks()) {
      w.println("B " + b.getName() + " " + b.getStart().toString() + " " + b.getEnd().toString() + " " + (b.isExecute() ? "x" : "-"));
    }
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      w.println("FN " + f.getEntryPoint().toString() + " " + f.getName());
    }
    int n = 0;
    for (Instruction ins : L.getInstructions(true)) {
      StringBuilder sb = new StringBuilder();
      sb.append("I ").append(ins.getAddress().toString()).append(' ').append(ins.getLength()).append(' ');
      for (byte b : ins.getBytes()) sb.append(String.format("%02x", b & 0xff));
      sb.append(" | ").append(ins.toString());
      w.println(sb);
      Address[] fl = ins.getFlows();
      if (fl != null && fl.length > 0) {
        StringBuilder f = new StringBuilder("F");
        for (Address a : fl) f.append(' ').append(a.toString());
        w.println(f);
      }
      for (PcodeOp op : ins.getPcode()) {
        StringBuilder p = new StringBuilder("P ");
        p.append(op.getMnemonic()).append(' ').append(vn(op.getOutput()));
        for (int i = 0; i < op.getNumInputs(); i++) p.append(' ').append(vn(op.getInput(i)));
        if (op.getOpcode() == PcodeOp.CALLOTHER) {
          p.append(" #").append(currentProgram.getLanguage().getUserDefinedOpName((int) op.getInput(0).getOffset()));
        }
        w.println(p);
      }
      n++;
    }
    w.close();
    println("exported " + n + " instructions, functions created " + made);
  }
}
