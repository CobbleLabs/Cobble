#include "packet_interceptor.hpp"
#include "../util/logger.hpp"
#include "../sdk/minecraft.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>

// ============================================================================
// PacketInterceptor Implementation
//
// This uses JVMTI to intercept sendPacket calls. The approach:
// 1. Cache classes and methods for C03PacketPlayer detection
// 2. Bytecode-patch NetworkManager.sendPacket to call a native gate
// 3. Queue movement packets, let everything else through
// ============================================================================

namespace {

constexpr const char* kTargetClassSlash = "net/minecraft/network/NetworkManager";
constexpr const char* kTargetClassDot = "net.minecraft.network.NetworkManager";
constexpr const char* kHookClassSlash = "com/cobble/agent/PacketHook";
constexpr const char* kHookClassDot = "com.cobble.agent.PacketHook";
constexpr const char* kHookMethodName = "shouldSend";
constexpr const char* kHookMethodDesc = "(Ljava/lang/Object;)Z";
constexpr int kPrefixLen = 8;

static const unsigned char kPacketHookClassBytes[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x34, 0x00, 0x0c, 0x01, 0x00, 0x1b, 0x63, 0x6f, 0x6d,
    0x2f, 0x63, 0x6f, 0x62, 0x62, 0x6c, 0x65, 0x2f, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x2f, 0x50, 0x61,
    0x63, 0x6b, 0x65, 0x74, 0x48, 0x6f, 0x6f, 0x6b, 0x07, 0x00, 0x01, 0x01, 0x00, 0x10, 0x6a, 0x61,
    0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x07, 0x00,
    0x03, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e, 0x69, 0x74, 0x3e, 0x01, 0x00, 0x03, 0x28, 0x29, 0x56,
    0x0c, 0x00, 0x05, 0x00, 0x06, 0x0a, 0x00, 0x04, 0x00, 0x07, 0x01, 0x00, 0x04, 0x43, 0x6f, 0x64,
    0x65, 0x01, 0x00, 0x0a, 0x73, 0x68, 0x6f, 0x75, 0x6c, 0x64, 0x53, 0x65, 0x6e, 0x64, 0x01, 0x00,
    0x15, 0x28, 0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a,
    0x65, 0x63, 0x74, 0x3b, 0x29, 0x5a, 0x00, 0x31, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x02, 0x00, 0x05, 0x00, 0x06, 0x00, 0x01, 0x00, 0x09, 0x00, 0x00, 0x00, 0x11,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x2a, 0xb7, 0x00, 0x08, 0xb1, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x09, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00
};

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos;
    bool ok;

    Reader(const uint8_t* src, size_t len) : data(src), size(len), pos(0), ok(true) {}

    uint8_t ReadU1() {
        if (pos + 1 > size) {
            ok = false;
            return 0;
        }
        return data[pos++];
    }

    uint16_t ReadU2() {
        if (pos + 2 > size) {
            ok = false;
            return 0;
        }
        uint16_t v = (data[pos] << 8) | data[pos + 1];
        pos += 2;
        return v;
    }

    uint32_t ReadU4() {
        if (pos + 4 > size) {
            ok = false;
            return 0;
        }
        uint32_t v = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
        pos += 4;
        return v;
    }

    const uint8_t* ReadBytes(size_t count) {
        if (pos + count > size) {
            ok = false;
            return nullptr;
        }
        const uint8_t* ptr = data + pos;
        pos += count;
        return ptr;
    }

    Reader Slice(size_t count) {
        if (pos + count > size) {
            ok = false;
            return Reader(nullptr, 0);
        }
        Reader r(data + pos, count);
        pos += count;
        return r;
    }
};

struct Writer {
    std::vector<uint8_t> out;

    void WriteU1(uint8_t v) {
        out.push_back(v);
    }

