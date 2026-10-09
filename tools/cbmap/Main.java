import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

/**
 * usage: java -cp asm.jar Main.java <vanilla.jar> <cb.jar> <joined.srg> <fields.csv> <methods.csv> <out.tsv>
 *
 * out.tsv rows:
 *   CL  mcpClass  notch  cbClass  mcpSuperclass
 *   FD  mcpOwner  mcpName  srgName  mcpDesc  cbOwner  cbName  cbDesc
 *   MD  mcpOwner  mcpName  srgName  mcpDesc  cbOwner  cbName  cbDesc
 */
public class Main {
    public static void main(String[] args) throws IOException {
        Map<String, Cls> vanilla = Loader.load(Path.of(args[0]));
        Map<String, Cls> cb = Loader.load(Path.of(args[1]));
        Map<String, String[]> pools = Loader.findStringPools(cb);
        int inlined = Loader.inlineStringPools(cb, pools);
        System.out.printf("vanilla=%d cb=%d pools=%d inlined=%d%n", vanilla.size(), cb.size(), pools.size(), inlined);
        Loader.analyze(vanilla);
        Loader.analyze(cb);

        Links links = new Links(vanilla, cb);
        Matcher matcher = new Matcher(links);
        matcher.run();

        Srg srg = Srg.read(Path.of(args[2]), Path.of(args[3]), Path.of(args[4]));
        Path out = Path.of(args[5]);
        write(out, vanilla, srg);
        writeReport(out.resolveSibling(out.getFileName() + ".report.txt"), vanilla, srg, matcher.conflicts());
    }

    private static void write(Path out, Map<String, Cls> vanilla, Srg srg) throws IOException {
        List<String> rows = new ArrayList<>();
        for (Cls v : vanilla.values()) {
            if (v.match == null)
                continue;
            String mcpOwner = srg.className(v.name);
            rows.add(String.join("\t", "CL", mcpOwner, v.name, v.match.name, srg.className(v.node.superName)));
            for (Fld f : v.fields.values()) {
                if (f.match == null)
                    continue;
                String srgName = srg.fieldSrg(v.name, f.node.name);
                rows.add(String.join("\t", "FD", mcpOwner, srg.mcpName(srgName), srgName, srg.mapDesc(f.node.desc),
                        f.match.owner.name, f.match.node.name, f.match.node.desc));
            }
            for (Mth m : v.methods.values()) {
                if (m.match == null)
                    continue;
                String srgName = srg.methodSrg(v.name, m.node.name, m.node.desc);
                rows.add(String.join("\t", "MD", mcpOwner, srg.mcpName(srgName), srgName, srg.mapDesc(m.node.desc),
                        m.match.owner.name, m.match.node.name, m.match.node.desc));
            }
        }
        Collections.sort(rows);
        Files.write(out, rows, StandardCharsets.UTF_8);
        System.out.println("wrote " + rows.size() + " rows to " + out);
    }

    private static void writeReport(Path out, Map<String, Cls> vanilla, Srg srg, List<String> conflicts) throws IOException {
        try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(out, StandardCharsets.UTF_8))) {
            List<String> unmatched = new ArrayList<>();
            int fields = 0, fieldsMatched = 0, methods = 0, methodsMatched = 0;
            for (Cls v : vanilla.values()) {
                if (v.match == null) {
                    unmatched.add(srg.className(v.name) + " (" + v.name + ")");
                    continue;
                }
                for (Fld f : v.fields.values()) {
                    fields++;
                    if (f.match != null)
                        fieldsMatched++;
                    else
                        w.println("unmatched field " + srg.className(v.name) + "." + srg.mcpName(srg.fieldSrg(v.name, f.node.name)));
                }
                for (Mth m : v.methods.values()) {
                    methods++;
                    if (m.match != null)
                        methodsMatched++;
                    else
                        w.println("unmatched method " + srg.className(v.name) + "."
                                + srg.mcpName(srg.methodSrg(v.name, m.node.name, m.node.desc)) + srg.mapDesc(m.node.desc));
                }
            }
            Collections.sort(unmatched);
            for (String u : unmatched)
                w.println("unmatched class " + u);
            for (String c : conflicts)
                w.println("conflict " + c);
            String summary = String.format("classes %d/%d, fields in matched classes %d/%d, methods in matched classes %d/%d, conflicts %d",
                    vanilla.size() - unmatched.size(), vanilla.size(), fieldsMatched, fields, methodsMatched, methods, conflicts.size());
            w.println(summary);
            System.out.println(summary);
        }
    }
}
