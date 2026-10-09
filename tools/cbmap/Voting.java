import org.objectweb.asm.Type;
import org.objectweb.asm.tree.*;

import java.util.*;

/**
 * Aligns the bodies of matched methods and turns every pair of aligned references
 * (types, fields, methods) into a vote. Mutual, dominant votes become matches.
 */
final class Voting {
    private static final long MAX_DP_CELLS = 4_000_000;

    private final Links links;
    private final Map<Mth, int[][]> alignments = new HashMap<>();
    private final Map<Object, Map<Object, Double>> forward = new HashMap<>();
    private final Map<Object, Map<Object, Double>> backward = new HashMap<>();
    final List<String> conflicts = new ArrayList<>();

    Voting(Links links) { this.links = links; }

    void run() {
        forward.clear();
        backward.clear();
        for (Cls v : links.vanilla.values()) {
            if (v.match == null)
                continue;
            for (Mth m : v.methods.values()) {
                if (m.match == null)
                    continue;
                voteMethodDesc(m.node.desc, m.match.node.desc, 1);
                int[][] pairs = alignments.computeIfAbsent(m, k -> align(k.tokens, k.match.tokens));
                double weight = pairs.length == m.code.size() && pairs.length == m.match.code.size() ? 2 : 1;
                for (int[] p : pairs)
                    voteInsn(m.code.get(p[0]), m.match.code.get(p[1]), weight);
            }
            for (Fld f : v.fields.values())
                if (f.match != null)
                    voteType(Type.getType(f.node.desc), Type.getType(f.match.node.desc), 1);
        }
        apply(Cls.class);
        apply(Fld.class);
        apply(Mth.class);
    }

    private void apply(Class<?> kind) {
        for (Map.Entry<Object, Map<Object, Double>> e : forward.entrySet()) {
            Object v = e.getKey();
            if (!kind.isInstance(v))
                continue;
            Object best = best(e.getValue());
            if (best == null || best(backward.get(best)) != v)
                continue;
            Object current = currentMatch(v);
            if (current != null) {
                if (current != best && conflicts.size() < 200)
                    conflicts.add(v + " matched " + current + " but votes say " + best);
                continue;
            }
            if (v instanceof Cls cv)
                links.link(cv, (Cls) best);
            else if (v instanceof Fld fv)
                links.link(fv, (Fld) best);
            else
                links.link((Mth) v, (Mth) best);
        }
    }

    /** Dominant candidate: at least two votes and three quarters of the total. */
    private static Object best(Map<Object, Double> candidates) {
        if (candidates == null)
            return null;
        Object best = null;
        double bestWeight = 0, total = 0;
        for (Map.Entry<Object, Double> e : candidates.entrySet()) {
            total += e.getValue();
            if (e.getValue() > bestWeight) {
                bestWeight = e.getValue();
                best = e.getKey();
            }
        }
        return bestWeight >= 2 && bestWeight >= 0.75 * total ? best : null;
    }

    private static Object currentMatch(Object o) {
        if (o instanceof Cls c)
            return c.match;
        if (o instanceof Fld f)
            return f.match;
        return ((Mth) o).match;
    }

    private void vote(Object v, Object c, double weight) {
        if (v == null || c == null || v.getClass() != c.getClass())
            return;
        forward.computeIfAbsent(v, k -> new HashMap<>()).merge(c, weight, Double::sum);
        backward.computeIfAbsent(c, k -> new HashMap<>()).merge(v, weight, Double::sum);
    }

    // ------------------------------------------------------------ references

    private void voteInsn(AbstractInsnNode v, AbstractInsnNode c, double w) {
        switch (v) {
            case TypeInsnNode tv when c instanceof TypeInsnNode tc -> voteType(internalType(tv.desc), internalType(tc.desc), w);
            case LdcInsnNode lv when c instanceof LdcInsnNode lc && lv.cst instanceof Type a && lc.cst instanceof Type b ->
                    voteType(a, b, w);
            case MultiANewArrayInsnNode av when c instanceof MultiANewArrayInsnNode ac ->
                    voteType(Type.getType(av.desc), Type.getType(ac.desc), w);
            case FieldInsnNode fv when c instanceof FieldInsnNode fc -> {
                voteType(Type.getObjectType(fv.owner), Type.getObjectType(fc.owner), w);
                voteType(Type.getType(fv.desc), Type.getType(fc.desc), w);
                vote(resolveField(links.vanilla, fv), resolveField(links.cb, fc), w);
            }
            case MethodInsnNode mv when c instanceof MethodInsnNode mc -> {
                voteType(internalType(mv.owner), internalType(mc.owner), w);
                voteMethodDesc(mv.desc, mc.desc, w);
                vote(resolveMethod(links.vanilla, mv.owner, mv.name, mv.desc),
                        resolveMethod(links.cb, mc.owner, mc.name, mc.desc), w);
            }
            default -> {}
        }
    }