    void WriteU2(uint16_t v) {
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void WriteU4(uint32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void WriteBytes(const uint8_t* src, size_t count) {
        if (count == 0) return;
        out.insert(out.end(), src, src + count);
    }
};

struct CpEntry {
    uint8_t tag = 0;
    uint16_t a = 0;
    uint16_t b = 0;
};

struct VerificationTypeInfo {
    uint8_t tag = 0;
    uint16_t extra = 0;
};

struct StackMapFrame {
    enum Kind { SAME, SAME_LOCALS_1_STACK, CHOP, APPEND, FULL } kind = SAME;
    int abs_offset = 0;
    uint8_t chop_k = 0;
    std::vector<VerificationTypeInfo> locals;
    std::vector<VerificationTypeInfo> stack;
};

static bool readVerificationTypeInfo(Reader& r, VerificationTypeInfo& info) {
    info.tag = r.ReadU1();
    if (!r.ok) return false;
    if (info.tag == 7 || info.tag == 8) {
        info.extra = r.ReadU2();
        if (!r.ok) return false;
    } else {
        info.extra = 0;
    }
    return true;
}

static void writeVerificationTypeInfo(Writer& w, const VerificationTypeInfo& info) {
    w.WriteU1(info.tag);
    if (info.tag == 7 || info.tag == 8) {
        w.WriteU2(info.extra);
    }
}

static bool readConstantPool(Reader& r, uint16_t cp_count, std::vector<CpEntry>& cp,
                             std::vector<std::string>& utf8, std::vector<uint8_t>& raw) {
    cp.assign(cp_count, CpEntry());
    utf8.assign(cp_count, std::string());
    Writer cpw;

    for (uint16_t i = 1; i < cp_count; ++i) {
        uint8_t tag = r.ReadU1();
        if (!r.ok) return false;
        cpw.WriteU1(tag);
        cp[i].tag = tag;

        switch (tag) {
            case 1: {
                uint16_t len = r.ReadU2();
                if (!r.ok) return false;
                cpw.WriteU2(len);
                const uint8_t* bytes = r.ReadBytes(len);
                if (!r.ok) return false;
                cpw.WriteBytes(bytes, len);
                utf8[i] = std::string(reinterpret_cast<const char*>(bytes), len);
                break;
            }
            case 3:
            case 4: {
                const uint8_t* bytes = r.ReadBytes(4);
                if (!r.ok) return false;
                cpw.WriteBytes(bytes, 4);
                break;
            }
            case 5:
            case 6: {
                const uint8_t* bytes = r.ReadBytes(8);
                if (!r.ok) return false;
                cpw.WriteBytes(bytes, 8);
                if (i + 1 < cp_count) {
                    cp[i + 1].tag = 0;
                }
                ++i;
                break;
            }
            case 7:
            case 8:
            case 16:
            case 19:
            case 20: {
                uint16_t a = r.ReadU2();
                if (!r.ok) return false;
                cpw.WriteU2(a);
                cp[i].a = a;
                break;
            }
            case 9:
            case 10:
            case 11:
            case 12:
            case 17:
            case 18: {
                uint16_t a = r.ReadU2();
                uint16_t b = r.ReadU2();
                if (!r.ok) return false;
                cpw.WriteU2(a);
                cpw.WriteU2(b);
                cp[i].a = a;
                cp[i].b = b;
                break;
            }
            case 15: {
                uint8_t kind = r.ReadU1();
                uint16_t ref = r.ReadU2();
                if (!r.ok) return false;
                cpw.WriteU1(kind);
                cpw.WriteU2(ref);
                break;
            }
            default:
                return false;
        }
    }

    raw = std::move(cpw.out);
    return true;
}

static bool resolveMethodRef(const std::vector<CpEntry>& cp, const std::vector<std::string>& utf8,
                             uint16_t index, std::string& className, std::string& methodName,
                             std::string& methodDesc) {
    if (index == 0 || index >= cp.size()) return false;
    const CpEntry& ref = cp[index];
    if (ref.tag != 10 && ref.tag != 11) return false;
    if (ref.a == 0 || ref.a >= cp.size()) return false;
    if (ref.b == 0 || ref.b >= cp.size()) return false;

    const CpEntry& cls = cp[ref.a];
    if (cls.tag != 7 || cls.a == 0 || cls.a >= utf8.size()) return false;
    className = utf8[cls.a];

    const CpEntry& nt = cp[ref.b];
    if (nt.tag != 12 || nt.a == 0 || nt.b == 0) return false;
    if (nt.a >= utf8.size() || nt.b >= utf8.size()) return false;
    methodName = utf8[nt.a];
    methodDesc = utf8[nt.b];
    return true;
}

static bool isHookMethodRef(const std::vector<CpEntry>& cp, const std::vector<std::string>& utf8, uint16_t index) {
    std::string cls;
    std::string name;
    std::string desc;
    if (!resolveMethodRef(cp, utf8, index, cls, name, desc)) return false;
    return cls == kHookClassSlash && name == kHookMethodName && desc == kHookMethodDesc;
}

static bool parseStackMapTable(Reader& r, std::vector<StackMapFrame>& frames) {
    uint16_t count = r.ReadU2();
    if (!r.ok) return false;

    int prev_offset = -1;
    frames.clear();
    frames.reserve(count);

    for (uint16_t i = 0; i < count; ++i) {
        uint8_t frame_type = r.ReadU1();
        if (!r.ok) return false;

        StackMapFrame frame;
        uint16_t offset_delta = 0;

        if (frame_type <= 63) {
            frame.kind = StackMapFrame::SAME;
            offset_delta = frame_type;
        } else if (frame_type <= 127) {
            frame.kind = StackMapFrame::SAME_LOCALS_1_STACK;
            offset_delta = frame_type - 64;
            VerificationTypeInfo info;
            if (!readVerificationTypeInfo(r, info)) return false;
            frame.stack.push_back(info);
        } else if (frame_type == 247) {
            frame.kind = StackMapFrame::SAME_LOCALS_1_STACK;
            offset_delta = r.ReadU2();
            VerificationTypeInfo info;
            if (!readVerificationTypeInfo(r, info)) return false;
            frame.stack.push_back(info);
        } else if (frame_type >= 248 && frame_type <= 250) {
            frame.kind = StackMapFrame::CHOP;
            frame.chop_k = static_cast<uint8_t>(251 - frame_type);
            offset_delta = r.ReadU2();
        } else if (frame_type == 251) {
            frame.kind = StackMapFrame::SAME;
            offset_delta = r.ReadU2();
        } else if (frame_type >= 252 && frame_type <= 254) {
            frame.kind = StackMapFrame::APPEND;
            uint8_t locals_to_add = static_cast<uint8_t>(frame_type - 251);
            offset_delta = r.ReadU2();
            for (uint8_t l = 0; l < locals_to_add; ++l) {
                VerificationTypeInfo info;
                if (!readVerificationTypeInfo(r, info)) return false;
                frame.locals.push_back(info);
            }
        } else if (frame_type == 255) {
            frame.kind = StackMapFrame::FULL;
            offset_delta = r.ReadU2();
            uint16_t locals_count = r.ReadU2();
            for (uint16_t l = 0; l < locals_count; ++l) {
                VerificationTypeInfo info;
                if (!readVerificationTypeInfo(r, info)) return false;
                frame.locals.push_back(info);
            }
            uint16_t stack_count = r.ReadU2();
            for (uint16_t s = 0; s < stack_count; ++s) {
                VerificationTypeInfo info;
                if (!readVerificationTypeInfo(r, info)) return false;
                frame.stack.push_back(info);
            }
        } else {
            return false;
        }

        frame.abs_offset = prev_offset + static_cast<int>(offset_delta) + 1;
        prev_offset = frame.abs_offset;
        frames.push_back(std::move(frame));
    }

    return r.ok;
}

static bool patchStackMapTable(const uint8_t* data, size_t len, int shift, std::vector<uint8_t>& out) {
    Reader r(data, len);
    std::vector<StackMapFrame> frames;
    if (!parseStackMapTable(r, frames) || !r.ok) return false;

    bool has_zero = !frames.empty() && frames[0].abs_offset == 0;
    std::vector<StackMapFrame> patched;
    patched.reserve(frames.size() + (has_zero ? 0 : 1));

    if (!has_zero) {
        StackMapFrame entry;
        entry.kind = StackMapFrame::SAME;
        entry.abs_offset = shift;
        patched.push_back(entry);
    }

    for (auto& frame : frames) {
        frame.abs_offset += shift;
        for (auto& info : frame.locals) {
            if (info.tag == 8) {
                info.extra = static_cast<uint16_t>(info.extra + shift);
            }
        }
        for (auto& info : frame.stack) {
            if (info.tag == 8) {
                info.extra = static_cast<uint16_t>(info.extra + shift);
            }
        }
        patched.push_back(frame);
    }

    Writer w;
    w.WriteU2(static_cast<uint16_t>(patched.size()));

    int prev_offset = -1;
    for (const auto& frame : patched) {
        int delta = frame.abs_offset - prev_offset - 1;
        if (delta < 0 || delta > 0xFFFF) return false;
        uint16_t delta_u16 = static_cast<uint16_t>(delta);

        switch (frame.kind) {
            case StackMapFrame::SAME:
                if (delta_u16 <= 63) {
                    w.WriteU1(static_cast<uint8_t>(delta_u16));
                } else {
                    w.WriteU1(251);
                    w.WriteU2(delta_u16);
                }
                break;
            case StackMapFrame::SAME_LOCALS_1_STACK: {
                if (frame.stack.size() != 1) return false;
                if (delta_u16 <= 63) {
                    w.WriteU1(static_cast<uint8_t>(64 + delta_u16));
                } else {
                    w.WriteU1(247);
                    w.WriteU2(delta_u16);
                }
                writeVerificationTypeInfo(w, frame.stack[0]);
                break;
            }
            case StackMapFrame::CHOP:
                if (frame.chop_k == 0 || frame.chop_k > 3) return false;
                w.WriteU1(static_cast<uint8_t>(251 - frame.chop_k));
                w.WriteU2(delta_u16);
                break;
            case StackMapFrame::APPEND:
                if (frame.locals.empty() || frame.locals.size() > 3) return false;
                w.WriteU1(static_cast<uint8_t>(251 + frame.locals.size()));
                w.WriteU2(delta_u16);
                for (const auto& info : frame.locals) {
                    writeVerificationTypeInfo(w, info);
                }
                break;
            case StackMapFrame::FULL:
                w.WriteU1(255);
                w.WriteU2(delta_u16);
                w.WriteU2(static_cast<uint16_t>(frame.locals.size()));
                for (const auto& info : frame.locals) {
                    writeVerificationTypeInfo(w, info);
                }
                w.WriteU2(static_cast<uint16_t>(frame.stack.size()));
                for (const auto& info : frame.stack) {
                    writeVerificationTypeInfo(w, info);
                }
                break;
        }

        prev_offset = frame.abs_offset;
    }

    out = std::move(w.out);
    return true;
}

static bool patchLineNumberTable(const uint8_t* data, size_t len, int shift, std::vector<uint8_t>& out) {
    Reader r(data, len);
    Writer w;
    uint16_t count = r.ReadU2();
    if (!r.ok) return false;
    w.WriteU2(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t start_pc = r.ReadU2();
        uint16_t line = r.ReadU2();
        if (!r.ok) return false;
        w.WriteU2(static_cast<uint16_t>(start_pc + shift));
        w.WriteU2(line);
    }
    out = std::move(w.out);
    return true;
}

static bool patchLocalVariableTable(const uint8_t* data, size_t len, int shift, std::vector<uint8_t>& out) {
    Reader r(data, len);
    Writer w;
    uint16_t count = r.ReadU2();
    if (!r.ok) return false;
    w.WriteU2(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t start_pc = r.ReadU2();
        uint16_t length = r.ReadU2();
        uint16_t name_index = r.ReadU2();
        uint16_t desc_index = r.ReadU2();
        uint16_t slot = r.ReadU2();
        if (!r.ok) return false;
        w.WriteU2(static_cast<uint16_t>(start_pc + shift));
        w.WriteU2(length);
        w.WriteU2(name_index);
        w.WriteU2(desc_index);
        w.WriteU2(slot);
    }
    out = std::move(w.out);
    return true;
}

static bool isAlreadyPatched(const uint8_t* code, size_t len, const std::vector<CpEntry>& cp,
                             const std::vector<std::string>& utf8) {
    if (len < static_cast<size_t>(kPrefixLen)) return false;
    uint8_t aload = code[0];
    if (aload != 0x2a && aload != 0x2b) return false;
    if (code[1] != 0xb8) return false;
    if (code[4] != 0x9a || code[7] != 0xb1) return false;
    uint16_t method_ref = static_cast<uint16_t>((code[2] << 8) | code[3]);
    return isHookMethodRef(cp, utf8, method_ref);
}

static bool tryPatchCodeAttribute(const uint8_t* data, size_t len, const std::vector<CpEntry>& cp,
                                  const std::vector<std::string>& utf8, uint16_t hook_methodref,
                                  bool is_static, std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint16_t max_stack = r.ReadU2();
    uint16_t max_locals = r.ReadU2();
    uint32_t code_len = r.ReadU4();
    const uint8_t* code = r.ReadBytes(code_len);
    if (!r.ok) return false;

    if (isAlreadyPatched(code, code_len, cp, utf8)) {
        return false;
    }

    uint16_t ex_count = r.ReadU2();
    if (!r.ok) return false;

    struct ExEntry {
        uint16_t start_pc;
        uint16_t end_pc;
        uint16_t handler_pc;
        uint16_t catch_type;
    };
    std::vector<ExEntry> exceptions;
    exceptions.reserve(ex_count);
    for (uint16_t i = 0; i < ex_count; ++i) {
        ExEntry entry;
        entry.start_pc = r.ReadU2();
        entry.end_pc = r.ReadU2();
        entry.handler_pc = r.ReadU2();
        entry.catch_type = r.ReadU2();
        if (!r.ok) return false;
        exceptions.push_back(entry);
    }

    uint16_t attrs_count = r.ReadU2();
    if (!r.ok) return false;

    uint8_t aload = is_static ? 0x2a : 0x2b;
    std::vector<uint8_t> new_code;
    new_code.reserve(code_len + kPrefixLen);
    new_code.push_back(aload);
    new_code.push_back(0xb8);
    new_code.push_back(static_cast<uint8_t>((hook_methodref >> 8) & 0xFF));
    new_code.push_back(static_cast<uint8_t>(hook_methodref & 0xFF));
    uint16_t jump = static_cast<uint16_t>(kPrefixLen - 4);
    new_code.push_back(0x9a);
    new_code.push_back(static_cast<uint8_t>((jump >> 8) & 0xFF));
    new_code.push_back(static_cast<uint8_t>(jump & 0xFF));
    new_code.push_back(0xb1);
    new_code.insert(new_code.end(), code, code + code_len);

    Writer w;
    w.WriteU2(std::max<uint16_t>(max_stack, static_cast<uint16_t>(1)));
    w.WriteU2(max_locals);
    w.WriteU4(code_len + kPrefixLen);
    w.WriteBytes(new_code.data(), new_code.size());

    w.WriteU2(ex_count);
    for (const auto& entry : exceptions) {
        w.WriteU2(static_cast<uint16_t>(entry.start_pc + kPrefixLen));
        w.WriteU2(static_cast<uint16_t>(entry.end_pc + kPrefixLen));
        w.WriteU2(static_cast<uint16_t>(entry.handler_pc + kPrefixLen));
        w.WriteU2(entry.catch_type);
    }

    w.WriteU2(attrs_count);
    for (uint16_t i = 0; i < attrs_count; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok) return false;
        Reader sub = r.Slice(attr_len);
        if (!r.ok) return false;
        const uint8_t* attr_data = sub.data;

        const char* attr_name = nullptr;
        if (name_index < utf8.size()) {
            attr_name = utf8[name_index].c_str();
        }

        std::vector<uint8_t> patched;
        bool replaced = false;

        if (attr_name && strcmp(attr_name, "StackMapTable") == 0) {
            if (!patchStackMapTable(attr_data, attr_len, kPrefixLen, patched)) return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LineNumberTable") == 0) {
            if (!patchLineNumberTable(attr_data, attr_len, kPrefixLen, patched)) return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LocalVariableTable") == 0) {
            if (!patchLocalVariableTable(attr_data, attr_len, kPrefixLen, patched)) return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LocalVariableTypeTable") == 0) {
            if (!patchLocalVariableTable(attr_data, attr_len, kPrefixLen, patched)) return false;
            replaced = true;
        }

        w.WriteU2(name_index);
        if (replaced) {
            w.WriteU4(static_cast<uint32_t>(patched.size()));
            w.WriteBytes(patched.data(), patched.size());
        } else {
            w.WriteU4(attr_len);
            w.WriteBytes(attr_data, attr_len);
        }
    }

    if (!r.ok) return false;
    out = std::move(w.out);
    return true;
}

static int patchNetworkManagerClass(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint32_t magic = r.ReadU4();
    if (!r.ok || magic != 0xCAFEBABE) return -1;

    uint16_t minor = r.ReadU2();
    uint16_t major = r.ReadU2();
    uint16_t cp_count = r.ReadU2();
    if (!r.ok || cp_count == 0) return -1;

    std::vector<CpEntry> cp;
    std::vector<std::string> utf8;
    std::vector<uint8_t> cp_raw;
    if (!readConstantPool(r, cp_count, cp, utf8, cp_raw)) return -1;

    if (cp_count > 0xFFF9) return -1;
    uint16_t base = cp_count;
    uint16_t idx_utf8_class = base;
    uint16_t idx_class = static_cast<uint16_t>(base + 1);
    uint16_t idx_utf8_name = static_cast<uint16_t>(base + 2);
    uint16_t idx_utf8_desc = static_cast<uint16_t>(base + 3);
    uint16_t idx_name_type = static_cast<uint16_t>(base + 4);
    uint16_t idx_methodref = static_cast<uint16_t>(base + 5);

    Writer w;
    w.WriteU4(magic);
    w.WriteU2(minor);
    w.WriteU2(major);
    w.WriteU2(static_cast<uint16_t>(cp_count + 6));
    w.WriteBytes(cp_raw.data(), cp_raw.size());

    w.WriteU1(1);
    w.WriteU2(static_cast<uint16_t>(strlen(kHookClassSlash)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kHookClassSlash), strlen(kHookClassSlash));
    w.WriteU1(7);
    w.WriteU2(idx_utf8_class);
    w.WriteU1(1);
    w.WriteU2(static_cast<uint16_t>(strlen(kHookMethodName)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kHookMethodName), strlen(kHookMethodName));
    w.WriteU1(1);
    w.WriteU2(static_cast<uint16_t>(strlen(kHookMethodDesc)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kHookMethodDesc), strlen(kHookMethodDesc));
    w.WriteU1(12);
    w.WriteU2(idx_utf8_name);
    w.WriteU2(idx_utf8_desc);
    w.WriteU1(10);
    w.WriteU2(idx_class);
    w.WriteU2(idx_name_type);

    uint16_t access_flags = r.ReadU2();
    uint16_t this_class = r.ReadU2();
    uint16_t super_class = r.ReadU2();
    if (!r.ok) return -1;
    w.WriteU2(access_flags);
    w.WriteU2(this_class);
    w.WriteU2(super_class);

    uint16_t interfaces_count = r.ReadU2();
    if (!r.ok) return -1;
    w.WriteU2(interfaces_count);
    for (uint16_t i = 0; i < interfaces_count; ++i) {
        uint16_t idx = r.ReadU2();
        if (!r.ok) return -1;
        w.WriteU2(idx);
    }

    uint16_t fields_count = r.ReadU2();
    if (!r.ok) return -1;
    w.WriteU2(fields_count);
    for (uint16_t i = 0; i < fields_count; ++i) {
        uint16_t f_access = r.ReadU2();
        uint16_t f_name = r.ReadU2();
        uint16_t f_desc = r.ReadU2();
        uint16_t f_attrs = r.ReadU2();
        if (!r.ok) return -1;
        w.WriteU2(f_access);
        w.WriteU2(f_name);
        w.WriteU2(f_desc);
        w.WriteU2(f_attrs);
        for (uint16_t a = 0; a < f_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok) return -1;
            const uint8_t* bytes = r.ReadBytes(attr_len);
            if (!r.ok) return -1;
            w.WriteU2(name_index);
            w.WriteU4(attr_len);
            w.WriteBytes(bytes, attr_len);
        }
    }

