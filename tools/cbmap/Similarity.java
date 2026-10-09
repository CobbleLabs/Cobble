import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;
import org.objectweb.asm.tree.AbstractInsnNode;
import org.objectweb.asm.tree.FieldInsnNode;

import java.util.*;

/** Fallback passes for what structure and votes leave open: scored candidates, mutual best with a margin. */
final class Similarity {
    private static final double CLASS_MIN = 0.6, METHOD_MIN = 0.6, FIELD_MIN = 0.5, MARGIN = 0.1;

    private final Links links;

    Similarity(Links links) { this.links = links; }

    void matchClasses() {
        List<Cls> left = links.vanilla.values().stream().filter(c -> c.match == null).toList();
        List<Cls> right = links.cb.values().stream().filter(c -> c.match == null).toList();
        Map<Cls, Map<Cls, Double>> scores = new HashMap<>();
        for (Cls v : left)
            for (Cls c : right)
                if (v.isInterface() == c.isInterface() && v.isEnum() == c.isEnum() && sameExternalSuper(v, c))
                    scores.computeIfAbsent(v, k -> new HashMap<>()).put(c, classScore(v, c));
        for (Map.Entry<Cls, Cls> e : mutualBest(scores, CLASS_MIN).entrySet())
            links.link(e.getKey(), e.getValue());
    }

    private static boolean sameExternalSuper(Cls v, Cls c) {
        return (v.superCls == null) == (c.superCls == null) && (v.superCls != null || v.node.superName.equals(c.node.superName));
    }

    private double classScore(Cls v, Cls c) {
        List<String> mv = new ArrayList<>(), mc = new ArrayList<>();
        for (Fld f : v.fields.values())
            mv.add("F" + f.isStatic() + links.mapDesc(f.node.desc, true));
        for (Fld f : c.fields.values())
            mc.add("F" + f.isStatic() + links.mapDesc(f.node.desc, false));
        for (Mth m : v.methods.values())
            mv.add("M" + m.isStatic() + links.mapDesc(m.node.desc, true));
        for (Mth m : c.methods.values())
            mc.add("M" + m.isStatic() + links.mapDesc(m.node.desc, false));
        double structure = containment(mv, mc);
        double hierarchy = v.superCls != null && v.superCls.match != null ? (v.superCls.match == c.superCls ? 1 : 0) : 0.5;
        boolean hasStrings = !v.strings.isEmpty() || !c.strings.isEmpty();
        double strings = hasStrings ? jaccard(v.strings, c.strings) : structure;
        double externals = v.externals.isEmpty() && c.externals.isEmpty() ? structure : jaccard(v.externals, c.externals);
        return 0.35 * structure + 0.25 * strings + 0.2 * externals + 0.2 * hierarchy;
    }

    void matchMethods() {
        for (Cls v : links.vanilla.values()) {
            Cls c = v.match;
            if (c == null)
                continue;
            Map<Mth, Map<Mth, Double>> scores = new HashMap<>();
            for (Mth mv : v.methods.values()) {
                if (mv.match != null)
                    continue;
                for (Mth mc : c.methods.values()) {
                    if (mc.match != null || mv.isStatic() != mc.isStatic() || !links.compatibleMethodDesc(mv.node.desc, mc.node.desc))
                        continue;
                    if (Names.isReal(mv.node.name) != Names.isReal(mc.node.name))
                        continue;
                    int total = mv.tokens.size() + mc.tokens.size();
                    double score = total == 0 ? 1 : 2.0 * Voting.lcsLength(mv.tokens, mc.tokens) / total;
                    scores.computeIfAbsent(mv, k -> new HashMap<>()).put(mc, score);
                }
            }
            for (Map.Entry<Mth, Mth> e : mutualBest(scores, METHOD_MIN).entrySet())
                links.link(e.getKey(), e.getValue());
        }
    }

