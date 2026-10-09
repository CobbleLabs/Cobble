import org.objectweb.asm.ClassReader;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;
import org.objectweb.asm.tree.*;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Path;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

final class Loader {
    private Loader() {}

    static Map<String, Cls> load(Path jar) throws IOException {
        Map<String, Cls> classes = new HashMap<>();
        try (ZipFile zip = new ZipFile(jar.toFile())) {
            for (Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements(); ) {
                ZipEntry entry = e.nextElement();
                if (!entry.getName().endsWith(".class"))
                    continue;
                try (InputStream in = zip.getInputStream(entry)) {
                    ClassNode node = new ClassNode();
                    new ClassReader(in.readAllBytes()).accept(node, ClassReader.SKIP_FRAMES);
                    classes.put(node.name, new Cls(node));
                }
            }
        }
        for (Cls c : classes.values()) {
            c.superCls = classes.get(c.node.superName);
            if (c.superCls != null)
                c.superCls.children.add(c);
            for (String itf : c.node.interfaces) {
                Cls i = classes.get(itf);
                if (i != null)
                    c.interfaces.add(i);
            }
            for (FieldNode f : c.node.fields)
                c.fields.put(f.name + ":" + f.desc, new Fld(c, f));
            for (MethodNode m : c.node.methods)
                c.methods.put(m.name + m.desc, new Mth(c, m));
        }
        return classes;
    }

    /** CheatBreaker moves string literals into static String[] pools filled in <clinit>. */
    static Map<String, String[]> findStringPools(Map<String, Cls> classes) {
        Map<String, String[]> pools = new HashMap<>();
        for (Cls c : classes.values()) {
            Mth clinit = c.methods.get("<clinit>()V");
            if (clinit == null)
                continue;
            for (AbstractInsnNode insn = clinit.node.instructions.getFirst(); insn != null; insn = insn.getNext()) {
                if (insn.getOpcode() != Opcodes.ANEWARRAY || !((TypeInsnNode) insn).desc.equals("java/lang/String"))
                    continue;
                Integer size = intValue(previousReal(insn));
                if (size == null || size <= 0)
                    continue;
                String[] values = new String[size];
                AbstractInsnNode cur = nextReal(insn);
                while (cur != null && cur.getOpcode() == Opcodes.DUP) {
                    AbstractInsnNode indexInsn = nextReal(cur);
                    AbstractInsnNode ldc = nextReal(indexInsn);
                    AbstractInsnNode store = nextReal(ldc);
                    Integer index = intValue(indexInsn);
                    if (index == null || index < 0 || index >= size || !(ldc instanceof LdcInsnNode l)
                            || !(l.cst instanceof String s) || store == null || store.getOpcode() != Opcodes.AASTORE)
                        break;
                    values[index] = s;
                    cur = nextReal(store);
                }
                if (cur instanceof FieldInsnNode put && put.getOpcode() == Opcodes.PUTSTATIC && put.owner.equals(c.name)
                        && Arrays.stream(values).noneMatch(Objects::isNull))
                    pools.put(put.owner + "." + put.name, values);
            }
        }
        return pools;
    }

    /** Rewrites `GETSTATIC pool; <int constant expression>; AALOAD` into the literal it loads. */
    static int inlineStringPools(Map<String, Cls> classes, Map<String, String[]> pools) {
        int inlined = 0;
        for (Cls c : classes.values()) {
            for (Mth m : c.methods.values()) {
                InsnList list = m.node.instructions;
                AbstractInsnNode insn = list.getFirst();
                while (insn != null) {
                    AbstractInsnNode next = insn.getNext();
                    if (insn instanceof FieldInsnNode get && get.getOpcode() == Opcodes.GETSTATIC) {
                        String[] pool = pools.get(get.owner + "." + get.name);
                        PoolLoad load = pool == null ? null : evaluatePoolLoad(get, pool);
                        if (load != null) {
                            AbstractInsnNode after = load.aaload.getNext();
                            LdcInsnNode literal = new LdcInsnNode(pool[load.index]);
                            list.insert(load.aaload, literal);
                            for (AbstractInsnNode cur = get; cur != literal; ) {
                                AbstractInsnNode n = cur.getNext();
                                if (cur.getOpcode() >= 0)
                                    list.remove(cur);
                                cur = n;
                            }
                            inlined++;
                            next = after;
                        }
                    }
                    insn = next;
                }
            }
        }
        return inlined;
    }

    private record PoolLoad(AbstractInsnNode aaload, int index) {}

    private static PoolLoad evaluatePoolLoad(AbstractInsnNode get, String[] pool) {
        Deque<Integer> stack = new ArrayDeque<>();
        for (AbstractInsnNode cur = nextReal(get); cur != null; cur = nextReal(cur)) {
            Integer constant = intValue(cur);
            if (constant != null) {
                stack.push(constant);
                continue;
            }
            int op = cur.getOpcode();
            if (op == Opcodes.AALOAD) {
                if (stack.size() != 1)
                    return null;
                int index = stack.pop();
                return index >= 0 && index < pool.length ? new PoolLoad(cur, index) : null;
            }
            if (op == Opcodes.INEG && !stack.isEmpty()) {
                stack.push(-stack.pop());
                continue;
            }
            if (stack.size() < 2)
                return null;
            int b = stack.pop(), a = stack.pop();
            switch (op) {
                case Opcodes.IAND -> stack.push(a & b);
                case Opcodes.IOR -> stack.push(a | b);
                case Opcodes.IXOR -> stack.push(a ^ b);
                case Opcodes.IADD -> stack.push(a + b);
                case Opcodes.ISUB -> stack.push(a - b);
                case Opcodes.IMUL -> stack.push(a * b);
                case Opcodes.ISHL -> stack.push(a << b);
                case Opcodes.ISHR -> stack.push(a >> b);
                case Opcodes.IUSHR -> stack.push(a >>> b);
                default -> {
                    return null;
                }
            }
        }
        return null;
    }