    uint16_t methods_count = r.ReadU2();
    if (!r.ok) return -1;
    w.WriteU2(methods_count);

    int patched_methods = 0;
    for (uint16_t i = 0; i < methods_count; ++i) {
        uint16_t m_access = r.ReadU2();
        uint16_t m_name = r.ReadU2();
        uint16_t m_desc = r.ReadU2();
        uint16_t m_attrs = r.ReadU2();
        if (!r.ok) return -1;
        w.WriteU2(m_access);
        w.WriteU2(m_name);
        w.WriteU2(m_desc);
        w.WriteU2(m_attrs);

        bool is_target = false;
        if (m_name < utf8.size() && m_desc < utf8.size()) {
            const std::string& name = utf8[m_name];
            const std::string& desc = utf8[m_desc];
            if (name == "sendPacket" && desc.size() >= 3 && desc.back() == 'V' && desc[desc.size() - 2] == ')') {
                if (desc.rfind("(Lnet/minecraft/network/Packet;", 0) == 0) {
                    is_target = true;
                }
            }
        }

        for (uint16_t a = 0; a < m_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok) return -1;
            Reader attr_reader = r.Slice(attr_len);
            if (!r.ok) return -1;
            const uint8_t* attr_data = attr_reader.data;

            const char* attr_name = nullptr;
            if (name_index < utf8.size()) {
                attr_name = utf8[name_index].c_str();
            }

            bool patched = false;
            std::vector<uint8_t> patched_attr;
            if (is_target && attr_name && strcmp(attr_name, "Code") == 0 &&
                (m_access & (0x0100 | 0x0400)) == 0) {
                bool is_static = (m_access & 0x0008) != 0;
                if (tryPatchCodeAttribute(attr_data, attr_len, cp, utf8, idx_methodref, is_static, patched_attr)) {
                    w.WriteU2(name_index);
                    w.WriteU4(static_cast<uint32_t>(patched_attr.size()));
                    w.WriteBytes(patched_attr.data(), patched_attr.size());
                    patched = true;
                    ++patched_methods;
                }
            }

            if (!patched) {
                w.WriteU2(name_index);
                w.WriteU4(attr_len);
                w.WriteBytes(attr_data, attr_len);
            }
        }
    }

    uint16_t class_attrs = r.ReadU2();
    if (!r.ok) return -1;
    w.WriteU2(class_attrs);
    for (uint16_t i = 0; i < class_attrs; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok) return -1;
        const uint8_t* bytes = r.ReadBytes(attr_len);
        if (!r.ok) return -1;
        w.WriteU2(name_index);
        w.WriteU4(attr_len);
        w.WriteBytes(bytes, attr_len);
    }

    if (patched_methods == 0) return 0;
    out = std::move(w.out);
    return patched_methods;
}

