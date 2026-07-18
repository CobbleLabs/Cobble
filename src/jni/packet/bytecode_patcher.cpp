#include "bytecode_patcher.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "../../util/logger.hpp"
#include "../../util/aes_string.hpp"
#include "packet_hook_enc.hpp"
#include "../../util/inject_trace.hpp"

namespace BytecodePatcher {
namespace {

static inline std::string kTargetClassSlash() {
    return OBFUSCATE_STRING("net/minecraft/network/NetworkManager");
}
static inline std::string kTargetClassDot() {
    return OBFUSCATE_STRING("net.minecraft.network.NetworkManager");
}
static inline std::string kHookClassSlash() {
    return OBFUSCATE_STRING("com/cobble/agent/PacketHook");
}
static inline std::string kHookClassDot() {
    return OBFUSCATE_STRING("com.cobble.agent.PacketHook");
}
static inline std::string kHookMethodName() {
    return OBFUSCATE_STRING("shouldSend");
}
static inline std::string kHookMethodDesc() {
    return OBFUSCATE_STRING("(Ljava/lang/Object;)Z");
}
static inline std::string kIncomingTargetClassSlash() {
    return OBFUSCATE_STRING("net/minecraft/client/network/NetHandlerPlayClient");
}
static inline std::string kIncomingTargetClassDot() {
    return OBFUSCATE_STRING("net.minecraft.client.network.NetHandlerPlayClient");
}
static inline std::string kHandleCustomPayload() {
    return OBFUSCATE_STRING("handleCustomPayload");
}
static inline std::string kHandleTeams() {
    return OBFUSCATE_STRING("handleTeams");
}
static inline std::string kLunarNetHandlerSlash() {
    return OBFUSCATE_STRING("com/lunarclient/bukkitapi/nethandler/client/LCNetHandlerClient");
}
static inline std::string kLunarHandleNametagsUpdate() {
    return OBFUSCATE_STRING("handleNametagsUpdate");
}
static inline std::string kLunarHandleOverrideNametags() {
    return OBFUSCATE_STRING("handleOverrideNametags");
}
static inline std::string kOnReceiveMethodName() {
    return OBFUSCATE_STRING("onReceive");
}
static inline std::string kOnReceiveMethodDesc() {
    return OBFUSCATE_STRING("(Ljava/lang/Object;)V");
}
// Backtrack incoming BLOCKING hook: a boolean native on a dedicated class, invoked
// from NetHandlerPlayClient.handleEntity/handleEntityTeleport to hold S14/S18.
static inline std::string kIncomingHookClassSlash() {
    return OBFUSCATE_STRING("com/cobble/agent/IncomingHook");
}
static inline std::string kIncomingHookMethodName() {
    return OBFUSCATE_STRING("canProcess");
}
static inline std::string kIncomingHookMethodDesc() {
    return OBFUSCATE_STRING("(Ljava/lang/Object;)Z");
}
static inline std::string kS14ClassMarker() {
    return OBFUSCATE_STRING("S14PacketEntity");
}
static inline std::string kS18ClassMarker() {
    return OBFUSCATE_STRING("S18PacketEntityTeleport");
}
// We patch processPacket on the PACKET classes themselves (not the NetHandler), so it
// survives clients like Lunar that mixin/replace the entity handlers.
static inline std::string kS14ClassSlash() {
    return OBFUSCATE_STRING("net/minecraft/network/play/server/S14PacketEntity");
}
static inline std::string kS18ClassSlash() {
    return OBFUSCATE_STRING("net/minecraft/network/play/server/S18PacketEntityTeleport");
}
static inline std::string kProcessPacketName() {
    return OBFUSCATE_STRING("processPacket");
}
static inline std::string kProcessPacketNameObf() {
    return OBFUSCATE_STRING("func_148833_a");
}

// Vanilla nametag rendering. RendererLivingEntity.renderName draws the actual
// name for every living entity (RenderPlayer.renderName overrides it and also
// draws the scoreboard line, then calls super). No-op'ing both hides the tag.
static inline std::string kRenderPlayerSlash() {
    return OBFUSCATE_STRING("net/minecraft/client/renderer/entity/RenderPlayer");
}
static inline std::string kRenderPlayerDot() {
    return OBFUSCATE_STRING("net.minecraft.client.renderer.entity.RenderPlayer");
}
static inline std::string kRendererLivingSlash() {
    return OBFUSCATE_STRING("net/minecraft/client/renderer/entity/RendererLivingEntity");
}
static inline std::string kRendererLivingDot() {
    return OBFUSCATE_STRING("net.minecraft.client.renderer.entity.RendererLivingEntity");
}
static inline std::string kRenderArmorStandSlash() {
    return OBFUSCATE_STRING("net/minecraft/client/renderer/entity/RenderArmorStand");
}
static inline std::string kRenderArmorStandDot() {
    return OBFUSCATE_STRING("net.minecraft.client.renderer.entity.RenderArmorStand");
}
static inline std::string kRenderNameMethod() {
    return OBFUSCATE_STRING("renderName");
}
static inline std::string kPassSpecialRenderMethod() {
    return OBFUSCATE_STRING("passSpecialRender");
}
static inline std::string kPassSpecialRenderMethodObf() {
    return OBFUSCATE_STRING("func_177069_a");
}

static HookTargets g_targets{};
// True only while patching an S14/S18 packet class, so the shared method patcher knows
// to look for processPacket (and grab `this`, the packet, with aload_0).
static std::atomic<bool> g_btPacketClass{false};
static std::atomic<bool> g_hideVanillaNames{false};
static std::atomic<bool> bytecodeHookInstalled{false};
static std::atomic<bool> hooksRegistered{false};
static jclass cls_PacketHook = nullptr;
static jclass cls_SlowdownTarget = nullptr;
constexpr int kPrefixLen = 8;

// PacketHook class with both methods:
// - public static native boolean shouldSend(Object packet);  // for outgoing (can block)
// - public static native void onReceive(Object packet);      // for incoming (observer only)
//
// Java source equivalent:
// package com.cobble.agent;
// public class PacketHook {
//     public static native boolean shouldSend(Object packet);
//     public static native void onReceive(Object packet);
// }
// PacketHook class bytecode lives encrypted in packet_hook_enc.hpp. The
// constant pool entries (class names, descriptors, "Code") would otherwise
// be plaintext in .rdata and a dead-giveaway anchor for any reverser
// grepping the binary. Decoded into a transient buffer and zeroed after
// DefineClass returns. See packet_hook_enc.hpp for the key schedule.
inline std::vector<uint8_t> DecodeHookClassBytes() {
    std::vector<uint8_t> out(detail::kHookEncSize);
    constexpr uint8_t key_seed = 0x5A;
    for (size_t i = 0; i < detail::kHookEncSize; ++i) {
        uint8_t k = static_cast<uint8_t>(key_seed + i * 0x9D + (i * i) * 0x37);
        out[i] = detail::kHookEnc[i] ^ k;
    }
    return out;
}

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

