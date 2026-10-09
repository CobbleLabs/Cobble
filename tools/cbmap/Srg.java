import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

/** MCP joined.srg (Notch -> SRG) plus fields.csv / methods.csv (SRG -> MCP). */
final class Srg {
    private final Map<String, String> classes = new HashMap<>();
    private final Map<String, String> fields = new HashMap<>();
    private final Map<String, String> methods = new HashMap<>();
    private final Map<String, String> names = new HashMap<>();

    static Srg read(Path joined, Path fieldsCsv, Path methodsCsv) throws IOException {
        Srg srg = new Srg();
        for (String line : Files.readAllLines(joined, StandardCharsets.UTF_8)) {
            String[] p = line.split(" ");
            switch (p[0]) {
                case "CL:" -> srg.classes.put(p[1], p[2]);
                case "FD:" -> srg.fields.put(p[1], p[2].substring(p[2].lastIndexOf('/') + 1));
                case "MD:" -> srg.methods.put(p[1] + p[2], p[3].substring(p[3].lastIndexOf('/') + 1));
                default -> {}
            }
        }
        for (Path csv : new Path[]{fieldsCsv, methodsCsv})
            for (String line : Files.readAllLines(csv, StandardCharsets.UTF_8)) {
                String[] p = line.split(",", 3);
                if (p.length >= 2 && !p[0].equals("searge"))
                    srg.names.put(p[0], p[1]);
            }
        return srg;
    }

    String className(String notch) { return classes.getOrDefault(notch, notch); }

    String fieldSrg(String owner, String name) { return fields.getOrDefault(owner + "/" + name, name); }

    String methodSrg(String owner, String name, String desc) { return methods.getOrDefault(owner + "/" + name + desc, name); }

    String mcpName(String srgName) { return names.getOrDefault(srgName, srgName); }

    String mapDesc(String desc) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < desc.length(); ) {
            char ch = desc.charAt(i);
            if (ch != 'L') {
                out.append(ch);
                i++;
                continue;
            }
            int end = desc.indexOf(';', i);
            out.append('L').append(className(desc.substring(i + 1, end))).append(';');
            i = end + 1;
        }
        return out.toString();
    }
}