static void JNICALL onClassFileLoadHook(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jclass class_being_redefined,
                                        jobject loader, const char* name, jobject protection_domain,
                                        jint class_data_len, const unsigned char* class_data,
                                        jint* new_class_data_len, unsigned char** new_class_data) {
    (void)jni_env;
    (void)class_being_redefined;
    (void)loader;
    (void)protection_domain;

    if (!name || strcmp(name, kTargetClassSlash) != 0) return;

    std::vector<uint8_t> patched;
    int patched_methods = patchNetworkManagerClass(class_data, static_cast<size_t>(class_data_len), patched);
    if (patched_methods <= 0) {
        if (patched_methods < 0) {
            log_warn("[PacketInterceptor] Bytecode patch failed for %s", name);
        }
        return;
    }

    unsigned char* buffer = nullptr;
    jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
    if (err != JVMTI_ERROR_NONE || !buffer) {
        log_warn("[PacketInterceptor] JVMTI Allocate failed for patched class: %d", err);
        return;
    }

    memcpy(buffer, patched.data(), patched.size());
    *new_class_data_len = static_cast<jint>(patched.size());
    *new_class_data = buffer;

    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        log_ok("[PacketInterceptor] Patched NetworkManager.sendPacket bytecode (%d method(s))", patched_methods);
    } else {
        log_debug("[PacketInterceptor] Patched NetworkManager.sendPacket bytecode (%d method(s))", patched_methods);
    }
}

} // namespace

