import org.objectweb.asm.Type;

import java.util.Map;

/** Match bookkeeping shared by every matching pass. */
final class Links {
    final Map<String, Cls> vanilla;
    final Map<String, Cls> cb;
    int classes, fields, methods;

    Links(Map<String, Cls> vanilla, Map<String, Cls> cb) {
        this.vanilla = vanilla;
        this.cb = cb;
    }

    int progress() { return classes + fields + methods; }

    boolean link(Cls v, Cls c) {
        if (v.match != null || c.match != null || v.isInterface() != c.isInterface() || v.isEnum() != c.isEnum())
            return false;
        v.match = c;
        c.match = v;
        classes++;
        return true;
    }

    boolean link(Fld v, Fld c) {
        if (v.match != null || c.match != null || v.isStatic() != c.isStatic() || v.owner.match != c.owner)
            return false;
        if (!compatible(Type.getType(v.node.desc), Type.getType(c.node.desc)))
            return false;
        v.match = c;
        c.match = v;
        fields++;
        return true;
    }

    boolean link(Mth v, Mth c) {
        if (v.match != null || c.match != null || v.isStatic() != c.isStatic() || v.owner.match != c.owner)
            return false;
        if (Names.isReal(v.node.name) != Names.isReal(c.node.name)
                || (Names.isReal(v.node.name) && !v.node.name.equals(c.node.name)))
            return false;
        if (!compatibleMethodDesc(v.node.desc, c.node.desc))
            return false;
        v.match = c;
        c.match = v;
        methods++;
        return true;
    }

    /** Descriptor with every game class replaced by its vanilla name when matched, "?" otherwise. */
    String mapDesc(String desc, boolean isVanilla) {
        Map<String, Cls> side = isVanilla ? vanilla : cb;
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < desc.length(); ) {
            char ch = desc.charAt(i);
            if (ch != 'L') {
                out.append(ch);
                i++;
                continue;
            }
            int end = desc.indexOf(';', i);
            Cls c = side.get(desc.substring(i + 1, end));
            if (c == null)
                out.append(desc, i, end + 1);
            else if (isVanilla)
                out.append('L').append(c.match == null ? "?" : c.name).append(';');
            else
                out.append('L').append(c.match == null ? "?" : c.match.name).append(';');
            i = end + 1;
        }
        return out.toString();
    }

    boolean compatibleMethodDesc(String v, String c) {
        Type[] a = Type.getArgumentTypes(v), b = Type.getArgumentTypes(c);
        if (a.length != b.length || !compatible(Type.getReturnType(v), Type.getReturnType(c)))
            return false;
        for (int i = 0; i < a.length; i++)
            if (!compatible(a[i], b[i]))
                return false;
        return true;
    }

    /** Same shape, same library types, and game types either matched to each other or both unmatched. */
    boolean compatible(Type v, Type c) {
        if (v.getSort() != c.getSort())
            return false;
        if (v.getSort() == Type.ARRAY)
            return v.getDimensions() == c.getDimensions() && compatible(v.getElementType(), c.getElementType());
        if (v.getSort() != Type.OBJECT)
            return true;
        Cls cv = vanilla.get(v.getInternalName()), cc = cb.get(c.getInternalName());
        if (cv == null || cc == null)
            return cv == null && cc == null && v.getInternalName().equals(c.getInternalName());
        if (cv.match != null || cc.match != null)
            return cv.match == cc;
        return cv.isInterface() == cc.isInterface() && cv.isEnum() == cc.isEnum();
    }
}