    /** Remaining fields are compared by which matched methods read and write them. */
    void matchFields() {
        Map<Fld, Set<String>> usageV = usage(links.vanilla, true), usageC = usage(links.cb, false);
        for (Cls v : links.vanilla.values()) {
            Cls c = v.match;
            if (c == null)
                continue;
            Map<Fld, Map<Fld, Double>> scores = new HashMap<>();
            for (Fld fv : v.fields.values()) {
                if (fv.match != null)
                    continue;
                Set<String> uv = usageV.getOrDefault(fv, Set.of());
                for (Fld fc : c.fields.values()) {
                    if (fc.match != null || fv.isStatic() != fc.isStatic()
                            || !links.compatible(Type.getType(fv.node.desc), Type.getType(fc.node.desc)))
                        continue;
                    Set<String> uc = usageC.getOrDefault(fc, Set.of());
                    if (!uv.isEmpty() || !uc.isEmpty())
                        scores.computeIfAbsent(fv, k -> new HashMap<>()).put(fc, jaccard(uv, uc));
                }
            }
            for (Map.Entry<Fld, Fld> e : mutualBest(scores, FIELD_MIN).entrySet())
                links.link(e.getKey(), e.getValue());
        }
    }

    /** Field -> "R|W vanillaMethod" for every matched method touching it. */
    private static Map<Fld, Set<String>> usage(Map<String, Cls> side, boolean isVanilla) {
        Map<Fld, Set<String>> out = new HashMap<>();
        for (Cls c : side.values()) {
            if (c.match == null)
                continue;
            for (Mth m : c.methods.values()) {
                if (m.match == null)
                    continue;
                String key = (isVanilla ? m : m.match).toString();
                for (AbstractInsnNode insn : m.code) {
                    if (!(insn instanceof FieldInsnNode fi))
                        continue;
                    Fld f = Voting.resolveField(side, fi);
                    if (f == null || f.match != null)
                        continue;
                    boolean write = fi.getOpcode() == Opcodes.PUTFIELD || fi.getOpcode() == Opcodes.PUTSTATIC;
                    out.computeIfAbsent(f, k -> new HashSet<>()).add((write ? "W " : "R ") + key);
                }
            }
        }
        return out;
    }

    // ----------------------------------------------------------------- scoring

    private static <T> Map<T, T> mutualBest(Map<T, Map<T, Double>> scores, double min) {
        Map<T, T> bestRight = new HashMap<>();
        Map<T, Double> bestRightScore = new HashMap<>();
        Map<T, T> result = new HashMap<>();
        for (Map.Entry<T, Map<T, Double>> e : scores.entrySet())
            for (Map.Entry<T, Double> s : e.getValue().entrySet())
                if (s.getValue() > bestRightScore.getOrDefault(s.getKey(), -1.0)) {
                    bestRightScore.put(s.getKey(), s.getValue());
                    bestRight.put(s.getKey(), e.getKey());
                }
        for (Map.Entry<T, Map<T, Double>> e : scores.entrySet()) {
            T best = null;
            double first = -1, second = -1;
            for (Map.Entry<T, Double> s : e.getValue().entrySet()) {
                if (s.getValue() > first) {
                    second = first;
                    first = s.getValue();
                    best = s.getKey();
                } else if (s.getValue() > second) {
                    second = s.getValue();
                }
            }
            if (best != null && first >= min && first - Math.max(second, 0) >= MARGIN && bestRight.get(best) == e.getKey())
                result.put(e.getKey(), best);
        }
        return result;
    }

    static double jaccard(Set<String> a, Set<String> b) {
        if (a.isEmpty() && b.isEmpty())
            return 0;
        int inter = 0;
        for (String s : a)
            if (b.contains(s))
                inter++;
        return (double) inter / (a.size() + b.size() - inter);
    }

    /** Share of the vanilla multiset found in the CheatBreaker one; CheatBreaker only adds members. */
    static double containment(List<String> vanilla, List<String> cb) {
        if (vanilla.isEmpty())
            return cb.isEmpty() ? 1 : 0;
        Map<String, Integer> counts = new HashMap<>();
        for (String s : cb)
            counts.merge(s, 1, Integer::sum);
        int hit = 0;
        for (String s : vanilla)
            if (counts.merge(s, -1, Integer::sum) >= 0)
                hit++;
        double sizePenalty = (double) vanilla.size() / Math.max(vanilla.size(), cb.size());
        return (double) hit / vanilla.size() * (0.7 + 0.3 * sizePenalty);
    }
}