namespace PacketInterceptor {

// State
static std::atomic<bool> initialized{false};
static std::atomic<bool> blinkActive{false};

// Cached JNI references
static jclass cls_NetworkManager = nullptr;
static jclass cls_Packet = nullptr;
static jclass cls_C03PacketPlayer = nullptr;
static jclass cls_C04PacketPlayerPosition = nullptr;
static jclass cls_C05PacketPlayerLook = nullptr;
static jclass cls_C06PacketPlayerPosLook = nullptr;
static jclass cls_PacketHook = nullptr;
static jmethodID mid_sendPacket = nullptr;

static std::atomic<bool> bytecodeHookInstalled{false};

// Packet queue for Blink
static std::vector<jobject> packetQueue;
static std::mutex queueMutex;

// NetworkManager instance cache
static jobject networkManagerInstance = nullptr;

static std::atomic<bool> flushActive{false};
static std::chrono::steady_clock::time_point lastFlushTime{};
constexpr int kFlushIntervalMs = 50;
constexpr int kFlushBatchSize = 1;

// ============================================================================
// Helper: Get NetworkManager instance from player
// ============================================================================
static jobject getNetworkManager(JNIEnv* env) {
    log_debug("[PacketInterceptor] Getting NetworkManager instance...");
    
    // Path: thePlayer.sendQueue.netManager
    jobject player = mc.getPlayer();
    if (!player) {
        log_warn("[PacketInterceptor] No player instance");
        return nullptr;
    }
    
    // Get EntityPlayerSP class
    jclass cls_EntityPlayerSP = nullptr;
    if (!Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", cls_EntityPlayerSP)) {
        log_err("[PacketInterceptor] Failed to get EntityPlayerSP class");
        return nullptr;
    }
    
    // Get sendQueue field
    jfieldID fid_sendQueue = env->GetFieldID(cls_EntityPlayerSP, "sendQueue", 
        "Lnet/minecraft/client/network/NetHandlerPlayClient;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_err("[PacketInterceptor] Failed to get sendQueue field");
        env->DeleteLocalRef(cls_EntityPlayerSP);
        return nullptr;
    }
    
    jobject sendQueue = env->GetObjectField(player, fid_sendQueue);
    env->DeleteLocalRef(cls_EntityPlayerSP);
    
    if (!sendQueue) {
        log_warn("[PacketInterceptor] sendQueue is null");
        return nullptr;
    }
    log_debug("[PacketInterceptor] Got sendQueue: %p", sendQueue);
    
    // Get NetHandlerPlayClient class
    jclass cls_NetHandlerPlayClient = nullptr;
    if (!Java::AssignClass("net.minecraft.client.network.NetHandlerPlayClient", cls_NetHandlerPlayClient)) {
        log_err("[PacketInterceptor] Failed to get NetHandlerPlayClient class");
        env->DeleteLocalRef(sendQueue);
        return nullptr;
    }
    
    // Get netManager field
    jfieldID fid_netManager = env->GetFieldID(cls_NetHandlerPlayClient, "netManager",
        "Lnet/minecraft/network/NetworkManager;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_err("[PacketInterceptor] Failed to get netManager field");
        env->DeleteLocalRef(cls_NetHandlerPlayClient);
        env->DeleteLocalRef(sendQueue);
        return nullptr;
    }
    
    jobject netManager = env->GetObjectField(sendQueue, fid_netManager);
    env->DeleteLocalRef(cls_NetHandlerPlayClient);
    env->DeleteLocalRef(sendQueue);
    
    if (netManager) {
        log_ok("[PacketInterceptor] Got NetworkManager: %p", netManager);
    } else {
        log_warn("[PacketInterceptor] netManager is null");
    }
    
    return netManager;
}

// ============================================================================
// Initialize packet classes cache
// ============================================================================
static bool cachePacketClasses(JNIEnv* env) {
    log_debug("[PacketInterceptor] Caching packet classes...");

    auto makeGlobal = [env](jclass localRef) -> jclass {
        if (!localRef) return nullptr;
        jclass globalRef = (jclass)env->NewGlobalRef(localRef);
        env->DeleteLocalRef(localRef);
        return globalRef;
    };
    
    // NetworkManager
    jclass localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.NetworkManager", localClass)) {
        log_err("[PacketInterceptor] Failed to cache NetworkManager class");
        return false;
    }
    cls_NetworkManager = makeGlobal(localClass);
    if (!cls_NetworkManager) {
        log_err("[PacketInterceptor] Failed to create global ref for NetworkManager");
        return false;
    }
    log_ok("[PacketInterceptor] Cached NetworkManager class");
    