    void WriteU1(uint8_t v) { out.push_back(v); }

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
        if (count == 0)
            return;
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
    if (!r.ok)
        return false;
    if (info.tag == 7 || info.tag == 8) {
        info.extra = r.ReadU2();
        if (!r.ok)
            return false;
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

static bool readConstantPool(Reader& r, uint16_t cp_count, std::vector<CpEntry>& cp, std::vector<std::string>& utf8,
                             std::vector<uint8_t>& raw) {
    cp.assign(cp_count, CpEntry());
    utf8.assign(cp_count, std::string());
    Writer cpw;

    for (uint16_t i = 1; i < cp_count; ++i) {
        uint8_t tag = r.ReadU1();
        if (!r.ok)
            return false;
        cpw.WriteU1(tag);
        cp[i].tag = tag;

        switch (tag) {
            case 1: {
                uint16_t len = r.ReadU2();
                if (!r.ok)
                    return false;
                cpw.WriteU2(len);
                const uint8_t* bytes = r.ReadBytes(len);
                if (!r.ok)
                    return false;
                cpw.WriteBytes(bytes, len);
                utf8[i] = std::string(reinterpret_cast<const char*>(bytes), len);
                break;
            }
            case 3:
            case 4: {
                const uint8_t* bytes = r.ReadBytes(4);
                if (!r.ok)
                    return false;
                cpw.WriteBytes(bytes, 4);
                break;
            }
            case 5:
            case 6: {
                const uint8_t* bytes = r.ReadBytes(8);
                if (!r.ok)
                    return false;
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
                if (!r.ok)
                    return false;
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
                if (!r.ok)
                    return false;
                cpw.WriteU2(a);
                cpw.WriteU2(b);
                cp[i].a = a;
                cp[i].b = b;
                break;
            }
            case 15: {
                uint8_t kind = r.ReadU1();
                uint16_t ref = r.ReadU2();
                if (!r.ok)
                    return false;
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

static bool resolveMethodRef(const std::vector<CpEntry>& cp, const std::vector<std::string>& utf8, uint16_t index,
                             std::string& className, std::string& methodName, std::string& methodDesc) {
    if (index == 0 || index >= cp.size())
        return false;
    const CpEntry& ref = cp[index];
    if (ref.tag != 10 && ref.tag != 11)
        return false;
    if (ref.a == 0 || ref.a >= cp.size())
        return false;
    if (ref.b == 0 || ref.b >= cp.size())
        return false;

    const CpEntry& cls = cp[ref.a];
    if (cls.tag != 7 || cls.a == 0 || cls.a >= utf8.size())
        return false;
    className = utf8[cls.a];

    const CpEntry& nt = cp[ref.b];
    if (nt.tag != 12 || nt.a == 0 || nt.b == 0)
        return false;
    if (nt.a >= utf8.size() || nt.b >= utf8.size())
        return false;
    methodName = utf8[nt.a];
    methodDesc = utf8[nt.b];
    return true;
}

static bool isHookMethodRef(const std::vector<CpEntry>& cp, const std::vector<std::string>& utf8, uint16_t index) {
    std::string cls;
    std::string name;
    std::string desc;
    if (!resolveMethodRef(cp, utf8, index, cls, name, desc))
        return false;
    return cls == kHookClassSlash() && name == kHookMethodName() && desc == kHookMethodDesc();
}

static bool parseStackMapTable(Reader& r, std::vector<StackMapFrame>& frames) {
    uint16_t count = r.ReadU2();
    if (!r.ok)
        return false;

    int prev_offset = -1;
    frames.clear();
    frames.reserve(count);

    for (uint16_t i = 0; i < count; ++i) {
        uint8_t frame_type = r.ReadU1();
        if (!r.ok)
            return false;

        StackMapFrame frame;
        uint16_t offset_delta = 0;

        if (frame_type <= 63) {
            frame.kind = StackMapFrame::SAME;
            offset_delta = frame_type;
        } else if (frame_type <= 127) {
            frame.kind = StackMapFrame::SAME_LOCALS_1_STACK;
            offset_delta = frame_type - 64;
            VerificationTypeInfo info;
            if (!readVerificationTypeInfo(r, info))
                return false;
            frame.stack.push_back(info);
        } else if (frame_type == 247) {
            frame.kind = StackMapFrame::SAME_LOCALS_1_STACK;
            offset_delta = r.ReadU2();
            VerificationTypeInfo info;
            if (!readVerificationTypeInfo(r, info))
                return false;
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
                if (!readVerificationTypeInfo(r, info))
                    return false;
                frame.locals.push_back(info);
            }
        } else if (frame_type == 255) {
            frame.kind = StackMapFrame::FULL;
            offset_delta = r.ReadU2();
            uint16_t locals_count = r.ReadU2();
            for (uint16_t l = 0; l < locals_count; ++l) {
                VerificationTypeInfo info;
                if (!readVerificationTypeInfo(r, info))
                    return false;
                frame.locals.push_back(info);
            }
            uint16_t stack_count = r.ReadU2();
            for (uint16_t s = 0; s < stack_count; ++s) {
                VerificationTypeInfo info;
                if (!readVerificationTypeInfo(r, info))
                    return false;
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
    if (!parseStackMapTable(r, frames) || !r.ok)
        return false;

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
        if (delta < 0 || delta > 0xFFFF)
            return false;
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
                if (frame.stack.size() != 1)
                    return false;
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
                if (frame.chop_k == 0 || frame.chop_k > 3)
                    return false;
                w.WriteU1(static_cast<uint8_t>(251 - frame.chop_k));
                w.WriteU2(delta_u16);
                break;
            case StackMapFrame::APPEND:
                if (frame.locals.empty() || frame.locals.size() > 3)
                    return false;
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
    if (!r.ok)
        return false;
    w.WriteU2(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t start_pc = r.ReadU2();
        uint16_t line = r.ReadU2();
        if (!r.ok)
            return false;
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
    if (!r.ok)
        return false;
    w.WriteU2(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t start_pc = r.ReadU2();
        uint16_t length = r.ReadU2();
        uint16_t name_index = r.ReadU2();
        uint16_t desc_index = r.ReadU2();
        uint16_t slot = r.ReadU2();
        if (!r.ok)
            return false;
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
    if (len < static_cast<size_t>(kPrefixLen))
        return false;
    uint8_t aload = code[0];
    if (aload != 0x2a && aload != 0x2b)
        return false;
    if (code[1] != 0xb8)
        return false;
    if (code[4] != 0x9a || code[7] != 0xb1)
        return false;
    uint16_t method_ref = static_cast<uint16_t>((code[2] << 8) | code[3]);
    return isHookMethodRef(cp, utf8, method_ref);
}

// Observer patch - only calls hook without modifying control flow
// Used for incoming packets where we want to observe but not block
// Prefix length for observer: aload_X (1) + invokestatic (3) = 4 bytes
constexpr int kObserverPrefixLen = 4;

static bool tryPatchCodeAttributeObserver(const uint8_t* data, size_t len, const std::vector<CpEntry>& cp,
                                          const std::vector<std::string>& utf8, uint16_t hook_methodref,
                                          int packet_slot, std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint16_t max_stack = r.ReadU2();
    uint16_t max_locals = r.ReadU2();
    uint32_t code_len = r.ReadU4();
    const uint8_t* code = r.ReadBytes(code_len);
    if (!r.ok)
        return false;

    // Check if already patched
    if (code_len >= 4 && code[1] == 0xb8) {
        uint16_t method_ref = static_cast<uint16_t>((code[2] << 8) | code[3]);
        if (isHookMethodRef(cp, utf8, method_ref)) {
            return false;  // Already patched
        }
    }

    uint16_t ex_count = r.ReadU2();
    if (!r.ok)
        return false;

    struct ExEntry {
        uint16_t start_pc, end_pc, handler_pc, catch_type;
    };
    std::vector<ExEntry> exceptions;
    exceptions.reserve(ex_count);
    for (uint16_t i = 0; i < ex_count; ++i) {
        ExEntry entry;
        entry.start_pc = r.ReadU2();
        entry.end_pc = r.ReadU2();
        entry.handler_pc = r.ReadU2();
        entry.catch_type = r.ReadU2();
        if (!r.ok)
            return false;
        exceptions.push_back(entry);
    }

    uint16_t attrs_count = r.ReadU2();
    if (!r.ok)
        return false;

    // Build new code: aload_X + invokestatic (void return) + original code
    std::vector<uint8_t> new_code;
    new_code.reserve(code_len + kObserverPrefixLen);

    // aload instruction for the packet parameter
    if (packet_slot <= 3) {
        new_code.push_back(0x2a + packet_slot);  // aload_0..aload_3
    } else {
        new_code.push_back(0x19);  // aload
        new_code.push_back(static_cast<uint8_t>(packet_slot));
    }

    // invokestatic to call the observer hook (void return, no control flow change)
    new_code.push_back(0xb8);
    new_code.push_back(static_cast<uint8_t>((hook_methodref >> 8) & 0xFF));
    new_code.push_back(static_cast<uint8_t>(hook_methodref & 0xFF));

    // Original code continues as normal
    new_code.insert(new_code.end(), code, code + code_len);

    int actual_prefix_len = (packet_slot <= 3) ? 4 : 5;

    Writer w;
    w.WriteU2(std::max<uint16_t>(max_stack, static_cast<uint16_t>(1)));
    w.WriteU2(max_locals);
    w.WriteU4(code_len + actual_prefix_len);
    w.WriteBytes(new_code.data(), new_code.size());

    w.WriteU2(ex_count);
    for (const auto& entry : exceptions) {
        w.WriteU2(static_cast<uint16_t>(entry.start_pc + actual_prefix_len));
        w.WriteU2(static_cast<uint16_t>(entry.end_pc + actual_prefix_len));
        w.WriteU2(static_cast<uint16_t>(entry.handler_pc + actual_prefix_len));
        w.WriteU2(entry.catch_type);
    }

    w.WriteU2(attrs_count);
    for (uint16_t i = 0; i < attrs_count; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok)
            return false;
        Reader sub = r.Slice(attr_len);
        if (!r.ok)
            return false;
        const uint8_t* attr_data = sub.data;

        const char* attr_name = nullptr;
        if (name_index < utf8.size()) {
            attr_name = utf8[name_index].c_str();
        }

        std::vector<uint8_t> patched;
        bool replaced = false;

        if (attr_name && strcmp(attr_name, "StackMapTable") == 0) {
            if (!patchStackMapTable(attr_data, attr_len, actual_prefix_len, patched))
                return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LineNumberTable") == 0) {
            if (!patchLineNumberTable(attr_data, attr_len, actual_prefix_len, patched))
                return false;
            replaced = true;
        } else if (attr_name &&
                   (strcmp(attr_name, "LocalVariableTable") == 0 || strcmp(attr_name, "LocalVariableTypeTable") == 0)) {
            if (!patchLocalVariableTable(attr_data, attr_len, actual_prefix_len, patched))
                return false;
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

    if (!r.ok)
        return false;
    out = std::move(w.out);
    return true;
}

static bool tryPatchCodeAttribute(const uint8_t* data, size_t len, const std::vector<CpEntry>& cp,
                                  const std::vector<std::string>& utf8, uint16_t hook_methodref, bool is_static,
                                  std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint16_t max_stack = r.ReadU2();
    uint16_t max_locals = r.ReadU2();
    uint32_t code_len = r.ReadU4();
    const uint8_t* code = r.ReadBytes(code_len);
    if (!r.ok)
        return false;

    if (isAlreadyPatched(code, code_len, cp, utf8)) {
        return false;
    }

    uint16_t ex_count = r.ReadU2();
    if (!r.ok)
        return false;

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
        if (!r.ok)
            return false;
        exceptions.push_back(entry);
    }

    uint16_t attrs_count = r.ReadU2();
    if (!r.ok)
        return false;

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
        if (!r.ok)
            return false;
        Reader sub = r.Slice(attr_len);
        if (!r.ok)
            return false;
        const uint8_t* attr_data = sub.data;

        const char* attr_name = nullptr;
        if (name_index < utf8.size()) {
            attr_name = utf8[name_index].c_str();
        }

        std::vector<uint8_t> patched;
        bool replaced = false;

        if (attr_name && strcmp(attr_name, "StackMapTable") == 0) {
            if (!patchStackMapTable(attr_data, attr_len, kPrefixLen, patched))
                return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LineNumberTable") == 0) {
            if (!patchLineNumberTable(attr_data, attr_len, kPrefixLen, patched))
                return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LocalVariableTable") == 0) {
            if (!patchLocalVariableTable(attr_data, attr_len, kPrefixLen, patched))
                return false;
            replaced = true;
        } else if (attr_name && strcmp(attr_name, "LocalVariableTypeTable") == 0) {
            if (!patchLocalVariableTable(attr_data, attr_len, kPrefixLen, patched))
                return false;
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

    if (!r.ok)
        return false;
    out = std::move(w.out);
    return true;
}

static int patchNetworkManagerClass(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    COBBLE_TRACE("patchNetworkManagerClass: enter");
    Reader r(data, len);
    uint32_t magic = r.ReadU4();
    if (!r.ok || magic != 0xCAFEBABE) {
        COBBLE_TRACE("patchNetworkManagerClass: invalid magic");
        return -1;
    }

    uint16_t minor = r.ReadU2();
    uint16_t major = r.ReadU2();
    uint16_t cp_count = r.ReadU2();
    if (!r.ok || cp_count == 0) {
        COBBLE_TRACE("patchNetworkManagerClass: invalid cp_count");
        return -1;
    }

    char traceBuf[1024];
    wsprintfA(traceBuf, "patchNetworkManagerClass: cp_count=%d", cp_count);
    COBBLE_TRACE(traceBuf);

    std::vector<CpEntry> cp;
    std::vector<std::string> utf8;
    std::vector<uint8_t> cp_raw;
    if (!readConstantPool(r, cp_count, cp, utf8, cp_raw)) {
        COBBLE_TRACE("patchNetworkManagerClass: readConstantPool failed");
        return -1;
    }
    COBBLE_TRACE("patchNetworkManagerClass: constant pool read successfully");

    if (cp_count > 0xFFF3) {
        COBBLE_TRACE("patchNetworkManagerClass: cp_count too large");
        return -1;  // Need room for 12 new entries (6 for each method)
    }
    uint16_t base = cp_count;

    // Entries for shouldSend (blocking hook)
    uint16_t idx_utf8_class = base;
    uint16_t idx_class = static_cast<uint16_t>(base + 1);
    uint16_t idx_utf8_name = static_cast<uint16_t>(base + 2);
    uint16_t idx_utf8_desc = static_cast<uint16_t>(base + 3);
    uint16_t idx_name_type = static_cast<uint16_t>(base + 4);
    uint16_t idx_methodref = static_cast<uint16_t>(base + 5);

    // Entries for onReceive (observer hook) - shares class ref with shouldSend
    uint16_t idx_utf8_recv_name = static_cast<uint16_t>(base + 6);
    uint16_t idx_utf8_recv_desc = static_cast<uint16_t>(base + 7);
    uint16_t idx_recv_name_type = static_cast<uint16_t>(base + 8);
    uint16_t idx_recv_methodref = static_cast<uint16_t>(base + 9);

    // Entries for IncomingHook.shouldProcess (backtrack blocking hook) — its own class.
    uint16_t idx_bt_utf8_class = static_cast<uint16_t>(base + 10);
    uint16_t idx_bt_class = static_cast<uint16_t>(base + 11);
    uint16_t idx_bt_utf8_name = static_cast<uint16_t>(base + 12);
    uint16_t idx_bt_utf8_desc = static_cast<uint16_t>(base + 13);
    uint16_t idx_bt_name_type = static_cast<uint16_t>(base + 14);
    uint16_t idx_bt_methodref = static_cast<uint16_t>(base + 15);

    Writer w;
    w.WriteU4(magic);
    w.WriteU2(minor);
    w.WriteU2(major);
    w.WriteU2(static_cast<uint16_t>(cp_count + 16));  // 6 shouldSend + 4 onReceive + 6 IncomingHook
    w.WriteBytes(cp_raw.data(), cp_raw.size());

    // shouldSend entries
    w.WriteU1(1);  // CONSTANT_Utf8
    {
        std::string _s = kHookClassSlash();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(7);  // CONSTANT_Class
    w.WriteU2(idx_utf8_class);
    w.WriteU1(1);  // CONSTANT_Utf8
    {
        std::string _s = kHookMethodName();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(1);  // CONSTANT_Utf8
    {
        std::string _s = kHookMethodDesc();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(12);  // CONSTANT_NameAndType
    w.WriteU2(idx_utf8_name);
    w.WriteU2(idx_utf8_desc);
    w.WriteU1(10);  // CONSTANT_Methodref
    w.WriteU2(idx_class);
    w.WriteU2(idx_name_type);

    // onReceive entries (reuses idx_class)
    w.WriteU1(1);  // CONSTANT_Utf8
    {
        std::string _s = kOnReceiveMethodName();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(1);  // CONSTANT_Utf8
    {
        std::string _s = kOnReceiveMethodDesc();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(12);  // CONSTANT_NameAndType
    w.WriteU2(idx_utf8_recv_name);
    w.WriteU2(idx_utf8_recv_desc);
    w.WriteU1(10);  // CONSTANT_Methodref
    w.WriteU2(idx_class);
    w.WriteU2(idx_recv_name_type);

    // IncomingHook.shouldProcess entries (own class ref). Order MUST match the index
    // assignment above (base+10 .. base+15).
    w.WriteU1(1);  // #base+10 CONSTANT_Utf8 "com/cobble/agent/IncomingHook"
    {
        std::string _s = kIncomingHookClassSlash();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(7);  // #base+11 CONSTANT_Class -> base+10
    w.WriteU2(idx_bt_utf8_class);
    w.WriteU1(1);  // #base+12 CONSTANT_Utf8 "shouldProcess"
    {
        std::string _s = kIncomingHookMethodName();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(1);  // #base+13 CONSTANT_Utf8 "(Ljava/lang/Object;)Z"
    {
        std::string _s = kIncomingHookMethodDesc();
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(12);  // #base+14 CONSTANT_NameAndType -> base+12, base+13
    w.WriteU2(idx_bt_utf8_name);
    w.WriteU2(idx_bt_utf8_desc);
    w.WriteU1(10);  // #base+15 CONSTANT_Methodref -> base+11, base+14
    w.WriteU2(idx_bt_class);
    w.WriteU2(idx_bt_name_type);

    COBBLE_TRACE("patchNetworkManagerClass: constant pool modifications written");

    uint16_t access_flags = r.ReadU2();
    uint16_t this_class = r.ReadU2();
    uint16_t super_class = r.ReadU2();
    if (!r.ok) {
        COBBLE_TRACE("patchNetworkManagerClass: failed reading access/this/super class");
        return -1;
    }
    w.WriteU2(access_flags);
    w.WriteU2(this_class);
    w.WriteU2(super_class);

    uint16_t interfaces_count = r.ReadU2();
    if (!r.ok) {
        COBBLE_TRACE("patchNetworkManagerClass: failed reading interfaces_count");
        return -1;
    }
    w.WriteU2(interfaces_count);
    for (uint16_t i = 0; i < interfaces_count; ++i) {
        uint16_t idx = r.ReadU2();
        if (!r.ok) {
            COBBLE_TRACE("patchNetworkManagerClass: failed reading interface index");
            return -1;
        }
        w.WriteU2(idx);
    }

    uint16_t fields_count = r.ReadU2();
    if (!r.ok) {
        COBBLE_TRACE("patchNetworkManagerClass: failed reading fields_count");
        return -1;
    }
    wsprintfA(traceBuf, "patchNetworkManagerClass: fields_count=%d", fields_count);
    COBBLE_TRACE(traceBuf);
    w.WriteU2(fields_count);
    for (uint16_t i = 0; i < fields_count; ++i) {
        uint16_t f_access = r.ReadU2();
        uint16_t f_name = r.ReadU2();
        uint16_t f_desc = r.ReadU2();
        uint16_t f_attrs = r.ReadU2();
        if (!r.ok) {
            COBBLE_TRACE("patchNetworkManagerClass: failed reading field metadata");
            return -1;
        }
        w.WriteU2(f_access);
        w.WriteU2(f_name);
        w.WriteU2(f_desc);
        w.WriteU2(f_attrs);
        for (uint16_t a = 0; a < f_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok) {
                COBBLE_TRACE("patchNetworkManagerClass: failed reading field attribute metadata");
                return -1;
            }
            const uint8_t* bytes = r.ReadBytes(attr_len);
            if (!r.ok) {
                COBBLE_TRACE("patchNetworkManagerClass: failed reading field attribute bytes");
                return -1;
            }
            w.WriteU2(name_index);
            w.WriteU4(attr_len);
            w.WriteBytes(bytes, attr_len);
        }
    }
    COBBLE_TRACE("patchNetworkManagerClass: fields parsed successfully");

    uint16_t methods_count = r.ReadU2();
    if (!r.ok) {
        COBBLE_TRACE("patchNetworkManagerClass: failed reading methods_count");
        return -1;
    }
    wsprintfA(traceBuf, "patchNetworkManagerClass: methods_count=%d", methods_count);
    COBBLE_TRACE(traceBuf);

    // Lunar Client injects ~120 extra methods into NetHandlerPlayClient via Mixins.
    // Patching that inflated version causes a JVM VerifyError (mixin StackMapTable
    // frames). Vanilla NetHandlerPlayClient has ~40 methods; over 80 is modded — skip.
    // (Backtrack no longer needs this class: it patches S14/S18 processPacket instead.)
    if (methods_count > 80) {
        COBBLE_TRACE("patchNetworkManagerClass: methods_count too high (Lunar mixin class) — skipping patch");
        return 0;
    }

    w.WriteU2(methods_count);

    int patched_methods = 0;
    for (uint16_t i = 0; i < methods_count; ++i) {
        uint16_t m_access = r.ReadU2();
        uint16_t m_name = r.ReadU2();
        uint16_t m_desc = r.ReadU2();
        uint16_t m_attrs = r.ReadU2();
        if (!r.ok) {
            COBBLE_TRACE("patchNetworkManagerClass: failed reading method metadata");
            return -1;
        }
        w.WriteU2(m_access);
        w.WriteU2(m_name);
        w.WriteU2(m_desc);
        w.WriteU2(m_attrs);

        bool is_target = false;
        bool is_incoming_target = false;
        bool is_backtrack_target = false;  // handleEntity/handleEntityTeleport → BLOCKING hook
        int incoming_packet_slot = 2;      // Default: second param for channelRead0
        if (m_name < utf8.size() && m_desc < utf8.size()) {
            const std::string& name = utf8[m_name];
            const std::string& desc = utf8[m_desc];

            wsprintfA(traceBuf, "patchNetworkManagerClass: method %d/%d (%s %s)", i, methods_count, name.c_str(),
                      desc.c_str());
            COBBLE_TRACE(traceBuf);

            // Log all methods for debugging
            static bool loggedMethods = false;
            if (!loggedMethods && name.length() > 0) {
                log_debug("[PacketInterceptor] NetworkManager method: %s %s", name.c_str(), desc.c_str());
            }

            if (g_targets.outgoing &&
                (name == OBFUSCATE_STRING("sendPacket") || name == OBFUSCATE_STRING("scheduleOutboundPacket")) &&
                desc.size() >= 3 && desc.back() == 'V' && desc[desc.size() - 2] == ')') {
                // Accept either full MCP name or obfuscated single-letter class name
                if (desc.rfind(OBFUSCATE_STRING("(Lnet/minecraft/network/Packet;"), 0) == 0) {
                    is_target = true;
                    log_debug("[PacketInterceptor] Found %s with MCP signature: %s", name.c_str(), desc.c_str());
                } else if (desc[0] == '(' && desc[1] == 'L' && desc.find(';') != std::string::npos) {
                    // Obfuscated: (Lej;)V or similar - any class that takes one object param and returns void
                    std::string paramClass = desc.substr(2, desc.find(';') - 2);
                    if (paramClass.find('/') == std::string::npos || paramClass.find("Packet") != std::string::npos) {
                        is_target = true;
                        log_debug("[PacketInterceptor] Found %s with obfuscated signature: %s", name.c_str(),
                                  desc.c_str());
                    }
                }
            }

            // Detect channelRead0 for incoming packets (observer hook, non-destructive)
            // channelRead0 signature: (ChannelHandlerContext, Object) -> void
            // Packet is the SECOND parameter (slot 2 for instance method)
            if (g_targets.incoming && name == "channelRead0") {
                // Must have exactly 2 ref params and return void: (Lxxx;Lyyy;)V
                size_t semiCount = std::count(desc.begin(), desc.end(), ';');
                if (semiCount == 2 && desc.back() == 'V') {
                    is_incoming_target = true;
                    incoming_packet_slot = 2;  // Second param (after ChannelHandlerContext)
                    log_debug("[PacketInterceptor] Found channelRead0 for incoming: %s", desc.c_str());
                }
            }

            // Detect Lunar Client nametag handler methods
            // handleNametagsUpdate(LCPacketNametagsUpdate)V - packet is first param (slot 1)
            // handleOverrideNametags(LCPacketNametagsOverride)V - packet is first param (slot 1)
            if (g_targets.lunarNametags &&
                (name == kLunarHandleNametagsUpdate() || name == kLunarHandleOverrideNametags())) {
                // Single param that takes LCPacket and returns void
                if (desc.back() == 'V' && desc.find(';') != std::string::npos) {
                    is_incoming_target = true;
                    incoming_packet_slot = 1;  // First param for instance method
                    log_debug("[PacketInterceptor] Found Lunar nametag handler: %s %s (access=0x%04X, attrs=%d)",
                              name.c_str(), desc.c_str(), m_access, m_attrs);
                    // Check if method is abstract or native (no Code attribute)
                    bool isAbstract = (m_access & 0x0400) != 0;
                    bool isNative = (m_access & 0x0100) != 0;
                    if (isAbstract || isNative) {
                        log_warn("[PacketInterceptor] %s is %s - no Code to patch. Need to find implementation class!",
                                 name.c_str(), isAbstract ? "ABSTRACT" : "NATIVE");
                    }
                }
            }

            // Detect handleCustomPayload for incoming custom payloads (Lunar packets)
            // handleCustomPayload(S3FPacketCustomPayload packetIn) -> void
            // Packet is the first param (slot 1 for instance method)
            if (g_targets.customPayload && name == kHandleCustomPayload()) {
                // Must take one reference param and return void
                if (desc.back() == 'V' && desc.find(';') != std::string::npos) {
                    is_incoming_target = true;
                    incoming_packet_slot = 1;  // First param for instance method
                    log_debug("[PacketInterceptor] Found handleCustomPayload: %s (access=0x%04X)", desc.c_str(),
                              m_access);
                }
            }

            // Detect handleTeams for scoreboard team packets (S3EPacketTeams)
            // handleTeams(S3EPacketTeams packetIn) -> void
            if (g_targets.teams && name == kHandleTeams()) {
                if (desc.back() == 'V' && desc.find(';') != std::string::npos) {
                    is_incoming_target = true;
                    incoming_packet_slot = 1;
                    log_debug("[PacketInterceptor] Found handleTeams: %s (access=0x%04X)", desc.c_str(), m_access);
                }
            }

            // Backtrack: patch processPacket(INetHandler)V on the S14/S18 PACKET class
            // itself. processPacket calls the NetHandler's entity handler; cutting it here
            // holds the packet without touching the handler — so it survives clients like
            // Lunar that mixin/replace the handlers. `this` (the packet) is grabbed below.
            if (g_targets.backtrack && g_btPacketClass.load(std::memory_order_relaxed) && desc.back() == 'V' &&
                std::count(desc.begin(), desc.end(), ';') == 1 &&
                (name == kProcessPacketName() || name == kProcessPacketNameObf())) {
                is_backtrack_target = true;
                log_debug("[PacketInterceptor] Found backtrack processPacket: %s %s", name.c_str(), desc.c_str());
            }

            // Mark that we've logged methods
            if (!loggedMethods && i == methods_count - 1) {
                loggedMethods = true;
            }
        } else {
            wsprintfA(traceBuf, "patchNetworkManagerClass: method %d/%d (invalid indices name=%d, desc=%d)", i,
                      methods_count, m_name, m_desc);
            COBBLE_TRACE(traceBuf);
        }

        for (uint16_t a = 0; a < m_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok) {
                COBBLE_TRACE("patchNetworkManagerClass: failed reading method attribute headers");
                return -1;
            }
            Reader attr_reader = r.Slice(attr_len);
            if (!r.ok) {
                COBBLE_TRACE("patchNetworkManagerClass: failed slicing method attribute payload");
                return -1;
            }
            const uint8_t* attr_data = attr_reader.data;

            const char* attr_name = nullptr;
            if (name_index < utf8.size()) {
                attr_name = utf8[name_index].c_str();
            }

            wsprintfA(traceBuf, "patchNetworkManagerClass: method %d attr %d/%d (name=%s, len=%d)", i, a, m_attrs,
                      attr_name ? attr_name : "unknown", attr_len);
            COBBLE_TRACE(traceBuf);

            bool patched = false;
            std::vector<uint8_t> patched_attr;

            // Patch outgoing packets (sendPacket) - blocking hook
            if (is_target && attr_name && strcmp(attr_name, "Code") == 0 && (m_access & (0x0100 | 0x0400)) == 0) {
                COBBLE_TRACE("patchNetworkManagerClass: calling tryPatchCodeAttribute");
                bool is_static = (m_access & 0x0008) != 0;
                if (tryPatchCodeAttribute(attr_data, attr_len, cp, utf8, idx_methodref, is_static, patched_attr)) {
                    w.WriteU2(name_index);
                    w.WriteU4(static_cast<uint32_t>(patched_attr.size()));
                    w.WriteBytes(patched_attr.data(), patched_attr.size());
                    patched = true;
                    ++patched_methods;
                    COBBLE_TRACE("patchNetworkManagerClass: tryPatchCodeAttribute SUCCESS");
                } else {
                    COBBLE_TRACE("patchNetworkManagerClass: tryPatchCodeAttribute returned false");
                }
            }

            // Patch incoming packets (channelRead0) - observer hook (non-blocking)
            if (!patched && is_incoming_target && attr_name && strcmp(attr_name, "Code") == 0 &&
                (m_access & (0x0100 | 0x0400)) == 0) {
                COBBLE_TRACE("patchNetworkManagerClass: calling tryPatchCodeAttributeObserver");
                const std::string& method_name = (m_name < utf8.size()) ? utf8[m_name] : "";
                log_debug("[PacketInterceptor] Attempting observer patch for: %s (slot=%d, methodref=%d)",
                          method_name.c_str(), incoming_packet_slot, idx_recv_methodref);

                if (tryPatchCodeAttributeObserver(attr_data, attr_len, cp, utf8, idx_recv_methodref,
                                                  incoming_packet_slot, patched_attr)) {
                    w.WriteU2(name_index);
                    w.WriteU4(static_cast<uint32_t>(patched_attr.size()));
                    w.WriteBytes(patched_attr.data(), patched_attr.size());
                    patched = true;
                    ++patched_methods;
                    log_ok("[PacketInterceptor] Patched incoming method: %s", method_name.c_str());
                    COBBLE_TRACE("patchNetworkManagerClass: tryPatchCodeAttributeObserver SUCCESS");
                } else {
                    log_debug("[PacketInterceptor] Observer patch FAILED for: %s", method_name.c_str());
                    COBBLE_TRACE("patchNetworkManagerClass: tryPatchCodeAttributeObserver returned false");
                }
            }

            // Patch S14/S18 processPacket (Backtrack) - BLOCKING hook. is_static=true emits
            // aload_0, i.e. `this` = the packet, which is what IncomingHook.canProcess wants.
            // When it returns false the method returns early, so the entity handler is never
            // called and the entity stays at its held position.
            if (!patched && is_backtrack_target && attr_name && strcmp(attr_name, "Code") == 0 &&
                (m_access & (0x0100 | 0x0400)) == 0) {
                COBBLE_TRACE("patchNetworkManagerClass: calling tryPatchCodeAttribute (backtrack)");
                if (tryPatchCodeAttribute(attr_data, attr_len, cp, utf8, idx_bt_methodref,
                                          /*is_static=*/true, patched_attr)) {
                    w.WriteU2(name_index);
                    w.WriteU4(static_cast<uint32_t>(patched_attr.size()));
                    w.WriteBytes(patched_attr.data(), patched_attr.size());
                    patched = true;
                    ++patched_methods;
                    log_ok("[PacketInterceptor] Patched backtrack entity handler (blocking)");
                }
            }

            if (!patched) {
                w.WriteU2(name_index);
                w.WriteU4(attr_len);
                w.WriteBytes(attr_data, attr_len);
            }
        }
    }
    COBBLE_TRACE("patchNetworkManagerClass: methods loop finished");

    uint16_t class_attrs = r.ReadU2();
    if (!r.ok) {
        COBBLE_TRACE("patchNetworkManagerClass: failed reading class attributes count");
        return -1;
    }
    wsprintfA(traceBuf, "patchNetworkManagerClass: class_attrs count=%d", class_attrs);
    COBBLE_TRACE(traceBuf);
    w.WriteU2(class_attrs);
    for (uint16_t i = 0; i < class_attrs; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok) {
            COBBLE_TRACE("patchNetworkManagerClass: failed reading class attribute metadata");
            return -1;
        }
        const uint8_t* bytes = r.ReadBytes(attr_len);
        if (!r.ok) {
            COBBLE_TRACE("patchNetworkManagerClass: failed reading class attribute payload");
            return -1;
        }
        w.WriteU2(name_index);
        w.WriteU4(attr_len);
        w.WriteBytes(bytes, attr_len);
    }

    wsprintfA(traceBuf, "patchNetworkManagerClass: done, patched_methods=%d", patched_methods);
    COBBLE_TRACE(traceBuf);

    if (patched_methods == 0)
        return 0;
    out = std::move(w.out);
    return patched_methods;
}

static int patchLivingBaseClass(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint32_t magic = r.ReadU4();
    if (!r.ok || magic != 0xCAFEBABE)
        return -1;

    uint16_t minor = r.ReadU2();
    uint16_t major = r.ReadU2();
    uint16_t cp_count = r.ReadU2();
    if (!r.ok || cp_count == 0)
        return -1;

    std::vector<CpEntry> cp;
    std::vector<std::string> utf8;
    std::vector<uint8_t> cp_raw;
    if (!readConstantPool(r, cp_count, cp, utf8, cp_raw))
        return -1;

    if (cp_count > 0xFFFB)
        return -1;
    uint16_t base = cp_count;

    // Entries for SlowDownHook.onSlowDown
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

    // com/cobble/agent/SlowDownHook class
    w.WriteU1(1);
    {
        std::string _s = OBFUSCATE_STRING("com/cobble/agent/SlowDownHook");
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(7);
    w.WriteU2(idx_utf8_class);

    // onSlowDown method name
    w.WriteU1(1);
    {
        std::string _s = OBFUSCATE_STRING("onSlowDown");
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }

    // (Ljava/lang/Object;)V descriptor
    w.WriteU1(1);
    {
        std::string _s = OBFUSCATE_STRING("(Ljava/lang/Object;)V");
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }

    w.WriteU1(12);
    w.WriteU2(idx_utf8_name);
    w.WriteU2(idx_utf8_desc);

    w.WriteU1(10);
    w.WriteU2(idx_class);
    w.WriteU2(idx_name_type);

    uint16_t access_flags = r.ReadU2();
    uint16_t this_class = r.ReadU2();
    uint16_t super_class = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(access_flags);
    w.WriteU2(this_class);
    w.WriteU2(super_class);

    uint16_t interfaces_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(interfaces_count);
    for (uint16_t i = 0; i < interfaces_count; ++i) {
        uint16_t idx = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(idx);
    }

    uint16_t fields_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(fields_count);
    for (uint16_t i = 0; i < fields_count; ++i) {
        uint16_t f_access = r.ReadU2();
        uint16_t f_name = r.ReadU2();
        uint16_t f_desc = r.ReadU2();
        uint16_t f_attrs = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(f_access);
        w.WriteU2(f_name);
        w.WriteU2(f_desc);
        w.WriteU2(f_attrs);
        for (uint16_t a = 0; a < f_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok)
                return -1;
            const uint8_t* bytes = r.ReadBytes(attr_len);
            if (!r.ok)
                return -1;
            w.WriteU2(name_index);
            w.WriteU4(attr_len);
            w.WriteBytes(bytes, attr_len);
        }
    }

    uint16_t methods_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(methods_count);

    int patched_methods = 0;
    for (uint16_t i = 0; i < methods_count; ++i) {
        uint16_t m_access = r.ReadU2();
        uint16_t m_name = r.ReadU2();
        uint16_t m_desc = r.ReadU2();
        uint16_t m_attrs = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(m_access);
        w.WriteU2(m_name);
        w.WriteU2(m_desc);
        w.WriteU2(m_attrs);

        bool is_target = false;
        if (m_name < utf8.size() && m_desc < utf8.size()) {
            const std::string& name = utf8[m_name];
            const std::string& desc = utf8[m_desc];

            if (desc == "()V" && (name == "onLivingUpdate" || name == "func_70636_d" || name == "m")) {
                is_target = true;
                log_debug("[PacketInterceptor] Found living update method: %s %s", name.c_str(), desc.c_str());
            }
        }

        for (uint16_t a = 0; a < m_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok)
                return -1;
            Reader attr_reader = r.Slice(attr_len);
            if (!r.ok)
                return -1;
            const uint8_t* attr_data = attr_reader.data;

            const char* attr_name = nullptr;
            if (name_index < utf8.size()) {
                attr_name = utf8[name_index].c_str();
            }

            bool patched = false;
            std::vector<uint8_t> patched_attr;

            if (is_target && attr_name && strcmp(attr_name, "Code") == 0 && (m_access & (0x0100 | 0x0400)) == 0) {
                if (tryPatchCodeAttributeObserver(attr_data, attr_len, cp, utf8, idx_methodref, 0, patched_attr)) {
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
    if (!r.ok)
        return -1;
    w.WriteU2(class_attrs);
    for (uint16_t i = 0; i < class_attrs; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok)
            return -1;
        const uint8_t* bytes = r.ReadBytes(attr_len);
        if (!r.ok)
            return -1;
        w.WriteU2(name_index);
        w.WriteU4(attr_len);
        w.WriteBytes(bytes, attr_len);
    }

    if (patched_methods == 0)
        return 0;
    out = std::move(w.out);
    return patched_methods;
}

// Inject `aload_1; invokestatic BacktrackGhostHook.onRenderWorld(Object)` at the
// entry of RenderGlobal.renderEntities. Slot 1 is the view Entity (an object),
// which the hook ignores — reusing the exact proven observer injection keeps this
// verifiable. Fail-safe: a parse error returns -1 and the original class is kept;
// bad bytecode is rejected cleanly by the verifier on retransform.
static int patchRenderGlobalClass(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint32_t magic = r.ReadU4();
    if (!r.ok || magic != 0xCAFEBABE)
        return -1;

    uint16_t minor = r.ReadU2();
    uint16_t major = r.ReadU2();
    uint16_t cp_count = r.ReadU2();
    if (!r.ok || cp_count == 0)
        return -1;

    std::vector<CpEntry> cp;
    std::vector<std::string> utf8;
    std::vector<uint8_t> cp_raw;
    if (!readConstantPool(r, cp_count, cp, utf8, cp_raw))
        return -1;

    if (cp_count > 0xFFFB)
        return -1;
    uint16_t base = cp_count;

    // Entries for BacktrackGhostHook.onRenderWorld(Object)
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

    // com/cobble/agent/BacktrackGhostHook class
    w.WriteU1(1);
    {
        std::string _s = OBFUSCATE_STRING("com/cobble/agent/BacktrackGhostHook");
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }
    w.WriteU1(7);
    w.WriteU2(idx_utf8_class);

    // onRenderWorld method name
    w.WriteU1(1);
    {
        std::string _s = OBFUSCATE_STRING("onRenderWorld");
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }

    // (Ljava/lang/Object;)V descriptor
    w.WriteU1(1);
    {
        std::string _s = OBFUSCATE_STRING("(Ljava/lang/Object;)V");
        w.WriteU2(static_cast<uint16_t>(_s.size()));
        w.WriteBytes(reinterpret_cast<const uint8_t*>(_s.data()), _s.size());
    }

    w.WriteU1(12);
    w.WriteU2(idx_utf8_name);
    w.WriteU2(idx_utf8_desc);

    w.WriteU1(10);
    w.WriteU2(idx_class);
    w.WriteU2(idx_name_type);

    uint16_t access_flags = r.ReadU2();
    uint16_t this_class = r.ReadU2();
    uint16_t super_class = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(access_flags);
    w.WriteU2(this_class);
    w.WriteU2(super_class);

    uint16_t interfaces_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(interfaces_count);
    for (uint16_t i = 0; i < interfaces_count; ++i) {
        uint16_t idx = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(idx);
    }

    uint16_t fields_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(fields_count);
    for (uint16_t i = 0; i < fields_count; ++i) {
        uint16_t f_access = r.ReadU2();
        uint16_t f_name = r.ReadU2();
        uint16_t f_desc = r.ReadU2();
        uint16_t f_attrs = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(f_access);
        w.WriteU2(f_name);
        w.WriteU2(f_desc);
        w.WriteU2(f_attrs);
        for (uint16_t a = 0; a < f_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok)
                return -1;
            const uint8_t* bytes = r.ReadBytes(attr_len);
            if (!r.ok)
                return -1;
            w.WriteU2(name_index);
            w.WriteU4(attr_len);
            w.WriteBytes(bytes, attr_len);
        }
    }

    uint16_t methods_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(methods_count);

    int patched_methods = 0;
    for (uint16_t i = 0; i < methods_count; ++i) {
        uint16_t m_access = r.ReadU2();
        uint16_t m_name = r.ReadU2();
        uint16_t m_desc = r.ReadU2();
        uint16_t m_attrs = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(m_access);
        w.WriteU2(m_name);
        w.WriteU2(m_desc);
        w.WriteU2(m_attrs);

        bool is_target = false;
        if (m_name < utf8.size() && m_desc < utf8.size()) {
            const std::string& name = utf8[m_name];
            const std::string& desc = utf8[m_desc];
            // RenderGlobal.renderEntities(Entity, ICamera, float): slot 1 is the
            // view Entity (Object), which is what we load for the hook arg.
            const bool nameMatch = (name == "renderEntities" || name == "func_180446_a");
            if (nameMatch && desc.find("ICamera") != std::string::npos && desc.rfind(")V") != std::string::npos) {
                is_target = true;
                log_debug("[PacketInterceptor] Found renderEntities method: %s %s", name.c_str(), desc.c_str());
            }
        }

        for (uint16_t a = 0; a < m_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok)
                return -1;
            Reader attr_reader = r.Slice(attr_len);
            if (!r.ok)
                return -1;
            const uint8_t* attr_data = attr_reader.data;

            const char* attr_name = nullptr;
            if (name_index < utf8.size())
                attr_name = utf8[name_index].c_str();

            bool patched = false;
            std::vector<uint8_t> patched_attr;

            if (is_target && attr_name && strcmp(attr_name, "Code") == 0 && (m_access & (0x0100 | 0x0400)) == 0) {
                if (tryPatchCodeAttributeObserver(attr_data, attr_len, cp, utf8, idx_methodref, 1, patched_attr)) {
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
    if (!r.ok)
        return -1;
    w.WriteU2(class_attrs);
    for (uint16_t i = 0; i < class_attrs; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok)
            return -1;
        const uint8_t* bytes = r.ReadBytes(attr_len);
        if (!r.ok)
            return -1;
        w.WriteU2(name_index);
        w.WriteU4(attr_len);
        w.WriteBytes(bytes, attr_len);
    }

    if (patched_methods == 0)
        return 0;
    out = std::move(w.out);
    return patched_methods;
}

// Replace every concrete renderName(...)V body with a single `return`. A method
// that is one basic block needs no StackMapTable, so this always verifies — a
// parse failure returns -1 and the caller keeps the original class (no crash).
static int patchRenderNameNoop(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    Reader r(data, len);
    uint32_t magic = r.ReadU4();
    if (!r.ok || magic != 0xCAFEBABE)
        return -1;

    uint16_t minor = r.ReadU2();
    uint16_t major = r.ReadU2();
    uint16_t cp_count = r.ReadU2();
    if (!r.ok || cp_count == 0)
        return -1;

    std::vector<CpEntry> cp;
    std::vector<std::string> utf8;
    std::vector<uint8_t> cp_raw;
    if (!readConstantPool(r, cp_count, cp, utf8, cp_raw))
        return -1;

    Writer w;
    w.WriteU4(magic);
    w.WriteU2(minor);
    w.WriteU2(major);
    w.WriteU2(cp_count);  // no new constant pool entries
    w.WriteBytes(cp_raw.data(), cp_raw.size());

    uint16_t access_flags = r.ReadU2();
    uint16_t this_class = r.ReadU2();
    uint16_t super_class = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(access_flags);
    w.WriteU2(this_class);
    w.WriteU2(super_class);

    uint16_t interfaces_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(interfaces_count);
    for (uint16_t i = 0; i < interfaces_count; ++i) {
        uint16_t idx = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(idx);
    }

    uint16_t fields_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(fields_count);
    for (uint16_t i = 0; i < fields_count; ++i) {
        uint16_t f_access = r.ReadU2();
        uint16_t f_name = r.ReadU2();
        uint16_t f_desc = r.ReadU2();
        uint16_t f_attrs = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(f_access);
        w.WriteU2(f_name);
        w.WriteU2(f_desc);
        w.WriteU2(f_attrs);
        for (uint16_t a = 0; a < f_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok)
                return -1;
            const uint8_t* bytes = r.ReadBytes(attr_len);
            if (!r.ok)
                return -1;
            w.WriteU2(name_index);
            w.WriteU4(attr_len);
            w.WriteBytes(bytes, attr_len);
        }
    }

    uint16_t methods_count = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(methods_count);

    int patched = 0;
    for (uint16_t i = 0; i < methods_count; ++i) {
        uint16_t m_access = r.ReadU2();
        uint16_t m_name = r.ReadU2();
        uint16_t m_desc = r.ReadU2();
        uint16_t m_attrs = r.ReadU2();
        if (!r.ok)
            return -1;
        w.WriteU2(m_access);
        w.WriteU2(m_name);
        w.WriteU2(m_desc);
        w.WriteU2(m_attrs);

        bool is_target = false;
        if (m_name < utf8.size() && m_desc < utf8.size()) {
            const std::string& name = utf8[m_name];
            const std::string& desc = utf8[m_desc];
            bool concrete = (m_access & (0x0100 | 0x0400)) == 0;  // not native/abstract
            if (concrete &&
                (name == kRenderNameMethod() || name == kPassSpecialRenderMethod() ||
                 name == kPassSpecialRenderMethodObf()) &&
                !desc.empty() && desc.back() == 'V')
                is_target = true;
        }

        for (uint16_t a = 0; a < m_attrs; ++a) {
            uint16_t name_index = r.ReadU2();
            uint32_t attr_len = r.ReadU4();
            if (!r.ok)
                return -1;
            Reader attr_reader = r.Slice(attr_len);
            if (!r.ok)
                return -1;
            const uint8_t* attr_data = attr_reader.data;

            const char* attr_name = nullptr;
            if (name_index < utf8.size())
                attr_name = utf8[name_index].c_str();

            if (is_target && attr_name && strcmp(attr_name, "Code") == 0) {
                Reader cr(attr_data, attr_len);
                cr.ReadU2();                        // max_stack (discarded)
                uint16_t max_locals = cr.ReadU2();  // preserve original
                if (!cr.ok)
                    return -1;

                Writer cw;
                cw.WriteU2(0);           // max_stack
                cw.WriteU2(max_locals);  // max_locals
                cw.WriteU4(1);           // code_length
                cw.WriteU1(0xb1);        // return
                cw.WriteU2(0);           // exception_table_length
                cw.WriteU2(0);           // attributes_count

                w.WriteU2(name_index);
                w.WriteU4(static_cast<uint32_t>(cw.out.size()));
                w.WriteBytes(cw.out.data(), cw.out.size());
                ++patched;
            } else {
                w.WriteU2(name_index);
                w.WriteU4(attr_len);
                w.WriteBytes(attr_data, attr_len);
            }
        }
    }

    uint16_t class_attrs = r.ReadU2();
    if (!r.ok)
        return -1;
    w.WriteU2(class_attrs);
    for (uint16_t i = 0; i < class_attrs; ++i) {
        uint16_t name_index = r.ReadU2();
        uint32_t attr_len = r.ReadU4();
        if (!r.ok)
            return -1;
        const uint8_t* bytes = r.ReadBytes(attr_len);
        if (!r.ok)
            return -1;
        w.WriteU2(name_index);
        w.WriteU4(attr_len);
        w.WriteBytes(bytes, attr_len);
    }

    if (patched == 0)
        return 0;
    out = std::move(w.out);
    return patched;
}

static void JNICALL onClassFileLoadHook(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jclass class_being_redefined,
                                        jobject loader, const char* name, jobject protection_domain,
                                        jint class_data_len, const unsigned char* class_data, jint* new_class_data_len,
                                        unsigned char** new_class_data) {
    (void)loader;
    (void)protection_domain;

    if (!name)
        return;

    // Hide vanilla nametags: no-op renderName in RenderPlayer + RendererLivingEntity.
    // When the flag is off (incl. a retransform to restore), this branch is skipped
    // and the original bytes are kept.
    if (g_hideVanillaNames.load() &&
        (strcmp(name, kRenderPlayerSlash().c_str()) == 0 || strcmp(name, kRendererLivingSlash().c_str()) == 0 ||
         strcmp(name, kRenderArmorStandSlash().c_str()) == 0)) {
        std::vector<uint8_t> patched;
        int n = patchRenderNameNoop(class_data, static_cast<size_t>(class_data_len), patched);
        if (n > 0) {
            unsigned char* buffer = nullptr;
            jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
            if (err == JVMTI_ERROR_NONE && buffer) {
                memcpy(buffer, patched.data(), patched.size());
                *new_class_data_len = static_cast<jint>(patched.size());
                *new_class_data = buffer;
                log_ok("[PacketInterceptor] Hid vanilla nametags in %s (%d method(s))", name, n);
            }
        }
        return;
    }

    // Handle NetworkManager hooks (outgoing/incoming)
    if (strcmp(name, kTargetClassSlash().c_str()) == 0 && (g_targets.outgoing || g_targets.incoming)) {
        COBBLE_TRACE("onClassFileLoadHook: NetworkManager matched");
        log_debug("[PacketInterceptor] onClassFileLoadHook triggered for NetworkManager");
        std::vector<uint8_t> patched;
        int patched_methods = patchNetworkManagerClass(class_data, static_cast<size_t>(class_data_len), patched);
        if (patched_methods <= 0) {
            if (patched_methods < 0) {
                COBBLE_TRACE("onClassFileLoadHook: NetworkManager patching failed");
                log_warn("[PacketInterceptor] Bytecode patch failed for %s", name);
            }
            return;
        }

        unsigned char* buffer = nullptr;
        jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
        if (err != JVMTI_ERROR_NONE || !buffer) {
            COBBLE_TRACE("onClassFileLoadHook: NetworkManager allocation failed");
            log_warn("[PacketInterceptor] JVMTI Allocate failed for patched class: %d", err);
            return;
        }

        memcpy(buffer, patched.data(), patched.size());
        *new_class_data_len = static_cast<jint>(patched.size());
        *new_class_data = buffer;

        static std::atomic<bool> logged{false};
        if (!logged.exchange(true)) {
            COBBLE_TRACE("onClassFileLoadHook: NetworkManager successfully patched");
            log_ok("[PacketInterceptor] Patched NetworkManager.sendPacket bytecode (%d method(s))", patched_methods);
        } else {
            log_debug("[PacketInterceptor] Patched NetworkManager.sendPacket bytecode (%d method(s))", patched_methods);
        }
        return;
    }

    // Hook LCNetHandlerClient methods for Lunar nametag packet interception
    if (g_targets.lunarNametags && strcmp(name, kLunarNetHandlerSlash().c_str()) == 0) {
        log_debug("[PacketInterceptor] LCNetHandlerClient loaded - patching nametag handlers");
        std::vector<uint8_t> patched;
        int patched_methods = patchNetworkManagerClass(class_data, static_cast<size_t>(class_data_len), patched);

        // Even if patchNetworkManagerClass doesn't patch (different method names), we still try
        // The function will find methods matching handleNametagsUpdate pattern
        if (patched_methods > 0) {
            unsigned char* buffer = nullptr;
            jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
            if (err == JVMTI_ERROR_NONE && buffer) {
                memcpy(buffer, patched.data(), patched.size());
                *new_class_data_len = static_cast<jint>(patched.size());
                *new_class_data = buffer;
                log_ok("[PacketInterceptor] Patched LCNetHandlerClient nametag handlers (%d method(s))",
                       patched_methods);
            }
        } else {
            // Try patching with observer hook for the specific methods
            log_debug("[PacketInterceptor] LCNetHandlerClient: no methods patched by default patcher");
        }
        return;
    }

    // Backtrack: patch processPacket on the S14/S18 packet classes (small, un-mixined).
    if (g_targets.backtrack &&
        (strcmp(name, kS14ClassSlash().c_str()) == 0 || strcmp(name, kS18ClassSlash().c_str()) == 0)) {
        std::vector<uint8_t> patched;
        g_btPacketClass.store(true, std::memory_order_relaxed);
        int patched_methods = 0;
        try {
            patched_methods = patchNetworkManagerClass(class_data, static_cast<size_t>(class_data_len), patched);
        } catch (...) {
            patched_methods = -1;
        }
        g_btPacketClass.store(false, std::memory_order_relaxed);
        if (patched_methods > 0) {
            unsigned char* buffer = nullptr;
            jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
            if (err == JVMTI_ERROR_NONE && buffer) {
                memcpy(buffer, patched.data(), patched.size());
                *new_class_data_len = static_cast<jint>(patched.size());
                *new_class_data = buffer;
                log_ok("[PacketInterceptor] Patched backtrack packet class %s (%d method(s))", name, patched_methods);
            }
        }
        return;
    }

    // Hook NetHandlerPlayClient.handleCustomPayload for incoming Lunar packets
    if ((g_targets.customPayload || g_targets.teams) && strcmp(name, kIncomingTargetClassSlash().c_str()) == 0) {
        log_debug("[PacketInterceptor] NetHandlerPlayClient loaded - patching handleCustomPayload");
        std::vector<uint8_t> patched;
        int patched_methods = 0;
        try {
            patched_methods = patchNetworkManagerClass(class_data, static_cast<size_t>(class_data_len), patched);
        } catch (...) {
            COBBLE_TRACE("patchNetworkManagerClass: exception caught — aborting patch");
            patched_methods = -1;
        }

        if (patched_methods > 0) {
            unsigned char* buffer = nullptr;
            jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
            if (err == JVMTI_ERROR_NONE && buffer) {
                memcpy(buffer, patched.data(), patched.size());
                *new_class_data_len = static_cast<jint>(patched.size());
                *new_class_data = buffer;
                log_ok("[PacketInterceptor] Patched NetHandlerPlayClient.handleCustomPayload (%d method(s))",
                       patched_methods);
            }
        } else if (patched_methods == 0) {
            log_debug("[PacketInterceptor] NetHandlerPlayClient: no methods patched (Lunar mixin class or no targets)");
        } else {
            log_warn("[PacketInterceptor] NetHandlerPlayClient: patch aborted (error/exception)");
        }
        return;
    }

    const bool isSlowdownTarget =
        g_targets.slowdown && ((cls_SlowdownTarget && class_being_redefined &&
                                jni_env->IsSameObject(class_being_redefined, cls_SlowdownTarget) == JNI_TRUE) ||
                               strcmp(name, "net/minecraft/entity/EntityLivingBase") == 0);
    if (isSlowdownTarget) {
        COBBLE_TRACE("onClassFileLoadHook: EntityLivingBase matched");
        log_debug("[PacketInterceptor] onClassFileLoadHook triggered for living base: %s", name);
        std::vector<uint8_t> patched;
        int patched_methods = patchLivingBaseClass(class_data, static_cast<size_t>(class_data_len), patched);
        if (patched_methods > 0) {
            unsigned char* buffer = nullptr;
            jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
            if (err == JVMTI_ERROR_NONE && buffer) {
                memcpy(buffer, patched.data(), patched.size());
                *new_class_data_len = static_cast<jint>(patched.size());
                *new_class_data = buffer;
                COBBLE_TRACE("onClassFileLoadHook: EntityLivingBase successfully patched");
                log_ok("[PacketInterceptor] Patched living update bytecode (%d method(s))", patched_methods);
            } else {
                COBBLE_TRACE("onClassFileLoadHook: EntityLivingBase allocation failed");
            }
        } else {
            COBBLE_TRACE("onClassFileLoadHook: EntityLivingBase patching failed or returned 0");
        }
        return;
    }

    // Backtrack 3D ghost: inject the render hook into RenderGlobal.renderEntities.
    if (g_targets.renderGhost && strcmp(name, "net/minecraft/client/renderer/RenderGlobal") == 0) {
        COBBLE_TRACE("onClassFileLoadHook: RenderGlobal matched");
        std::vector<uint8_t> patched;
        int patched_methods = patchRenderGlobalClass(class_data, static_cast<size_t>(class_data_len), patched);
        if (patched_methods > 0) {
            unsigned char* buffer = nullptr;
            jvmtiError err = jvmti_env->Allocate(static_cast<jint>(patched.size()), &buffer);
            if (err == JVMTI_ERROR_NONE && buffer) {
                memcpy(buffer, patched.data(), patched.size());
                *new_class_data_len = static_cast<jint>(patched.size());
                *new_class_data = buffer;
                log_ok("[PacketInterceptor] Patched RenderGlobal.renderEntities bytecode (%d method(s))",
                       patched_methods);
            }
        } else {
            COBBLE_TRACE("onClassFileLoadHook: RenderGlobal patch failed or returned 0");
        }
        return;
    }

    // Debug: Log Lunar-related classes
    if (strstr(name, OBFUSCATE_STRING("lunar").c_str()) != nullptr ||
        strstr(name, OBFUSCATE_STRING("apollo").c_str()) != nullptr) {
        // log_debug("[PacketInterceptor] Lunar class loaded: %s", name);
    }
}

}  // namespace

void SetTargets(const HookTargets& targets) {
    g_targets = targets;
}

static std::atomic<bool> g_hideVanillaDirty{false};

void SetHideVanillaNames(bool on) {
    bool prev = g_hideVanillaNames.exchange(on);
    if (prev != on)
        g_hideVanillaDirty.store(true);
}

// Force the render classes to reload through onClassFileLoadHook so a runtime
// toggle takes effect (or restores vanilla). No-op unless the flag changed.
// MUST be called from a JVM-attached thread (the module update loop, not render).
bool ApplyHideVanillaNames(JNIEnv* env) {
    if (!g_hideVanillaDirty.exchange(false))
        return true;  // nothing changed
    if (!env || !Java::tiEnv || !bytecodeHookInstalled.load())
        return false;

    jclass classes[3];
    jint count = 0;
    jclass rp = nullptr;
    if (Java::AssignClass(kRenderPlayerDot(), rp)) {
        classes[count++] = rp;
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    jclass rl = nullptr;
    if (Java::AssignClass(kRendererLivingDot(), rl)) {
        classes[count++] = rl;
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    jclass ra = nullptr;
    if (Java::AssignClass(kRenderArmorStandDot(), ra)) {
        classes[count++] = ra;
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    bool ok = false;
    if (count > 0) {
        jvmtiError err = Java::tiEnv->RetransformClasses(count, classes);
        ok = (err == JVMTI_ERROR_NONE);
        if (!ok)
            log_warn("[PacketInterceptor] Nametag retransform failed: %d", err);
        for (jint i = 0; i < count; ++i)
            env->DeleteLocalRef(classes[i]);
    }
    return ok;
}

static bool ensureHookClass(JNIEnv* env, jboolean(JNICALL* shouldSend)(JNIEnv*, jclass, jobject),
                            void(JNICALL* onReceive)(JNIEnv*, jclass, jobject)) {
    if (hooksRegistered.load())
        return true;

    if (!shouldSend || !onReceive) {
        log_err("[PacketInterceptor] Missing PacketHook native function pointers");
        return false;
    }

    jclass hook = nullptr;
    if (!Java::AssignClass(kHookClassDot(), hook)) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    if (!hook) {
        if (!Java::ClassLoader) {
            return false;
        }
        // Decoded only for the duration of DefineClass; zeroed before
        // returning so a memory scan after init can't lift the cleartext.
        std::vector<uint8_t> decoded = DecodeHookClassBytes();
        hook = env->DefineClass(kHookClassSlash().c_str(), Java::ClassLoader,
                                reinterpret_cast<const jbyte*>(decoded.data()), static_cast<jint>(decoded.size()));
        SecureZeroMemory(decoded.data(), decoded.size());
        if (!hook) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            if (Java::AssignClass(kHookClassDot(), hook)) {
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            }
            if (!hook) {
                return false;
            }
        }
    }

    JNINativeMethod methods[2];
    std::string _hmn = kHookMethodName();
    std::string _hmd = kHookMethodDesc();
    std::string _orn = kOnReceiveMethodName();
    std::string _ord = kOnReceiveMethodDesc();
    methods[0].name = const_cast<char*>(_hmn.c_str());
    methods[0].signature = const_cast<char*>(_hmd.c_str());
    methods[0].fnPtr = reinterpret_cast<void*>(shouldSend);

    methods[1].name = const_cast<char*>(_orn.c_str());
    methods[1].signature = const_cast<char*>(_ord.c_str());
    methods[1].fnPtr = reinterpret_cast<void*>(onReceive);

    if (env->RegisterNatives(hook, methods, 2) != 0) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        log_err("[PacketInterceptor] Failed to register PacketHook natives");
        env->DeleteLocalRef(hook);
        return false;
    }

    cls_PacketHook = (jclass)env->NewGlobalRef(hook);
    env->DeleteLocalRef(hook);
    hooksRegistered = true;
    log_ok("[PacketInterceptor] PacketHook native bridge ready (shouldSend + onReceive)");
    return true;
}

bool RegisterHooks(JNIEnv* env, jboolean(JNICALL* shouldSend)(JNIEnv*, jclass, jobject),
                   void(JNICALL* onReceive)(JNIEnv*, jclass, jobject)) {
    if (!env)
        return false;
    if (hooksRegistered.load())
        return true;
    return ensureHookClass(env, shouldSend, onReceive);
}

bool InstallBytecodeHook(JNIEnv* env) {
    if (bytecodeHookInstalled.load())
        return true;
    if (!Java::tiEnv || !env)
        return false;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &onClassFileLoadHook;

    jvmtiError err = Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        return false;
    }

    err = Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    if (err != JVMTI_ERROR_NONE) {
        return false;
    }

    bytecodeHookInstalled = true;

    if (g_targets.outgoing || g_targets.incoming) {
        jclass nm = nullptr;
        if (Java::AssignClass(kTargetClassDot(), nm)) {
            jclass classes[1] = {nm};
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
    }

    if (g_targets.lunarNametags) {
        jclass lunarHandler = nullptr;
        if (Java::AssignClass(OBFUSCATE_STRING("com.lunarclient.bukkitapi.nethandler.client.LCNetHandlerClient"),
                              lunarHandler)) {
            jclass classes[1] = {lunarHandler};
            err = Java::tiEnv->RetransformClasses(1, classes);
            if (err == JVMTI_ERROR_NONE) {
                log_ok("[PacketInterceptor] Retransform requested for LCNetHandlerClient");
            } else {
                log_debug("[PacketInterceptor] LCNetHandlerClient retransform failed: %d (may not be loaded yet)", err);
            }
            env->DeleteLocalRef(lunarHandler);
        } else {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            log_debug("[PacketInterceptor] LCNetHandlerClient not available for retransform");
        }
    }

    if (g_targets.customPayload || g_targets.teams) {
        jclass netHandler = nullptr;
        if (Java::AssignClass(kIncomingTargetClassDot(), netHandler)) {
            jclass classes[1] = {netHandler};
            err = Java::tiEnv->RetransformClasses(1, classes);
            if (err == JVMTI_ERROR_NONE) {
                log_ok("[PacketInterceptor] Retransform requested for NetHandlerPlayClient");
            } else {
                log_debug("[PacketInterceptor] NetHandlerPlayClient retransform failed: %d", err);
            }
            env->DeleteLocalRef(netHandler);
        } else {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            log_debug("[PacketInterceptor] NetHandlerPlayClient not available for retransform");
        }
    }

    if (g_targets.slowdown) {
        jclass livingBaseCls = nullptr;
        if (Java::AssignClass(OBFUSCATE_STRING("net.minecraft.entity.EntityLivingBase"), livingBaseCls)) {
            if (cls_SlowdownTarget) {
                env->DeleteGlobalRef(cls_SlowdownTarget);
            }
            cls_SlowdownTarget = static_cast<jclass>(env->NewGlobalRef(livingBaseCls));

            jclass classes[1] = {livingBaseCls};
            err = Java::tiEnv->RetransformClasses(1, classes);
            if (err == JVMTI_ERROR_NONE) {
                log_ok("[PacketInterceptor] Retransform requested for EntityLivingBase");
            } else {
                log_warn("[PacketInterceptor] EntityLivingBase retransform failed: %d", err);
            }
            env->DeleteLocalRef(livingBaseCls);
        } else {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            log_warn("[PacketInterceptor] EntityLivingBase not available for retransform");
        }
    }

    if (g_targets.renderGhost) {
        jclass renderGlobalCls = nullptr;
        if (Java::AssignClass(OBFUSCATE_STRING("net.minecraft.client.renderer.RenderGlobal"), renderGlobalCls)) {
            jclass classes[1] = {renderGlobalCls};
            err = Java::tiEnv->RetransformClasses(1, classes);
            if (err == JVMTI_ERROR_NONE) {
                log_ok("[PacketInterceptor] Retransform requested for RenderGlobal");
            } else {
                log_warn("[PacketInterceptor] RenderGlobal retransform failed: %d", err);
            }
            env->DeleteLocalRef(renderGlobalCls);
        } else {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            log_warn("[PacketInterceptor] RenderGlobal not available for retransform");
        }
    }

    if (g_targets.backtrack) {
        const char* btDots[2] = {"net.minecraft.network.play.server.S14PacketEntity",
                                 "net.minecraft.network.play.server.S18PacketEntityTeleport"};
        for (const char* dot : btDots) {
            jclass btCls = nullptr;
            if (Java::AssignClass(dot, btCls)) {
                jclass classes[1] = {btCls};
                err = Java::tiEnv->RetransformClasses(1, classes);
                if (err == JVMTI_ERROR_NONE)
                    log_ok("[PacketInterceptor] Retransform requested for backtrack packet class %s", dot);
                else
                    log_warn("[PacketInterceptor] Backtrack packet class retransform failed: %d", err);
                env->DeleteLocalRef(btCls);
            } else {
                if (env->ExceptionCheck())
                    env->ExceptionClear();
                log_debug("[PacketInterceptor] Backtrack packet class %s not loaded yet (will patch on load)", dot);
            }
        }
    }

    return true;
}

bool Shutdown(JNIEnv* env) {
    if (!env)
        return false;

    bool retransformSuccess = false;

    if (Java::tiEnv && bytecodeHookInstalled.load()) {
        Java::tiEnv->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);

        jvmtiEventCallbacks callbacks;
        memset(&callbacks, 0, sizeof(callbacks));
        Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));

        jclass classesToRetransform[4];
        jint count = 0;

        jclass nm = nullptr;
        if (Java::AssignClass(kTargetClassDot(), nm)) {
            classesToRetransform[count++] = nm;
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        jclass lunarHandler = nullptr;
        if (Java::AssignClass(OBFUSCATE_STRING("com.lunarclient.bukkitapi.nethandler.client.LCNetHandlerClient"),
                              lunarHandler)) {
            classesToRetransform[count++] = lunarHandler;
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        jclass netHandler = nullptr;
        if (Java::AssignClass(kIncomingTargetClassDot(), netHandler)) {
            classesToRetransform[count++] = netHandler;
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        jclass livingBaseCls = nullptr;
        if (Java::AssignClass(OBFUSCATE_STRING("net.minecraft.entity.EntityLivingBase"), livingBaseCls)) {
            classesToRetransform[count++] = livingBaseCls;
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        if (count > 0) {
            jvmtiError err = Java::tiEnv->RetransformClasses(count, classesToRetransform);
            if (err == JVMTI_ERROR_NONE) {
                retransformSuccess = true;
                log_ok("[BytecodePatcher] Successfully retransformed %d classes", count);
            } else {
                char errBuf[256];
                wsprintfA(errBuf, "[BytecodePatcher] Failed to retransform classes (err=%d)", err);
                log_err("%s", errBuf);
            }
            for (jint i = 0; i < count; i++) {
                env->DeleteLocalRef(classesToRetransform[i]);
            }
        } else {
            retransformSuccess = true;
        }
    } else {
        retransformSuccess = true;
    }

    if (cls_PacketHook) {
        if (retransformSuccess) {
            env->UnregisterNatives(cls_PacketHook);
            log_ok("[BytecodePatcher] Unregistered PacketHook natives");
        } else {
            log_warn("[BytecodePatcher] Skipped unregistering PacketHook natives to prevent crash");
        }
        env->DeleteGlobalRef(cls_PacketHook);
        cls_PacketHook = nullptr;
    }
    if (cls_SlowdownTarget) {
        env->DeleteGlobalRef(cls_SlowdownTarget);
        cls_SlowdownTarget = nullptr;
    }

    hooksRegistered = false;
    bytecodeHookInstalled = false;

    return retransformSuccess;
}

}  // namespace BytecodePatcher
