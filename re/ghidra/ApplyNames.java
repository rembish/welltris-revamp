// Ghidra headless script: apply symbol names from names.txt.
// Line format:  <addr> <name> [# comment]
//   addr = "XXXX" (code offset in segment 1000) or "ds:XXXX" (data in DGROUP 1a39)
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String file = getScriptArgs().length > 0 ? getScriptArgs()[0] : "names.txt";
        int n = 0;
        for (String line : Files.readAllLines(Paths.get(file))) {
            String comment = null;
            int h = line.indexOf('#');
            if (h >= 0) { comment = line.substring(h + 1).trim(); line = line.substring(0, h); }
            String[] p = line.trim().split("\\s+");
            if (p.length < 2) continue;
            boolean data = p[0].startsWith("ds:");
            String off = data ? p[0].substring(3) : p[0];
            Address a = toAddr((data ? "1a39:" : "1000:") + off);
            if (!data) {
                Function f = getFunctionAt(a);
                if (f == null) { disassemble(a); new CreateFunctionCmd(a).applyTo(currentProgram); f = getFunctionAt(a); }
                if (f != null) { f.setName(p[1], SourceType.USER_DEFINED); if (comment != null) f.setComment(comment); }
            } else {
                createLabel(a, p[1], true, SourceType.USER_DEFINED);
                if (comment != null) setEOLComment(a, comment);
            }
            n++;
        }
        println("applied " + n + " names");
    }
}