    // Base Packet class
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.Packet", localClass)) {
        log_err("[PacketInterceptor] Failed to cache Packet class");
        return false;
    }
    cls_Packet = makeGlobal(localClass);
    if (!cls_Packet) {
        log_err("[PacketInterceptor] Failed to create global ref for Packet");
        return false;
    }
    log_ok("[PacketInterceptor] Cached Packet class");
    
    // C03PacketPlayer (base for movement)
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", localClass)) {
        log_warn("[PacketInterceptor] Failed to cache C03PacketPlayer - trying alternative names");
        // Try without full package
        cls_C03PacketPlayer = nullptr;
    } else {
        cls_C03PacketPlayer = makeGlobal(localClass);
        log_ok("[PacketInterceptor] Cached C03PacketPlayer class");
    }
    
    // C04PacketPlayerPosition (with position)
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer$C04PacketPlayerPosition", localClass)) {
        // Try alternative naming
        Java::AssignClass("net.minecraft.network.play.client.C04PacketPlayerPosition", localClass);
    }
    if (localClass) {
        cls_C04PacketPlayerPosition = makeGlobal(localClass);
    }
    if (cls_C04PacketPlayerPosition) {
        log_ok("[PacketInterceptor] Cached C04PacketPlayerPosition class");
    }
    
    // C05PacketPlayerLook (with rotation)
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer$C05PacketPlayerLook", localClass)) {
        Java::AssignClass("net.minecraft.network.play.client.C05PacketPlayerLook", localClass);
    }
    if (localClass) {
        cls_C05PacketPlayerLook = makeGlobal(localClass);
    }
    if (cls_C05PacketPlayerLook) {
        log_ok("[PacketInterceptor] Cached C05PacketPlayerLook class");
    }
    
    // C06PacketPlayerPosLook (position + rotation)
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer$C06PacketPlayerPosLook", localClass)) {
        Java::AssignClass("net.minecraft.network.play.client.C06PacketPlayerPosLook", localClass);
    }
    if (localClass) {
        cls_C06PacketPlayerPosLook = makeGlobal(localClass);
    }
    if (cls_C06PacketPlayerPosLook) {
        log_ok("[PacketInterceptor] Cached C06PacketPlayerPosLook class");
    }
    
    // Get sendPacket method
    mid_sendPacket = env->GetMethodID(cls_NetworkManager, "sendPacket",
        "(Lnet/minecraft/network/Packet;)V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_warn("[PacketInterceptor] sendPacket(Packet) not found directly, trying alternatives...");
        
        // List methods to find correct signature
        if (Java::tiEnv) {
            jint methodCount = 0;
            jmethodID* methods = nullptr;
            if (Java::tiEnv->GetClassMethods(cls_NetworkManager, &methodCount, &methods) == JVMTI_ERROR_NONE) {
                for (int i = 0; i < methodCount; i++) {
                    char* name = nullptr;
                    char* sig = nullptr;
                    Java::tiEnv->GetMethodName(methods[i], &name, &sig, nullptr);
                    if (name && strcmp(name, "sendPacket") == 0) {
                        log_debug("[PacketInterceptor] Found sendPacket variant: %s", sig);
                        // Try to get it with this signature
                        mid_sendPacket = env->GetMethodID(cls_NetworkManager, "sendPacket", sig);
                        if (!env->ExceptionCheck() && mid_sendPacket) {
                            log_ok("[PacketInterceptor] Using sendPacket%s", sig);
                            if (name) Java::tiEnv->Deallocate((unsigned char*)name);
                            if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
                            break;
                        }
                        env->ExceptionClear();
                    }
                    if (name) Java::tiEnv->Deallocate((unsigned char*)name);
                    if (sig) Java::tiEnv->Deallocate((unsigned char*)sig);
                }
                Java::tiEnv->Deallocate((unsigned char*)methods);
            }
        }
    } else {
        log_ok("[PacketInterceptor] Cached sendPacket method");
    }
    
    return true;
}

static jboolean JNICALL nativeShouldSend(JNIEnv* env, jclass, jobject packet) {
    return shouldSendPacket(env, packet) ? JNI_TRUE : JNI_FALSE;
}

static bool ensureHookClass(JNIEnv* env) {
    if (cls_PacketHook) return true;

    jclass hook = nullptr;
    if (!Java::AssignClass(kHookClassDot, hook)) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    if (!hook) {
        if (!Java::ClassLoader) {
            log_err("[PacketInterceptor] No classloader available for PacketHook");
            return false;
        }
        hook = env->DefineClass(kHookClassSlash, Java::ClassLoader,
            reinterpret_cast<const jbyte*>(kPacketHookClassBytes),
            static_cast<jint>(sizeof(kPacketHookClassBytes)));
        if (!hook) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            if (Java::AssignClass(kHookClassDot, hook)) {
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            }
            if (!hook) {
                log_err("[PacketInterceptor] Failed to define PacketHook class");
                return false;
            }
        }
    }

    JNINativeMethod method;
    method.name = const_cast<char*>(kHookMethodName);
    method.signature = const_cast<char*>(kHookMethodDesc);
    method.fnPtr = reinterpret_cast<void*>(&nativeShouldSend);

    if (env->RegisterNatives(hook, &method, 1) != 0) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        log_err("[PacketInterceptor] Failed to register PacketHook native");
        env->DeleteLocalRef(hook);
        return false;
    }

    cls_PacketHook = (jclass)env->NewGlobalRef(hook);
    env->DeleteLocalRef(hook);
    log_ok("[PacketInterceptor] PacketHook native bridge ready");
    return true;
}

static bool installBytecodeHook(JNIEnv* env) {
    if (bytecodeHookInstalled.load()) return true;
    if (!Java::tiEnv) return false;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &onClassFileLoadHook;

    jvmtiError err = Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        log_warn("[PacketInterceptor] Failed to set JVMTI class hook callbacks: %d", err);
        return false;
    }

    err = Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    if (err != JVMTI_ERROR_NONE) {
        log_warn("[PacketInterceptor] Failed to enable class hook events: %d", err);
        return false;
    }

    bytecodeHookInstalled = true;

    jclass nm = nullptr;
    if (Java::AssignClass(kTargetClassDot, nm)) {
        jclass classes[1] = { nm };
        err = Java::tiEnv->RetransformClasses(1, classes);
        if (err == JVMTI_ERROR_NONE) {
            log_ok("[PacketInterceptor] Retransform requested for NetworkManager");
        } else {
            log_warn("[PacketInterceptor] RetransformClasses failed: %d", err);
        }
        env->DeleteLocalRef(nm);
    } else {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        log_warn("[PacketInterceptor] NetworkManager class not available for retransform");
    }

    return true;
}

