import java.util.*;
import java.util.function.Function;

/** Seeds and structural passes. Voting and similarity live in Voting and Similarity. */
final class Matcher {
    private final Links links;
    private final Voting voting;
    private final Similarity similarity;

    Matcher(Links links) {
        this.links = links;
        this.voting = new Voting(links);
        this.similarity = new Similarity(links);
    }

    void run() {
        seedByName();
        seedByFingerprint();
        log("seed");
        for (int round = 1; ; round++) {
            int before = links.progress();
            propagate();
            similarity.matchClasses();
            propagate();
            similarity.matchMethods();
            similarity.matchFields();
            log("round " + round);
            if (links.progress() == before)
                break;
        }
    }

    List<String> conflicts() { return voting.conflicts; }

    private void propagate() {
        while (true) {
            int before = links.progress();
            for (Cls v : links.vanilla.values())
                if (v.match != null)
                    matchMembers(v, v.match);
            voting.run();
            if (links.progress() == before)
                return;
        }
    }

    private void log(String stage) {
        System.out.printf("%-9s classes=%d/%d fields=%d methods=%d%n", stage, links.classes, links.vanilla.size(),
                links.fields, links.methods);
    }

    // ------------------------------------------------------------------ seeds

    private void seedByName() {
        for (Cls v : links.vanilla.values()) {
            Cls c = links.cb.get(v.name);
            if (c != null)
                links.link(v, c);
        }
    }

    /** Classes the CheatBreaker build left untouched are identical once game types are abstracted. */
    private void seedByFingerprint() {
        Map<String, List<Cls>> left = groupBy(links.vanilla.values(), c -> fingerprint(c, links.vanilla));
        Map<String, List<Cls>> right = groupBy(links.cb.values(), c -> fingerprint(c, links.cb));
        for (Map.Entry<String, List<Cls>> e : left.entrySet()) {
            List<Cls> r = right.get(e.getKey());
            if (e.getValue().size() == 1 && r != null && r.size() == 1)
                links.link(e.getValue().get(0), r.get(0));
        }
    }

    private static String fingerprint(Cls c, Map<String, Cls> side) {
        List<String> parts = new ArrayList<>();
        parts.add("access " + c.node.access);
        parts.add("super " + (c.superCls == null ? c.node.superName : "?"));
        parts.add("interfaces " + c.node.interfaces.size());
        List<String> ext = new ArrayList<>(c.externals);
        Collections.sort(ext);
        parts.addAll(ext);
        List<String> members = new ArrayList<>();
        for (Fld f : c.fields.values())
            members.add("F " + f.node.access + " " + Loader.abstractDesc(f.node.desc, side) + " " + f.node.value);
        for (Mth m : c.methods.values())
            members.add("M " + m.node.access + " " + (Names.isReal(m.node.name) ? m.node.name : "")
                    + Loader.abstractDesc(m.node.desc, side) + " " + m.tokens.hashCode() + "/" + m.tokens.size());
        Collections.sort(members);
        parts.addAll(members);
        return String.join("\n", parts);
    }

    private static <T> Map<String, List<T>> groupBy(Collection<T> items, Function<T, String> key) {
        Map<String, List<T>> out = new HashMap<>();
        for (T item : items)
            out.computeIfAbsent(key.apply(item), k -> new ArrayList<>()).add(item);
        return out;
    }

    // ----------------------------------------------------- structural members

    private void matchMembers(Cls v, Cls c) {
        matchOverrides(v, c);
        matchUnique(v.methods.values(), c.methods.values(), m -> methodKey(m, true, true), m -> methodKey(m, false, true));
        matchUnique(v.methods.values(), c.methods.values(), m -> methodKey(m, true, false), m -> methodKey(m, false, false));
        matchUnique(v.fields.values(), c.fields.values(), f -> fieldKey(f, true), f -> fieldKey(f, false));
        if (v.superCls != null && c.superCls != null)
            links.link(v.superCls, c.superCls);
        // Interface order is not preserved, so only a lone interface pairs up positionally.
        if (v.interfaces.size() == 1 && c.interfaces.size() == 1)
            links.link(v.interfaces.get(0), c.interfaces.get(0));
    }

    private String methodKey(Mth m, boolean isVanilla, boolean withBody) {
        if (m.match != null)
            return null;
        String key = (m.isStatic() ? "S " : "I ") + (Names.isReal(m.node.name) ? m.node.name : "")
                + links.mapDesc(m.node.desc, isVanilla);
        return withBody ? key + " #" + m.tokens.hashCode() + "/" + m.tokens.size() : key;
    }

    private String fieldKey(Fld f, boolean isVanilla) {
        return f.match != null ? null : (f.isStatic() ? "S " : "I ") + links.mapDesc(f.node.desc, isVanilla);
    }

    /** Pairs members whose key is unique on both sides. Keys with an unmatched game type ("L?;") are ambiguous. */
    private <T> void matchUnique(Collection<T> left, Collection<T> right, Function<T, String> keyV, Function<T, String> keyC) {
        Map<String, List<T>> l = new HashMap<>(), r = new HashMap<>();
        for (T t : left) {
            String k = keyV.apply(t);
            if (k != null)
                l.computeIfAbsent(k, x -> new ArrayList<>()).add(t);
        }
        for (T t : right) {
            String k = keyC.apply(t);
            if (k != null)
                r.computeIfAbsent(k, x -> new ArrayList<>()).add(t);
        }
        for (Map.Entry<String, List<T>> e : l.entrySet()) {
            List<T> other = r.get(e.getKey());
            if (e.getValue().size() != 1 || other == null || other.size() != 1)
                continue;
            if (e.getKey().contains("L?;") && !e.getKey().contains(" #"))
                continue;
            T a = e.getValue().get(0), b = other.get(0);
            if (a instanceof Mth ma)
                links.link(ma, (Mth) b);
            else
                links.link((Fld) a, (Fld) b);
        }
    }

    /** An override keeps the name of the method it overrides, so a matched ancestor method fixes the pair. */
    private void matchOverrides(Cls v, Cls c) {
        for (Mth m : v.methods.values()) {
            if (m.match != null || m.isStatic() || Names.isReal(m.node.name))
                continue;
            Mth ancestor = findInAncestors(v, m.node.name, m.node.desc);
            if (ancestor == null || ancestor.match == null)
                continue;
            for (Mth candidate : c.methods.values())
                if (candidate.match == null && candidate.node.name.equals(ancestor.match.node.name)
                        && links.compatibleMethodDesc(m.node.desc, candidate.node.desc)) {
                    links.link(m, candidate);
                    break;
                }
        }
    }

    static Mth findInAncestors(Cls c, String name, String desc) {
        Deque<Cls> queue = new ArrayDeque<>();
        if (c.superCls != null)
            queue.add(c.superCls);
        queue.addAll(c.interfaces);
        Set<Cls> seen = new HashSet<>();
        while (!queue.isEmpty()) {
            Cls cur = queue.poll();
            if (!seen.add(cur))
                continue;
            Mth m = cur.methods.get(name + desc);
            if (m != null && !m.isStatic())
                return m;
            if (cur.superCls != null)
                queue.add(cur.superCls);
            queue.addAll(cur.interfaces);
        }
        return null;
    }
}