    private static Type internalType(String internal) {
        return internal.startsWith("[") ? Type.getType(internal) : Type.getObjectType(internal);
    }

    private void voteMethodDesc(String v, String c, double w) {
        Type[] a = Type.getArgumentTypes(v), b = Type.getArgumentTypes(c);
        if (a.length != b.length)
            return;
        for (int i = 0; i < a.length; i++)
            voteType(a[i], b[i], w);
        voteType(Type.getReturnType(v), Type.getReturnType(c), w);
    }

    private void voteType(Type v, Type c, double w) {
        if (v.getSort() == Type.ARRAY && c.getSort() == Type.ARRAY && v.getDimensions() == c.getDimensions())
            voteType(v.getElementType(), c.getElementType(), w);
        else if (v.getSort() == Type.OBJECT && c.getSort() == Type.OBJECT)
            vote(links.vanilla.get(v.getInternalName()), links.cb.get(c.getInternalName()), w);
    }

    static Fld resolveField(Map<String, Cls> side, FieldInsnNode insn) {
        Deque<Cls> queue = new ArrayDeque<>();
        Cls start = side.get(insn.owner);
        if (start != null)
            queue.add(start);
        Set<Cls> seen = new HashSet<>();
        while (!queue.isEmpty()) {
            Cls c = queue.poll();
            if (!seen.add(c))
                continue;
            Fld f = c.fields.get(insn.name + ":" + insn.desc);
            if (f != null)
                return f;
            queue.addAll(c.interfaces);
            if (c.superCls != null)
                queue.add(c.superCls);
        }
        return null;
    }

    static Mth resolveMethod(Map<String, Cls> side, String owner, String name, String desc) {
        if (owner.startsWith("["))
            return null;
        Deque<Cls> queue = new ArrayDeque<>();
        Cls start = side.get(owner);
        if (start != null)
            queue.add(start);
        Set<Cls> seen = new HashSet<>();
        while (!queue.isEmpty()) {
            Cls c = queue.poll();
            if (!seen.add(c))
                continue;
            Mth m = c.methods.get(name + desc);
            if (m != null)
                return m;
            if (c.superCls != null)
                queue.add(c.superCls);
            queue.addAll(c.interfaces);
        }
        return null;
    }

    // -------------------------------------------------------------- alignment

    /** Index pairs of equal tokens: common prefix and suffix, plus an LCS of the middle when it is small enough. */
    static int[][] align(List<String> a, List<String> b) {
        int n = a.size(), m = b.size();
        int prefix = 0;
        while (prefix < n && prefix < m && a.get(prefix).equals(b.get(prefix)))
            prefix++;
        int suffix = 0;
        while (suffix < n - prefix && suffix < m - prefix && a.get(n - 1 - suffix).equals(b.get(m - 1 - suffix)))
            suffix++;
        List<int[]> pairs = new ArrayList<>();
        for (int i = 0; i < prefix; i++)
            pairs.add(new int[]{i, i});
        int an = n - prefix - suffix, bn = m - prefix - suffix;
        if (an > 0 && bn > 0 && (long) an * bn <= MAX_DP_CELLS)
            lcs(a, b, prefix, an, bn, pairs);
        for (int i = 0; i < suffix; i++)
            pairs.add(new int[]{n - suffix + i, m - suffix + i});
        return pairs.toArray(new int[0][]);
    }

    static int lcsLength(List<String> a, List<String> b) {
        return align(a, b).length;
    }

    private static void lcs(List<String> a, List<String> b, int offset, int an, int bn, List<int[]> out) {
        char[] dp = new char[(an + 1) * (bn + 1)];
        int w = bn + 1;
        for (int i = an - 1; i >= 0; i--)
            for (int j = bn - 1; j >= 0; j--)
                dp[i * w + j] = a.get(offset + i).equals(b.get(offset + j))
                        ? (char) (dp[(i + 1) * w + j + 1] + 1)
                        : (char) Math.max(dp[(i + 1) * w + j], dp[i * w + j + 1]);
        int i = 0, j = 0;
        while (i < an && j < bn) {
            if (a.get(offset + i).equals(b.get(offset + j))) {
                out.add(new int[]{offset + i, offset + j});
                i++;
                j++;
            } else if (dp[(i + 1) * w + j] >= dp[i * w + j + 1]) {
                i++;
            } else {
                j++;
            }
        }
    }
}