static bool installMethodEntryHook() {
    if (!Java::tiEnv) return false;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.MethodEntry = [](jvmtiEnv* jvmti_env, JNIEnv* jni_env,
                               jthread thread, jmethodID method) {
        if (!blinkActive.load()) return;

        char* methodName = nullptr;
        jvmti_env->GetMethodName(method, &methodName, nullptr, nullptr);

        if (methodName && strcmp(methodName, "sendPacket") == 0) {
            jclass declaringClass = nullptr;
            jvmti_env->GetMethodDeclaringClass(method, &declaringClass);

            if (declaringClass) {
                char* classSig = nullptr;
                jvmti_env->GetClassSignature(declaringClass, &classSig, nullptr);

                if (classSig && strstr(classSig, "NetworkManager")) {
                    jobject packet = nullptr;
                    jvmti_env->GetLocalObject(thread, 0, 1, &packet);

                    if (packet && !shouldSendPacket(jni_env, packet)) {
                        jvmti_env->ForceEarlyReturnVoid(thread);
                        log_debug("[PacketInterceptor] Blocked sendPacket via ForceEarlyReturn");
                    }
                    if (packet) {
                        jni_env->DeleteLocalRef(packet);
                    }
                }
                if (classSig) jvmti_env->Deallocate((unsigned char*)classSig);
                jni_env->DeleteLocalRef(declaringClass);
            }
        }
        if (methodName) jvmti_env->Deallocate((unsigned char*)methodName);
    };

    jvmtiError err = Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        log_warn("[PacketInterceptor] Failed to set JVMTI callbacks: %d", err);
        return false;
    }

    err = Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
    if (err == JVMTI_ERROR_NONE) {
        log_ok("[PacketInterceptor] JVMTI Method Entry hook installed");
        return true;
    }

    log_warn("[PacketInterceptor] Failed to enable method entry events: %d", err);
    return false;
}

// ============================================================================
// Check if packet is a movement packet (C03PacketPlayer or subclass)
// ============================================================================
static bool isMovementPacket(JNIEnv* env, jobject packet) {
    if (!packet) return false;
    
    // Check against cached classes
    if (cls_C03PacketPlayer && env->IsInstanceOf(packet, cls_C03PacketPlayer)) {
        return true;
    }
    if (cls_C04PacketPlayerPosition && env->IsInstanceOf(packet, cls_C04PacketPlayerPosition)) {
        return true;
    }
    if (cls_C05PacketPlayerLook && env->IsInstanceOf(packet, cls_C05PacketPlayerLook)) {
        return true;
    }
    if (cls_C06PacketPlayerPosLook && env->IsInstanceOf(packet, cls_C06PacketPlayerPosLook)) {
        return true;
    }
    
    // Fallback: check class name
    jclass packetClass = env->GetObjectClass(packet);
    if (packetClass) {
        char* sig = nullptr;
        if (Java::tiEnv && Java::tiEnv->GetClassSignature(packetClass, &sig, nullptr) == JVMTI_ERROR_NONE) {
            bool isMovement = (strstr(sig, "C03PacketPlayer") != nullptr) ||
                              (strstr(sig, "C04PacketPlayerPosition") != nullptr) ||
                              (strstr(sig, "C05PacketPlayerLook") != nullptr) ||
                              (strstr(sig, "C06PacketPlayerPosLook") != nullptr);
            Java::tiEnv->Deallocate((unsigned char*)sig);
            env->DeleteLocalRef(packetClass);
            return isMovement;
        }
        env->DeleteLocalRef(packetClass);
    }
    
    return false;
}

// ============================================================================
// Get packet type name for debugging
// ============================================================================
static const char* getPacketTypeName(JNIEnv* env, jobject packet) {
    if (!packet) return "null";
    
    jclass packetClass = env->GetObjectClass(packet);
    if (!packetClass) return "unknown";
    
    static char nameBuf[128];
    nameBuf[0] = '\0';
    
    if (Java::tiEnv) {
        char* sig = nullptr;
        if (Java::tiEnv->GetClassSignature(packetClass, &sig, nullptr) == JVMTI_ERROR_NONE) {
            // Extract simple name from signature like "Lnet/minecraft/network/.../C03PacketPlayer;"
            const char* lastSlash = strrchr(sig, '/');
            if (lastSlash) {
                strncpy(nameBuf, lastSlash + 1, sizeof(nameBuf) - 1);
                // Remove trailing semicolon
                size_t len = strlen(nameBuf);
                if (len > 0 && nameBuf[len-1] == ';') {
                    nameBuf[len-1] = '\0';
                }
            } else {
                strncpy(nameBuf, sig, sizeof(nameBuf) - 1);
            }
            Java::tiEnv->Deallocate((unsigned char*)sig);
        }
    }
    
    env->DeleteLocalRef(packetClass);
    return nameBuf;
}

// ============================================================================
// Public API
// ============================================================================

bool Init() {
    log_debug("[PacketInterceptor] Initializing...");
    
    if (!Java::Initialized) {
        log_err("[PacketInterceptor] Java not initialized");
        return false;
    }
    
    JNIEnv* env = Java::Env;
    if (!env) {
        log_err("[PacketInterceptor] No JNIEnv");
        return false;
    }
    
    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    bool haveCaps = false;

    if (!Java::tiEnv) {
        log_warn("[PacketInterceptor] JVMTI not available - limited functionality");
    } else {
        log_ok("[PacketInterceptor] JVMTI available");
        if (Java::tiEnv->GetCapabilities(&caps) == JVMTI_ERROR_NONE) {
            haveCaps = true;
            log_debug("[PacketInterceptor] JVMTI Capabilities:");
            log_debug("  can_retransform_classes: %d", caps.can_retransform_classes);
            log_debug("  can_redefine_classes: %d", caps.can_redefine_classes);
            log_debug("  can_generate_all_class_hook_events: %d", caps.can_generate_all_class_hook_events);
            log_debug("  can_generate_method_entry_events: %d", caps.can_generate_method_entry_events);
            log_debug("  can_force_early_return: %d", caps.can_force_early_return);
            log_debug("  can_access_local_variables: %d", caps.can_access_local_variables);
        }
    }
    
    // Cache packet classes
    if (!cachePacketClasses(env)) {
        log_err("[PacketInterceptor] Failed to cache packet classes");
        return false;
    }
    
    bool interceptReady = false;
    if (Java::tiEnv && haveCaps) {
        if (caps.can_generate_all_class_hook_events && caps.can_retransform_classes) {
            if (ensureHookClass(env) && installBytecodeHook(env)) {
                interceptReady = true;
            } else {
                log_warn("[PacketInterceptor] Bytecode hook setup failed");
            }
        } else {
            log_warn("[PacketInterceptor] Class hook capabilities unavailable");
        }

        if (!interceptReady && caps.can_generate_method_entry_events &&
            caps.can_force_early_return && caps.can_access_local_variables) {
            if (installMethodEntryHook()) {
                interceptReady = true;
            }
        }

        if (!interceptReady) {
            log_warn("[PacketInterceptor] No usable JVMTI interception path available");
        }
    }
    
    // Get NetworkManager instance
    networkManagerInstance = getNetworkManager(env);
    if (networkManagerInstance) {
        // Create global ref
        networkManagerInstance = env->NewGlobalRef(networkManagerInstance);
        log_ok("[PacketInterceptor] Cached NetworkManager instance as GlobalRef");
    } else {
        log_warn("[PacketInterceptor] No NetworkManager yet - will try later");
    }
    
    initialized = true;
    log_ok("[PacketInterceptor] Initialized successfully");
    
    return true;
}

