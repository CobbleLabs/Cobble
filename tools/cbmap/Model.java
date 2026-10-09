import org.objectweb.asm.Opcodes;
import org.objectweb.asm.tree.AbstractInsnNode;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.FieldNode;
import org.objectweb.asm.tree.MethodNode;

import java.util.*;

final class Cls {
    final String name;
    final ClassNode node;
    final Map<String, Fld> fields = new LinkedHashMap<>();
    final Map<String, Mth> methods = new LinkedHashMap<>();
    final Set<String> strings = new HashSet<>();
    final Set<String> externals = new HashSet<>();
    final List<Cls> children = new ArrayList<>();
    Cls superCls;
    final List<Cls> interfaces = new ArrayList<>();
    Cls match;

    Cls(ClassNode node) {
        this.name = node.name;
        this.node = node;
    }

    boolean isInterface() { return (node.access & Opcodes.ACC_INTERFACE) != 0; }

    boolean isEnum() { return (node.access & Opcodes.ACC_ENUM) != 0; }

    public String toString() { return name; }
}

final class Fld {
    final Cls owner;
    final FieldNode node;
    Fld match;

    Fld(Cls owner, FieldNode node) {
        this.owner = owner;
        this.node = node;
    }

    boolean isStatic() { return (node.access & Opcodes.ACC_STATIC) != 0; }

    public String toString() { return owner.name + "." + node.name + ":" + node.desc; }
}

final class Mth {
    final Cls owner;
    final MethodNode node;
    final List<AbstractInsnNode> code = new ArrayList<>();
    final List<String> tokens = new ArrayList<>();
    final Set<String> strings = new HashSet<>();
    Mth match;

    Mth(Cls owner, MethodNode node) {
        this.owner = owner;
        this.node = node;
    }

    boolean isStatic() { return (node.access & Opcodes.ACC_STATIC) != 0; }

    public String toString() { return owner.name + "." + node.name + node.desc; }
}

final class Names {
    private Names() {}

    /**
     * Names that survive both obfuscators: constructors and overrides of library methods
     * (toString, run, values...). ProGuard members are at most two letters; CheatBreaker uses long I/l runs.
     */
    static boolean isReal(String name) {
        return name.startsWith("<") || (name.length() >= 3 && !name.matches("[Il]{5,}"));
    }
}