    static AbstractInsnNode nextReal(AbstractInsnNode insn) {
        if (insn == null)
            return null;
        do {
            insn = insn.getNext();
        } while (insn != null && insn.getOpcode() < 0);
        return insn;
    }

    static AbstractInsnNode previousReal(AbstractInsnNode insn) {
        do {
            insn = insn.getPrevious();
        } while (insn != null && insn.getOpcode() < 0);
        return insn;
    }

    static Integer intValue(AbstractInsnNode insn) {
        if (insn == null)
            return null;
        int op = insn.getOpcode();
        if (op >= Opcodes.ICONST_M1 && op <= Opcodes.ICONST_5)
            return op - Opcodes.ICONST_0;
        if (op == Opcodes.BIPUSH || op == Opcodes.SIPUSH)
            return ((IntInsnNode) insn).operand;
        if (insn instanceof LdcInsnNode l && l.cst instanceof Integer i)
            return i;
        return null;
    }

    /** Fills per-method code/tokens/strings and per-class strings/externals. Game types are abstracted to "?". */
    static void analyze(Map<String, Cls> classes) {
        for (Cls c : classes.values()) {
            c.strings.clear();
            c.externals.clear();
            for (Mth m : c.methods.values()) {
                m.code.clear();
                m.tokens.clear();
                m.strings.clear();
                for (AbstractInsnNode insn = m.node.instructions.getFirst(); insn != null; insn = insn.getNext()) {
                    if (insn.getOpcode() < 0)
                        continue;
                    m.code.add(insn);
                    m.tokens.add(token(insn, classes, c, m));
                }
                c.strings.addAll(m.strings);
            }
            for (FieldNode f : c.node.fields)
                if (f.value instanceof String s)
                    c.strings.add(s);
            if (!classes.containsKey(c.node.superName))
                c.externals.add("super " + c.node.superName);
            for (String itf : c.node.interfaces)
                if (!classes.containsKey(itf))
                    c.externals.add("implements " + itf);
        }
    }

    private static String token(AbstractInsnNode insn, Map<String, Cls> classes, Cls c, Mth m) {
        StringBuilder sb = new StringBuilder().append(insn.getOpcode());
        switch (insn) {
            case IntInsnNode i -> sb.append(' ').append(i.operand);
            case VarInsnNode v -> sb.append(' ').append(v.var);
            case IincInsnNode i -> sb.append(' ').append(i.var).append(',').append(i.incr);
            case LdcInsnNode l -> {
                if (l.cst instanceof String s) {
                    m.strings.add(s);
                    sb.append(" s:").append(s);
                } else if (l.cst instanceof Type t) {
                    sb.append(" t:").append(abstractDesc(t.getDescriptor(), classes));
                } else {
                    sb.append(' ').append(l.cst);
                }
            }
            case TypeInsnNode t -> sb.append(' ').append(abstractInternal(t.desc, classes));
            case MultiANewArrayInsnNode a -> sb.append(' ').append(abstractDesc(a.desc, classes)).append(a.dims);
            case FieldInsnNode f -> {
                if (classes.containsKey(f.owner)) {
                    sb.append(" ?.").append(abstractDesc(f.desc, classes));
                } else {
                    String ref = f.owner + "." + f.name + ":" + f.desc;
                    c.externals.add(ref);
                    sb.append(' ').append(ref);
                }
            }
            case MethodInsnNode mi -> {
                if (classes.containsKey(mi.owner) && !Names.isReal(mi.name)) {
                    sb.append(" ?.").append(abstractDesc(mi.desc, classes));
                } else {
                    String ref = abstractInternal(mi.owner, classes) + "." + mi.name + abstractDesc(mi.desc, classes);
                    if (!classes.containsKey(mi.owner))
                        c.externals.add(ref);
                    sb.append(' ').append(ref);
                }
            }
            case InvokeDynamicInsnNode d -> sb.append(' ').append(d.name).append(abstractDesc(d.desc, classes));
            case TableSwitchInsnNode t -> sb.append(' ').append(t.min).append('-').append(t.max);
            case LookupSwitchInsnNode l -> sb.append(' ').append(l.keys);
            default -> {}
        }
        return sb.toString();
    }

    static String abstractInternal(String internal, Map<String, Cls> classes) {
        if (internal.startsWith("["))
            return abstractDesc(internal, classes);
        return classes.containsKey(internal) ? "?" : internal;
    }

    static String abstractDesc(String desc, Map<String, Cls> classes) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < desc.length(); ) {
            char ch = desc.charAt(i);
            if (ch != 'L') {
                out.append(ch);
                i++;
                continue;
            }
            int end = desc.indexOf(';', i);
            String name = desc.substring(i + 1, end);
            if (classes.containsKey(name))
                out.append("L?;");
            else
                out.append(desc, i, end + 1);
            i = end + 1;
        }
        return out.toString();
    }
}