void Shutdown() {
    log_debug("[PacketInterceptor] Shutting down...");
    
    blinkActive = false;
    
    // Clear queue
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        JNIEnv* env = Java::Env;
        if (env) {
            for (jobject packet : packetQueue) {
                env->DeleteGlobalRef(packet);
            }
        }
        packetQueue.clear();
    }
    
    // Release global refs
    JNIEnv* env = Java::Env;
    if (env) {
        if (networkManagerInstance) {
            env->DeleteGlobalRef(networkManagerInstance);
            networkManagerInstance = nullptr;
        }
        if (cls_PacketHook) {
            env->DeleteGlobalRef(cls_PacketHook);
            cls_PacketHook = nullptr;
        }
        if (cls_NetworkManager) env->DeleteGlobalRef(cls_NetworkManager);
        if (cls_Packet) env->DeleteGlobalRef(cls_Packet);
        if (cls_C03PacketPlayer) env->DeleteGlobalRef(cls_C03PacketPlayer);
        if (cls_C04PacketPlayerPosition) env->DeleteGlobalRef(cls_C04PacketPlayerPosition);
        if (cls_C05PacketPlayerLook) env->DeleteGlobalRef(cls_C05PacketPlayerLook);
        if (cls_C06PacketPlayerPosLook) env->DeleteGlobalRef(cls_C06PacketPlayerPosLook);
    }
    
    initialized = false;
    log_ok("[PacketInterceptor] Shutdown complete");
}

bool IsReady() {
    return initialized.load();
}

void setBlinkActive(bool active) {
    bool wasActive = blinkActive.exchange(active);
    
    if (active && !wasActive) {
        log_ok("[PacketInterceptor] Blink ACTIVATED - queueing movement packets");
        flushActive = false;
        
        // Ensure we have NetworkManager instance
        if (!networkManagerInstance) {
            JNIEnv* env = Java::Env;
            if (env) {
                jobject nm = getNetworkManager(env);
                if (nm) {
                    networkManagerInstance = env->NewGlobalRef(nm);
                    log_ok("[PacketInterceptor] Late-cached NetworkManager instance");
                }
            }
        }
    } else if (!active && wasActive) {
        log_ok("[PacketInterceptor] Blink DEACTIVATED - draining %d packets", getQueuedCount());
        lastFlushTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(kFlushIntervalMs);
        flushActive = true;
    }
}

bool isBlinkActive() {
    return blinkActive.load();
}

int getQueuedCount() {
    std::lock_guard<std::mutex> lock(queueMutex);
    return (int)packetQueue.size();
}

void flushQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    if (packetQueue.empty()) {
        log_debug("[PacketInterceptor] Nothing to flush");
        return;
    }
    
    JNIEnv* env = Java::Env;
    if (!env || !networkManagerInstance || !mid_sendPacket) {
        log_err("[PacketInterceptor] Cannot flush - missing dependencies");
        // Clear anyway
        for (jobject packet : packetQueue) {
            if (env) env->DeleteGlobalRef(packet);
        }
        packetQueue.clear();
        return;
    }
    
    int count = (int)packetQueue.size();
    log_ok("[PacketInterceptor] Flushing %d queued packets...", count);
    
    for (jobject packet : packetQueue) {
        log_debug("[PacketInterceptor] Sending queued packet: %s", getPacketTypeName(env, packet));
        
        // Call sendPacket on NetworkManager
        env->CallVoidMethod(networkManagerInstance, mid_sendPacket, packet);
        
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            log_err("[PacketInterceptor] Exception while sending queued packet");
        }
        
        env->DeleteGlobalRef(packet);
    }
    
    packetQueue.clear();
    log_ok("[PacketInterceptor] Flush complete - sent %d packets", count);
}

static void flushQueueBatch(int maxCount) {
    if (maxCount <= 0) return;

    std::vector<jobject> toSend;
    toSend.reserve(maxCount);

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        int count = std::min<int>(maxCount, static_cast<int>(packetQueue.size()));
        if (count == 0) return;
        toSend.insert(toSend.end(), packetQueue.begin(), packetQueue.begin() + count);
        packetQueue.erase(packetQueue.begin(), packetQueue.begin() + count);
    }

    JNIEnv* env = Java::Env;
    if (!env || !networkManagerInstance || !mid_sendPacket) {
        log_err("[PacketInterceptor] Cannot flush batch - missing dependencies");
        for (jobject packet : toSend) {
            if (env) env->DeleteGlobalRef(packet);
        }
        return;
    }

    for (jobject packet : toSend) {
        env->CallVoidMethod(networkManagerInstance, mid_sendPacket, packet);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            log_err("[PacketInterceptor] Exception while sending queued packet");
        }
        env->DeleteGlobalRef(packet);
    }
}

void clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    JNIEnv* env = Java::Env;
    for (jobject packet : packetQueue) {
        if (env) env->DeleteGlobalRef(packet);
    }
    packetQueue.clear();
    
    log_debug("[PacketInterceptor] Queue cleared");
}

void printDebugInfo() {
    log_debug("=== PacketInterceptor Debug ===");
    log_debug("  Initialized: %s", initialized.load() ? "YES" : "NO");
    log_debug("  Blink Active: %s", blinkActive.load() ? "YES" : "NO");
    log_debug("  Queued Packets: %d", getQueuedCount());
    log_debug("  NetworkManager: %p", networkManagerInstance);
    log_debug("  sendPacket method: %p", mid_sendPacket);
    log_debug("  C03PacketPlayer class: %p", cls_C03PacketPlayer);
}

void Tick() {
    if (blinkActive.load()) return;
    if (!flushActive.load()) return;

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushTime).count() < kFlushIntervalMs) {
        return;
    }
    lastFlushTime = now;

    flushQueueBatch(kFlushBatchSize);

    if (getQueuedCount() == 0) {
        flushActive = false;
        log_ok("[PacketInterceptor] Drain complete");
    }
}

bool shouldSendPacket(JNIEnv* env, jobject packet) {
    if (!blinkActive.load()) {
        return true;  // Not blinking, send everything
    }
    
    // Check if this is a movement packet
    if (!isMovementPacket(env, packet)) {
        // Not a movement packet - let it through
        return true;
    }
    
    // Movement packet while blinking - queue it
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobject globalRef = env->NewGlobalRef(packet);
        packetQueue.push_back(globalRef);
        
        static int debugCounter = 0;
        if (++debugCounter % 20 == 0 || packetQueue.size() <= 3) {
            log_debug("[PacketInterceptor] Queued %s (total: %d)", 
                getPacketTypeName(env, packet), (int)packetQueue.size());
        }
    }
    
    return false;  // Block this packet
}

} // namespace PacketInterceptor
