// WARNING: JVMTI + bytecode patching zone.
// Touch only if strictly necessary. Easy to break.

#include "packet_interceptor.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../module/utility/noitemrelease.h"
#include "../sdk/core/minecraft.h"
#include "../sdk/network/lunar_packet_listener.hpp"
#include "../util/logger.hpp"
#include "../util/team/friends_manager.h"

// ============================================================================
// PacketInterceptor Implementation
//
// This uses JVMTI to intercept sendPacket calls. The approach:
// 1. Cache classes and methods for C03PacketPlayer detection
// 2. Bytecode-patch NetworkManager.sendPacket to call a native gate
// 3. Queue movement packets for a delayed, continuous server timeline
// ============================================================================

namespace {

constexpr const char* kTargetClassSlash = "net/minecraft/network/NetworkManager";
constexpr const char* kTargetClassDot = "net.minecraft.network.NetworkManager";
constexpr const char* kHookClassSlash = "com/cobble/agent/PacketHook";
constexpr const char* kHookClassDot = "com.cobble.agent.PacketHook";
constexpr const char* kHookMethodName = "shouldSend";
constexpr const char* kHookMethodDesc = "(Ljava/lang/Object;)Z";
constexpr int kPrefixLen = 8;

// Incoming packet hook for plugin messages (Lunar API)
constexpr const char* kIncomingTargetClassSlash = "net/minecraft/client/network/NetHandlerPlayClient";
constexpr const char* kIncomingTargetClassDot = "net.minecraft.client.network.NetHandlerPlayClient";
constexpr const char* kHandleCustomPayload = "handleCustomPayload";  // S3FPacketCustomPayload
constexpr const char* kHandleTeams = "handleTeams";                  // S3EPacketTeams (scoreboard teams)
constexpr const char* kIncomingHookClassSlash = "com/cobble/agent/PluginMsgHook";
constexpr const char* kIncomingHookClassDot = "com.cobble.agent.PluginMsgHook";
constexpr const char* kIncomingHookMethodName = "onPluginMessage";
constexpr const char* kIncomingHookMethodDesc = "(Ljava/lang/Object;)V";

// Incoming packet hook via channelRead0
constexpr const char* kIncomingPacketHookClassSlash = "com/cobble/agent/IncomingPacketHook";
constexpr const char* kIncomingPacketHookClassDot = "com.cobble.agent.IncomingPacketHook";
constexpr const char* kIncomingPacketHookMethodName = "onReceive";
constexpr const char* kIncomingPacketHookMethodDesc = "(Ljava/lang/Object;)V";

// Lunar Client nametag handler - this is where nametag packets are processed
constexpr const char* kLunarNetHandlerSlash = "com/lunarclient/bukkitapi/nethandler/client/LCNetHandlerClient";
constexpr const char* kLunarHandleNametagsUpdate = "handleNametagsUpdate";
constexpr const char* kLunarHandleOverrideNametags = "handleOverrideNametags";
// Incoming method descriptor for onReceive(Object)V
constexpr const char* kOnReceiveMethodName = "onReceive";
constexpr const char* kOnReceiveMethodDesc = "(Ljava/lang/Object;)V";

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
static const unsigned char kPacketHookClassBytes[] = {
    // Magic + Version
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x34,
    // Constant pool count = 14 (0x000e)
    0x00, 0x0e,
    // Constant pool entries:
    // #1 = Utf8 "com/cobble/agent/PacketHook"
    0x01, 0x00, 0x1b, 0x63, 0x6f, 0x6d, 0x2f, 0x63, 0x6f, 0x62, 0x62, 0x6c, 0x65, 0x2f, 0x61, 0x67, 0x65, 0x6e, 0x74,
    0x2f, 0x50, 0x61, 0x63, 0x6b, 0x65, 0x74, 0x48, 0x6f, 0x6f, 0x6b,
    // #2 = Class -> #1
    0x07, 0x00, 0x01,
    // #3 = Utf8 "java/lang/Object"
    0x01, 0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74,
    // #4 = Class -> #3
    0x07, 0x00, 0x03,
    // #5 = Utf8 "<init>"
    0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e, 0x69, 0x74, 0x3e,
    // #6 = Utf8 "()V"
    0x01, 0x00, 0x03, 0x28, 0x29, 0x56,
    // #7 = NameAndType -> #5, #6

    0x0c, 0x00, 0x05, 0x00, 0x06,
    // #8 = Methodref -> #4, #7
    0x0a, 0x00, 0x04, 0x00, 0x07,
    // #9 = Utf8 "Code"
    0x01, 0x00, 0x04, 0x43, 0x6f, 0x64, 0x65,
    // #10 = Utf8 "shouldSend"
    0x01, 0x00, 0x0a, 0x73, 0x68, 0x6f, 0x75, 0x6c, 0x64, 0x53, 0x65, 0x6e, 0x64,
    // #11 = Utf8 "(Ljava/lang/Object;)Z"
    0x01, 0x00, 0x15, 0x28, 0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a, 0x65,
    0x63, 0x74, 0x3b, 0x29, 0x5a,
    // #12 = Utf8 "onReceive"
    0x01, 0x00, 0x09, 0x6f, 0x6e, 0x52, 0x65, 0x63, 0x65, 0x69, 0x76, 0x65,
    // #13 = Utf8 "(Ljava/lang/Object;)V"
    0x01, 0x00, 0x15, 0x28, 0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a, 0x65,
    0x63, 0x74, 0x3b, 0x29, 0x56,
    // Access flags (public) = 0x0031 (ACC_PUBLIC | ACC_SUPER)
    0x00, 0x31,
    // This class = #2
    0x00, 0x02,
    // Super class = #4
    0x00, 0x04,
    // Interfaces count = 0
    0x00, 0x00,
    // Fields count = 0
    0x00, 0x00,
    // Methods count = 3
    0x00, 0x03,
    // Method 1: <init>()V - constructor (ACC_PUBLIC = 0x0001)
    0x00, 0x01, 0x00, 0x05, 0x00, 0x06,
    // Attributes count = 1
    0x00, 0x01,
    // Code attribute
    0x00, 0x09,              // name index -> #9 "Code"
    0x00, 0x00, 0x00, 0x11,  // length = 17
    0x00, 0x01,              // max_stack = 1
    0x00, 0x01,              // max_locals = 1
    0x00, 0x00, 0x00, 0x05,  // code_length = 5
    0x2a,                    // aload_0
    0xb7, 0x00, 0x08,        // invokespecial #8 (Object.<init>)
    0xb1,                    // return
    0x00, 0x00,              // exception table length = 0
    0x00, 0x00,              // attributes count = 0
    // Method 2: shouldSend(Object)Z - native (ACC_PUBLIC | ACC_STATIC | ACC_NATIVE = 0x0109)
    0x01, 0x09, 0x00, 0x0a, 0x00, 0x0b,
    // Attributes count = 0
    0x00, 0x00,
    // Method 3: onReceive(Object)V - native (ACC_PUBLIC | ACC_STATIC | ACC_NATIVE = 0x0109)
    0x01, 0x09, 0x00, 0x0c, 0x00, 0x0d,
    // Attributes count = 0
    0x00, 0x00,
    // Class attributes count = 0
    0x00, 0x00};

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
    return cls == kHookClassSlash && name == kHookMethodName && desc == kHookMethodDesc;
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

    if (cp_count > 0xFFF3)
        return -1;  // Need room for 12 new entries (6 for each method)
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

    Writer w;
    w.WriteU4(magic);
    w.WriteU2(minor);
    w.WriteU2(major);
    w.WriteU2(static_cast<uint16_t>(cp_count + 10));  // 6 for shouldSend + 4 for onReceive (shares class)
    w.WriteBytes(cp_raw.data(), cp_raw.size());

    // shouldSend entries
    w.WriteU1(1);  // CONSTANT_Utf8
    w.WriteU2(static_cast<uint16_t>(strlen(kHookClassSlash)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kHookClassSlash), strlen(kHookClassSlash));
    w.WriteU1(7);  // CONSTANT_Class
    w.WriteU2(idx_utf8_class);
    w.WriteU1(1);  // CONSTANT_Utf8
    w.WriteU2(static_cast<uint16_t>(strlen(kHookMethodName)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kHookMethodName), strlen(kHookMethodName));
    w.WriteU1(1);  // CONSTANT_Utf8
    w.WriteU2(static_cast<uint16_t>(strlen(kHookMethodDesc)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kHookMethodDesc), strlen(kHookMethodDesc));
    w.WriteU1(12);  // CONSTANT_NameAndType
    w.WriteU2(idx_utf8_name);
    w.WriteU2(idx_utf8_desc);
    w.WriteU1(10);  // CONSTANT_Methodref
    w.WriteU2(idx_class);
    w.WriteU2(idx_name_type);

    // onReceive entries (reuses idx_class)
    w.WriteU1(1);  // CONSTANT_Utf8
    w.WriteU2(static_cast<uint16_t>(strlen(kOnReceiveMethodName)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kOnReceiveMethodName), strlen(kOnReceiveMethodName));
    w.WriteU1(1);  // CONSTANT_Utf8
    w.WriteU2(static_cast<uint16_t>(strlen(kOnReceiveMethodDesc)));
    w.WriteBytes(reinterpret_cast<const uint8_t*>(kOnReceiveMethodDesc), strlen(kOnReceiveMethodDesc));
    w.WriteU1(12);  // CONSTANT_NameAndType
    w.WriteU2(idx_utf8_recv_name);
    w.WriteU2(idx_utf8_recv_desc);
    w.WriteU1(10);  // CONSTANT_Methodref
    w.WriteU2(idx_class);
    w.WriteU2(idx_recv_name_type);

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
        bool is_incoming_target = false;
        int incoming_packet_slot = 2;  // Default: second param for channelRead0
        if (m_name < utf8.size() && m_desc < utf8.size()) {
            const std::string& name = utf8[m_name];
            const std::string& desc = utf8[m_desc];

            // Log all methods for debugging
            static bool loggedMethods = false;
            if (!loggedMethods && name.length() > 0) {
                log_debug("[PacketInterceptor] NetworkManager method: %s %s", name.c_str(), desc.c_str());
            }

            if ((name == "sendPacket" || name == "scheduleOutboundPacket") && desc.size() >= 3 && desc.back() == 'V' &&
                desc[desc.size() - 2] == ')') {
                // Accept either full MCP name or obfuscated single-letter class name
                if (desc.rfind("(Lnet/minecraft/network/Packet;", 0) == 0) {
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
            if (name == "channelRead0") {
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
            if (name == kLunarHandleNametagsUpdate || name == kLunarHandleOverrideNametags) {
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
            if (name == kHandleCustomPayload) {
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
            if (name == kHandleTeams) {
                if (desc.back() == 'V' && desc.find(';') != std::string::npos) {
                    is_incoming_target = true;
                    incoming_packet_slot = 1;
                    log_debug("[PacketInterceptor] Found handleTeams: %s (access=0x%04X)", desc.c_str(), m_access);
                }
            }

            // Mark that we've logged methods
            if (!loggedMethods && i == methods_count - 1) {
                loggedMethods = true;
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

            // Patch outgoing packets (sendPacket) - blocking hook
            if (is_target && attr_name && strcmp(attr_name, "Code") == 0 && (m_access & (0x0100 | 0x0400)) == 0) {
                bool is_static = (m_access & 0x0008) != 0;
                if (tryPatchCodeAttribute(attr_data, attr_len, cp, utf8, idx_methodref, is_static, patched_attr)) {
                    w.WriteU2(name_index);
                    w.WriteU4(static_cast<uint32_t>(patched_attr.size()));
                    w.WriteBytes(patched_attr.data(), patched_attr.size());
                    patched = true;
                    ++patched_methods;
                }
            }

            // Patch incoming packets (channelRead0) - observer hook (non-blocking)
            if (!patched && is_incoming_target && attr_name && strcmp(attr_name, "Code") == 0 &&
                (m_access & (0x0100 | 0x0400)) == 0) {
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
                } else {
                    log_debug("[PacketInterceptor] Observer patch FAILED for: %s", method_name.c_str());
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

static void JNICALL onClassFileLoadHook(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jclass class_being_redefined,
                                        jobject loader, const char* name, jobject protection_domain,
                                        jint class_data_len, const unsigned char* class_data, jint* new_class_data_len,
                                        unsigned char** new_class_data) {
    (void)jni_env;
    (void)class_being_redefined;
    (void)loader;
    (void)protection_domain;

    if (!name)
        return;

    // Handle NetworkManager.sendPacket (outgoing packets)
    if (strcmp(name, kTargetClassSlash) == 0) {
        log_debug("[PacketInterceptor] onClassFileLoadHook triggered for NetworkManager");
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
        return;
    }

    // Hook LCNetHandlerClient methods for Lunar nametag packet interception
    if (strcmp(name, kLunarNetHandlerSlash) == 0) {
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

    // Hook NetHandlerPlayClient.handleCustomPayload for incoming Lunar packets
    if (strcmp(name, kIncomingTargetClassSlash) == 0) {
        log_debug("[PacketInterceptor] NetHandlerPlayClient loaded - patching handleCustomPayload");
        std::vector<uint8_t> patched;
        int patched_methods = patchNetworkManagerClass(class_data, static_cast<size_t>(class_data_len), patched);

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
        } else {
            log_debug("[PacketInterceptor] NetHandlerPlayClient: no methods patched");
        }
        return;
    }

    // Debug: Log Lunar-related classes
    if (strstr(name, "lunar") != nullptr || strstr(name, "apollo") != nullptr) {
        // log_debug("[PacketInterceptor] Lunar class loaded: %s", name);
    }
}

}  // namespace

namespace PacketInterceptor {

// State
static std::atomic<bool> initialized{false};
static std::atomic<bool> blinkActive{false};
static std::atomic<bool> hybridIntercept{false};
static std::atomic<bool> hybridRelease{false};

// Debug: set to true to log all intercepted packets
static std::atomic<bool> debugPacketLogging{true};

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

struct PacketPos {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct QueuedPacket {
    jobject packet = nullptr;
    PacketPos pos{};
    bool hasPos = false;
};

// Packet queue for Hybrid Blink
static std::vector<QueuedPacket> packetQueue;
static std::mutex queueMutex;

static std::mutex shadowMutex;
static PacketPos lastSentPos{};
static bool haveLastSent = false;
static PacketPos shadowPos{};
static bool shadowInitialized = false;
static std::deque<PacketPos> desiredQueue;
static std::mutex desiredMutex;

static std::chrono::steady_clock::time_point lastHybridSend{};

static jfieldID fid_pkt_x = nullptr;
static jfieldID fid_pkt_y = nullptr;
static jfieldID fid_pkt_z = nullptr;
static std::atomic<bool> packetFieldsReady{false};
static bool packetFieldsLogged = false;
static bool packetFieldsWarned = false;
static std::mutex packetFieldMutex;

constexpr int kHybridIntervalMs = 50;
constexpr int kHybridDelayTicks = 6;
constexpr double kHybridMaxSkipDist = 0.28;
constexpr double kHybridMaxSkipY = 0.5;
constexpr size_t kHybridMaxQueue = 200;

// NetworkManager instance cache
static jobject networkManagerInstance = nullptr;

static jfieldID getFieldIDSafe(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    jfieldID fid = env->GetFieldID(cls, name, sig);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return fid;
}

static jfieldID findFieldByNames(JNIEnv* env, jclass cls, const char* const* names, const char* sig) {
    for (int i = 0; names[i] != nullptr; ++i) {
        jfieldID fid = getFieldIDSafe(env, cls, names[i], sig);
        if (fid)
            return fid;
    }
    return nullptr;
}

static bool cachePacketFields(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(packetFieldMutex);
    if (packetFieldsReady.load())
        return true;
    if (!cls_C03PacketPlayer)
        return false;

    const char* xNames[] = {"x", "field_149479_a", nullptr};
    const char* yNames[] = {"y", "field_149477_b", nullptr};
    const char* zNames[] = {"z", "field_149478_c", nullptr};

    fid_pkt_x = findFieldByNames(env, cls_C03PacketPlayer, xNames, "D");
    fid_pkt_y = findFieldByNames(env, cls_C03PacketPlayer, yNames, "D");
    fid_pkt_z = findFieldByNames(env, cls_C03PacketPlayer, zNames, "D");

    if ((!fid_pkt_x || !fid_pkt_y || !fid_pkt_z) && Java::tiEnv) {
        jint fieldCount = 0;
        jfieldID* fields = nullptr;
        jvmtiError err = Java::tiEnv->GetClassFields(cls_C03PacketPlayer, &fieldCount, &fields);
        if (err == JVMTI_ERROR_NONE && fieldCount > 0) {
            std::vector<jfieldID> doubles;
            doubles.reserve(3);
            for (int i = 0; i < fieldCount; ++i) {
                char* name = nullptr;
                char* sig = nullptr;
                Java::tiEnv->GetFieldName(cls_C03PacketPlayer, fields[i], &name, &sig, nullptr);
                if (sig && sig[0] == 'D') {
                    doubles.push_back(fields[i]);
                }
                if (name)
                    Java::tiEnv->Deallocate((unsigned char*)name);
                if (sig)
                    Java::tiEnv->Deallocate((unsigned char*)sig);
            }

            if (doubles.size() >= 3) {
                fid_pkt_x = doubles[0];
                fid_pkt_y = doubles[1];
                fid_pkt_z = doubles[2];
            }
        }
        if (fields) {
            Java::tiEnv->Deallocate((unsigned char*)fields);
        }
    }

    packetFieldsReady.store((fid_pkt_x && fid_pkt_y && fid_pkt_z), std::memory_order_release);
    if (packetFieldsReady.load()) {
        if (!packetFieldsLogged) {
            // log_ok("[PacketInterceptor] Cached packet position fields for hybrid Blink");
            packetFieldsLogged = true;
        }
    } else if (!packetFieldsWarned) {
        // log_warn("[PacketInterceptor] Failed to cache packet fields - hybrid skip disabled");
        packetFieldsWarned = true;
    }

    return packetFieldsReady.load();
}

static bool readPacketPos(JNIEnv* env, jobject packet, PacketPos& out) {
    if (!packetFieldsReady.load() || !packet)
        return false;
    out.x = env->GetDoubleField(packet, fid_pkt_x);
    out.y = env->GetDoubleField(packet, fid_pkt_y);
    out.z = env->GetDoubleField(packet, fid_pkt_z);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return true;
}

static bool isSafeSkip(const PacketPos& from, const PacketPos& to) {
    const double dx = to.x - from.x;
    const double dz = to.z - from.z;
    const double dy = to.y - from.y;
    const double horiz = std::sqrt(dx * dx + dz * dz);
    return horiz <= kHybridMaxSkipDist && std::abs(dy) <= kHybridMaxSkipY;
}

static PacketPos stepToward(const PacketPos& from, const PacketPos& to, double maxStep, double maxStepY) {
    PacketPos out = from;
    double dx = to.x - from.x;
    double dz = to.z - from.z;
    double horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz > maxStep && horiz > 1e-6) {
        double scale = maxStep / horiz;
        dx *= scale;
        dz *= scale;
    }
    out.x += dx;
    out.z += dz;

    double dy = to.y - from.y;
    if (dy > maxStepY)
        dy = maxStepY;
    if (dy < -maxStepY)
        dy = -maxStepY;
    out.y += dy;
    return out;
}

static void writePacketPos(JNIEnv* env, jobject packet, const PacketPos& pos) {
    if (!packetFieldsReady.load())
        return;
    env->SetDoubleField(packet, fid_pkt_x, pos.x);
    env->SetDoubleField(packet, fid_pkt_y, pos.y);
    env->SetDoubleField(packet, fid_pkt_z, pos.z);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

static void clearDesiredQueue() {
    std::lock_guard<std::mutex> lock(desiredMutex);
    desiredQueue.clear();
}

static size_t getDesiredQueueCount() {
    std::lock_guard<std::mutex> lock(desiredMutex);
    return desiredQueue.size();
}

// ============================================================================
// Helper: Get NetworkManager instance from player
// ============================================================================
static jobject getNetworkManager(JNIEnv* env) {
    // log_debug("[PacketInterceptor] Getting NetworkManager instance...");

    // Path: thePlayer.sendQueue.netManager
    jobject player = mc.getPlayer();
    if (!player) {
        // log_warn("[PacketInterceptor] No player instance");
        return nullptr;
    }

    // Get EntityPlayerSP class
    jclass cls_EntityPlayerSP = nullptr;
    if (!Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", cls_EntityPlayerSP)) {
        // log_err("[PacketInterceptor] Failed to get EntityPlayerSP class");
        return nullptr;
    }

    // Get sendQueue field
    jfieldID fid_sendQueue =
        env->GetFieldID(cls_EntityPlayerSP, "sendQueue", "Lnet/minecraft/client/network/NetHandlerPlayClient;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        // log_err("[PacketInterceptor] Failed to get sendQueue field");
        env->DeleteLocalRef(cls_EntityPlayerSP);
        return nullptr;
    }

    jobject sendQueue = env->GetObjectField(player, fid_sendQueue);
    env->DeleteLocalRef(cls_EntityPlayerSP);

    if (!sendQueue) {
        // log_warn("[PacketInterceptor] sendQueue is null");
        return nullptr;
    }
    // log_debug("[PacketInterceptor] Got sendQueue: %p", sendQueue);

    // Get NetHandlerPlayClient class
    jclass cls_NetHandlerPlayClient = nullptr;
    if (!Java::AssignClass("net.minecraft.client.network.NetHandlerPlayClient", cls_NetHandlerPlayClient)) {
        // log_err("[PacketInterceptor] Failed to get NetHandlerPlayClient class");
        env->DeleteLocalRef(sendQueue);
        return nullptr;
    }

    // Get netManager field
    jfieldID fid_netManager =
        env->GetFieldID(cls_NetHandlerPlayClient, "netManager", "Lnet/minecraft/network/NetworkManager;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        // log_err("[PacketInterceptor] Failed to get netManager field");
        env->DeleteLocalRef(cls_NetHandlerPlayClient);
        env->DeleteLocalRef(sendQueue);
        return nullptr;
    }

    jobject netManager = env->GetObjectField(sendQueue, fid_netManager);
    env->DeleteLocalRef(cls_NetHandlerPlayClient);
    env->DeleteLocalRef(sendQueue);

    if (netManager) {
        // log_ok("[PacketInterceptor] Got NetworkManager: %p", netManager);
    } else {
        // log_warn("[PacketInterceptor] netManager is null");
    }

    return netManager;
}

// ============================================================================
// Initialize packet classes cache
// ============================================================================
static bool cachePacketClasses(JNIEnv* env) {
    // log_debug("[PacketInterceptor] Caching packet classes...");

    auto makeGlobal = [env](jclass localRef) -> jclass {
        if (!localRef)
            return nullptr;
        jclass globalRef = (jclass)env->NewGlobalRef(localRef);
        env->DeleteLocalRef(localRef);
        return globalRef;
    };

    // NetworkManager
    jclass localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.NetworkManager", localClass)) {
        // log_err("[PacketInterceptor] Failed to cache NetworkManager class");
        return false;
    }
    cls_NetworkManager = makeGlobal(localClass);
    if (!cls_NetworkManager) {
        // log_err("[PacketInterceptor] Failed to create global ref for NetworkManager");
        return false;
    }
    // log_ok("[PacketInterceptor] Cached NetworkManager class");

    // Base Packet class
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.Packet", localClass)) {
        // log_err("[PacketInterceptor] Failed to cache Packet class");
        return false;
    }
    cls_Packet = makeGlobal(localClass);
    if (!cls_Packet) {
        // log_err("[PacketInterceptor] Failed to create global ref for Packet");
        return false;
    }
    // log_ok("[PacketInterceptor] Cached Packet class");

    // C03PacketPlayer (base for movement)
    localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", localClass)) {
        // log_warn("[PacketInterceptor] Failed to cache C03PacketPlayer - trying alternative names");
        //  Try without full package
        cls_C03PacketPlayer = nullptr;
    } else {
        cls_C03PacketPlayer = makeGlobal(localClass);
        // log_ok("[PacketInterceptor] Cached C03PacketPlayer class");
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
        // log_ok("[PacketInterceptor] Cached C04PacketPlayerPosition class");
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
        // log_ok("[PacketInterceptor] Cached C05PacketPlayerLook class");
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
        // log_ok("[PacketInterceptor] Cached C06PacketPlayerPosLook class");
    }

    // Get sendPacket method
    mid_sendPacket = env->GetMethodID(cls_NetworkManager, "sendPacket", "(Lnet/minecraft/network/Packet;)V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        // log_warn("[PacketInterceptor] sendPacket(Packet) not found directly, trying alternatives...");

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
                        // log_debug("[PacketInterceptor] Found sendPacket variant: %s", sig);
                        //  Try to get it with this signature
                        mid_sendPacket = env->GetMethodID(cls_NetworkManager, "sendPacket", sig);
                        if (!env->ExceptionCheck() && mid_sendPacket) {
                            // log_ok("[PacketInterceptor] Using sendPacket%s", sig);
                            if (name)
                                Java::tiEnv->Deallocate((unsigned char*)name);
                            if (sig)
                                Java::tiEnv->Deallocate((unsigned char*)sig);
                            break;
                        }
                        env->ExceptionClear();
                    }
                    if (name)
                        Java::tiEnv->Deallocate((unsigned char*)name);
                    if (sig)
                        Java::tiEnv->Deallocate((unsigned char*)sig);
                }
                Java::tiEnv->Deallocate((unsigned char*)methods);
            }
        }
    } else {
        // log_ok("[PacketInterceptor] Cached sendPacket method");
    }

    return true;
}

static jboolean JNICALL nativeShouldSend(JNIEnv* env, jclass, jobject packet) {
    return shouldSendPacket(env, packet) ? JNI_TRUE : JNI_FALSE;
}

// Native callback for incoming packets (observer only, void return)
static void JNICALL nativeOnReceive(JNIEnv* env, jclass, jobject packet) {
    if (!packet)
        return;

    // Cached JNI references for S3FPacketCustomPayload
    static jclass cls_S3FPacketCustomPayload = nullptr;
    static jmethodID mid_getChannelName = nullptr;
    static jmethodID mid_getBufferData = nullptr;
    static jmethodID mid_getChannelName_legacy = nullptr;
    static jmethodID mid_getPayloadBytes_legacy = nullptr;
    static jclass cls_PacketBuffer = nullptr;
    static jmethodID mid_readableBytes = nullptr;
    static jmethodID mid_readBytes = nullptr;
    static jmethodID mid_array = nullptr;
    static jmethodID mid_readerIndex = nullptr;
    static bool useLegacyCustomPayload = false;
    static bool triedInit = false;

    if (!triedInit) {
        triedInit = true;
        jclass local = env->FindClass("net/minecraft/network/play/server/S3FPacketCustomPayload");
        if (local) {
            cls_S3FPacketCustomPayload = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            mid_getChannelName = env->GetMethodID(cls_S3FPacketCustomPayload, "getChannelName", "()Ljava/lang/String;");
            mid_getBufferData =
                env->GetMethodID(cls_S3FPacketCustomPayload, "getBufferData", "()Lnet/minecraft/network/PacketBuffer;");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                mid_getChannelName = nullptr;
                mid_getBufferData = nullptr;
            }
        }
        // 1.7.10 fallback: obfuscated methods returning byte[]
        if (cls_S3FPacketCustomPayload && (!mid_getChannelName || !mid_getBufferData)) {
            mid_getChannelName_legacy =
                env->GetMethodID(cls_S3FPacketCustomPayload, "func_149169_c", "()Ljava/lang/String;");
            mid_getPayloadBytes_legacy = env->GetMethodID(cls_S3FPacketCustomPayload, "func_149168_d", "()[B");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                mid_getChannelName_legacy = nullptr;
                mid_getPayloadBytes_legacy = nullptr;
            } else if (mid_getChannelName_legacy && mid_getPayloadBytes_legacy) {
                useLegacyCustomPayload = true;
            }
        }
        // PacketBuffer class for reading bytes
        jclass pbLocal = env->FindClass("net/minecraft/network/PacketBuffer");
        if (pbLocal) {
            cls_PacketBuffer = (jclass)env->NewGlobalRef(pbLocal);
            env->DeleteLocalRef(pbLocal);
            mid_readableBytes = env->GetMethodID(cls_PacketBuffer, "readableBytes", "()I");
            mid_readBytes = env->GetMethodID(cls_PacketBuffer, "readBytes", "(I)Lio/netty/buffer/ByteBuf;");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            mid_array = env->GetMethodID(cls_PacketBuffer, "array", "()[B");
            mid_readerIndex = env->GetMethodID(cls_PacketBuffer, "readerIndex", "()I");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                mid_array = nullptr;
                mid_readerIndex = nullptr;
            }
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    // If this is a CustomPayload packet, check for lunar:apollo channel
    if (cls_S3FPacketCustomPayload && env->IsInstanceOf(packet, cls_S3FPacketCustomPayload)) {
        jmethodID channelMethod = mid_getChannelName ? mid_getChannelName : mid_getChannelName_legacy;
        if (channelMethod) {
            jstring channelStr = (jstring)env->CallObjectMethod(packet, channelMethod);
            if (channelStr) {
                const char* channel = env->GetStringUTFChars(channelStr, nullptr);
                if (channel) {
                    // Only process lunar:apollo packets
                    if (strcmp(channel, "lunar:apollo") == 0) {
                        jbyteArray byteArray = nullptr;
                        jbyte* bytes = nullptr;
                        const uint8_t* data = nullptr;
                        int len = 0;
                        jobject packetBuffer = nullptr;

                        // 1.8+: PacketBuffer -> array() + readerIndex()
                        if (mid_getBufferData && !useLegacyCustomPayload) {
                            packetBuffer = env->CallObjectMethod(packet, mid_getBufferData);
                            if (packetBuffer && !env->ExceptionCheck() && mid_readableBytes && mid_array &&
                                mid_readerIndex) {
                                jint readableBytes = env->CallIntMethod(packetBuffer, mid_readableBytes);
                                if (readableBytes > 0 && readableBytes < 65536) {
                                    jint readerIdx = env->CallIntMethod(packetBuffer, mid_readerIndex);
                                    if (!env->ExceptionCheck()) {
                                        byteArray = (jbyteArray)env->CallObjectMethod(packetBuffer, mid_array);
                                        if (byteArray && !env->ExceptionCheck()) {
                                            jint arrayLen = env->GetArrayLength(byteArray);
                                            if (readerIdx >= 0 && readerIdx + readableBytes <= arrayLen) {
                                                bytes = env->GetByteArrayElements(byteArray, nullptr);
                                                if (bytes) {
                                                    data = (const uint8_t*)(bytes + readerIdx);
                                                    len = readableBytes;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 1.7.10: byte[] payload via func_149168_d
                        if (!data && mid_getPayloadBytes_legacy) {
                            byteArray = (jbyteArray)env->CallObjectMethod(packet, mid_getPayloadBytes_legacy);
                            if (byteArray && !env->ExceptionCheck()) {
                                jint arrayLen = env->GetArrayLength(byteArray);
                                if (arrayLen > 0 && arrayLen < 65536) {
                                    bytes = env->GetByteArrayElements(byteArray, nullptr);
                                    if (bytes) {
                                        data = (const uint8_t*)bytes;
                                        len = arrayLen;
                                    }
                                }
                            }
                        }

                        if (data && len > 0) {
                            // Parse protobuf Any wrapper to get type_url
                            // Protobuf wire format: field 1 (type_url) = tag 0x0A (field 1, wire type 2)
                            // Field 2 (value) = tag 0x12 (field 2, wire type 2)
                            std::string typeUrl;
                            int pos = 0;
                            while (pos < len) {
                                if (pos >= len)
                                    break;
                                uint8_t tag = data[pos++];
                                int fieldNum = tag >> 3;
                                int wireType = tag & 0x07;

                                if (wireType == 2) {  // Length-delimited
                                    // Read varint length
                                    int strLen = 0;
                                    int shift = 0;
                                    while (pos < len) {
                                        uint8_t b = data[pos++];
                                        strLen |= (b & 0x7F) << shift;
                                        shift += 7;
                                        if (!(b & 0x80))
                                            break;
                                    }
                                    if (fieldNum == 1 && strLen > 0 && pos + strLen <= len) {
                                        // type_url field
                                        typeUrl = std::string((const char*)(data + pos), strLen);
                                    }
                                    pos += strLen;
                                } else if (wireType == 0) {  // Varint
                                    while (pos < len && (data[pos++] & 0x80))
                                        ;
                                } else {
                                    break;  // Unknown wire type
                                }
                            }

                            // Log if we found a nametag message
                            if (!typeUrl.empty()) {
                                static std::atomic<int> teamCount{0};

                                // Detect and parse Team messages
                                if (typeUrl.find("UpdateTeamMembers") != std::string::npos) {
                                    // Parse value field (field 2) from Any wrapper
                                    const uint8_t* valueData = nullptr;
                                    int valueLen = 0;
                                    int parsePos = 0;
                                    while (parsePos < len) {
                                        uint8_t tag = data[parsePos++];
                                        int fieldNum = tag >> 3;
                                        int wireType = tag & 0x07;
                                        if (wireType == 2) {
                                            int fLen = 0, shift = 0;
                                            while (parsePos < len) {
                                                uint8_t b = data[parsePos++];
                                                fLen |= (b & 0x7F) << shift;
                                                shift += 7;
                                                if (!(b & 0x80))
                                                    break;
                                            }
                                            if (fieldNum == 2 && fLen > 0 && parsePos + fLen <= len) {
                                                valueData = data + parsePos;
                                                valueLen = fLen;
                                            }
                                            parsePos += fLen;
                                        } else if (wireType == 0) {
                                            while (parsePos < len && (data[parsePos++] & 0x80))
                                                ;
                                        } else
                                            break;
                                    }

                                    // Parse UpdateTeamMembersMessage: field 1 = members (repeated TeamMember)
                                    if (valueData && valueLen > 0) {
                                        std::vector<std::pair<uint64_t, uint64_t>> teammateUuids;

                                        // Debug: log valueLen once
                                        static std::atomic<int> parseDebugCount{0};
                                        bool shouldDebug = parseDebugCount.load() < 3;
                                        if (shouldDebug) {
                                            log_debug("[Apollo] Team value len: %d bytes", valueLen);
                                        }

                                        int memberCount = 0;
                                        int vPos = 0;
                                        while (vPos < valueLen) {
                                            uint8_t vTag = valueData[vPos++];
                                            int vFieldNum = vTag >> 3;
                                            int vWireType = vTag & 0x07;

                                            if (vWireType == 2) {  // Length-delimited
                                                int vLen = 0, vShift = 0;
                                                while (vPos < valueLen) {
                                                    uint8_t b = valueData[vPos++];
                                                    vLen |= (b & 0x7F) << vShift;
                                                    vShift += 7;
                                                    if (!(b & 0x80))
                                                        break;
                                                }
                                                if (vFieldNum == 1 && vLen > 0 && vPos + vLen <= valueLen) {
                                                    memberCount++;
                                                    // TeamMember message: field 1 = player_uuid
                                                    const uint8_t* memberData = valueData + vPos;
                                                    int mPos = 0;
                                                    uint64_t uuidHigh = 0, uuidLow = 0;

                                                    // Debug: log first member's fields
                                                    if (memberCount == 1 && shouldDebug) {
                                                        log_debug(
                                                            "[Apollo]   Member1 len=%d, first bytes: %02x %02x %02x "
                                                            "%02x %02x",
                                                            vLen, vLen > 0 ? memberData[0] : 0,
                                                            vLen > 1 ? memberData[1] : 0, vLen > 2 ? memberData[2] : 0,
                                                            vLen > 3 ? memberData[3] : 0, vLen > 4 ? memberData[4] : 0);
                                                    }
                                                    while (mPos < vLen) {
                                                        uint8_t mTag = memberData[mPos++];
                                                        int mFieldNum = mTag >> 3;
                                                        int mWireType = mTag & 0x07;
                                                        if (mWireType == 2) {
                                                            int mLen = 0, mShift = 0;
                                                            while (mPos < vLen) {
                                                                uint8_t b = memberData[mPos++];
                                                                mLen |= (b & 0x7F) << mShift;
                                                                mShift += 7;
                                                                if (!(b & 0x80))
                                                                    break;
                                                            }
                                                            if (mFieldNum == 1 && mLen > 0 && mPos + mLen <= vLen) {
                                                                // player_uuid (Uuid proto) - uses fixed64 for
                                                                // high64/low64
                                                                const uint8_t* uuidData = memberData + mPos;
                                                                int uPos = 0;
                                                                while (uPos < mLen) {
                                                                    uint8_t uTag = uuidData[uPos++];
                                                                    int uFieldNum = uTag >> 3;
                                                                    int uWireType = uTag & 0x07;

                                                                    if (uWireType == 1 && uPos + 8 <= mLen) {
                                                                        // Wire type 1 = fixed64 (8 bytes little-endian)
                                                                        uint64_t val = 0;
                                                                        for (int i = 0; i < 8; i++) {
                                                                            val |= (uint64_t)uuidData[uPos + i]
                                                                                   << (i * 8);
                                                                        }
                                                                        uPos += 8;
                                                                        if (uFieldNum == 1)
                                                                            uuidHigh = val;
                                                                        else if (uFieldNum == 2)
                                                                            uuidLow = val;
                                                                    } else if (uWireType == 0) {
                                                                        // Wire type 0 = varint (skip)
                                                                        while (uPos < mLen && (uuidData[uPos++] & 0x80))
                                                                            ;
                                                                    } else
                                                                        break;
                                                                }
                                                                if (uuidHigh != 0 || uuidLow != 0) {
                                                                    teammateUuids.push_back({uuidHigh, uuidLow});
                                                                }
                                                            }
                                                            mPos += mLen;
                                                        } else if (mWireType == 0) {
                                                            while (mPos < vLen && (memberData[mPos++] & 0x80))
                                                                ;
                                                        } else
                                                            break;
                                                    }
                                                }
                                                vPos += vLen;
                                            } else if (vWireType == 0) {
                                                while (vPos < valueLen && (valueData[vPos++] & 0x80))
                                                    ;
                                            } else
                                                break;
                                        }

                                        if (shouldDebug) {
                                            log_debug("[Apollo] Team: found %d members, extracted %zu UUIDs",
                                                      memberCount, teammateUuids.size());
                                            parseDebugCount++;
                                        }

                                        // Log team update once
                                        if (!teammateUuids.empty() && teamCount.load() < 5) {
                                            log_ok("[Apollo] Team update: %d teammates", (int)teammateUuids.size());
                                            for (size_t i = 0; i < teammateUuids.size(); i++) {
                                                char uuidStr[64];
                                                uint64_t hi = teammateUuids[i].first;
                                                uint64_t lo = teammateUuids[i].second;
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(hi >> 32), (uint16_t)(hi >> 16), (uint16_t)hi,
                                                         (uint16_t)(lo >> 48),
                                                         (unsigned long long)(lo & 0xFFFFFFFFFFFFULL));
                                                log_debug("  Teammate[%d]: %s", (int)i, uuidStr);
                                            }
                                            teamCount++;
                                        }

                                        // Register teammates with FriendsManager
                                        // Use diff-based sync to properly handle teammate removal
                                        if (friendsManager) {
                                            // Build new UUID set from packet
                                            std::set<std::string> newTeammates;
                                            for (size_t i = 0; i < teammateUuids.size(); i++) {
                                                char uuidStr[64];
                                                uint64_t hi = teammateUuids[i].first;
                                                uint64_t lo = teammateUuids[i].second;
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(hi >> 32), (uint16_t)(hi >> 16), (uint16_t)hi,
                                                         (uint16_t)(lo >> 48),
                                                         (unsigned long long)(lo & 0xFFFFFFFFFFFFULL));
                                                newTeammates.insert(uuidStr);
                                            }


                                            friendsManager->syncLunarTeammates(newTeammates);

                                            // Only log when teammate count changes significantly
                                            static size_t lastTeammateCount = 0;
                                            size_t currentCount = newTeammates.size();
                                            if (currentCount != lastTeammateCount) {
                                                log_ok("[Apollo] Team synced: %d teammate(s)", (int)currentCount);
                                                lastTeammateCount = currentCount;
                                            }
                                        }
                                    }
                                } else if (typeUrl.find("OverrideNametag") != std::string::npos) {
                                    // Parse value field (field 2) from Any wrapper
                                    const uint8_t* valueData = nullptr;
                                    int valueLen = 0;
                                    int parsePos = 0;
                                    while (parsePos < len) {
                                        uint8_t tag = data[parsePos++];
                                        int fieldNum = tag >> 3;
                                        int wireType = tag & 0x07;
                                        if (wireType == 2) {
                                            int fLen = 0, shift = 0;
                                            while (parsePos < len) {
                                                uint8_t b = data[parsePos++];
                                                fLen |= (b & 0x7F) << shift;
                                                shift += 7;
                                                if (!(b & 0x80))
                                                    break;
                                            }
                                            if (fieldNum == 2 && fLen > 0 && parsePos + fLen <= len) {
                                                valueData = data + parsePos;
                                                valueLen = fLen;
                                            }
                                            parsePos += fLen;
                                        } else if (wireType == 0) {
                                            while (parsePos < len && (data[parsePos++] & 0x80))
                                                ;
                                        } else
                                            break;
                                    }

                                    // Parse OverrideNametagMessage:
                                    // field 1 = player_uuid (Uuid)
                                    // field 3 = adventure_json_lines (repeated string)
                                    if (valueData && valueLen > 0) {
                                        uint64_t uuidHigh = 0, uuidLow = 0;
                                        std::vector<std::string> jsonLines;

                                        int vPos = 0;
                                        int fieldCount = 0;
                                        while (vPos < valueLen) {
                                            uint8_t vTag = valueData[vPos++];
                                            int vFieldNum = vTag >> 3;
                                            int vWireType = vTag & 0x07;

                                            if (vWireType == 2) {
                                                int vLen = 0, vShift = 0;
                                                while (vPos < valueLen) {
                                                    uint8_t b = valueData[vPos++];
                                                    vLen |= (b & 0x7F) << vShift;
                                                    vShift += 7;
                                                    if (!(b & 0x80))
                                                        break;
                                                }
                                                if (vFieldNum == 1 && vLen > 0 && vPos + vLen <= valueLen) {
                                                    const uint8_t* uuidData = valueData + vPos;
                                                    int uPos = 0;
                                                    while (uPos < vLen) {
                                                        uint8_t uTag = uuidData[uPos++];
                                                        int uFieldNum = uTag >> 3;
                                                        int uWireType = uTag & 0x07;

                                                        if (uWireType == 1 && uPos + 8 <= vLen) {
                                                            // Wire type 1 = fixed64
                                                            uint64_t val = 0;
                                                            for (int i = 0; i < 8; i++) {
                                                                val |= (uint64_t)uuidData[uPos + i] << (i * 8);
                                                            }
                                                            uPos += 8;
                                                            if (uFieldNum == 1)
                                                                uuidHigh = val;
                                                            else if (uFieldNum == 2)
                                                                uuidLow = val;
                                                        } else if (uWireType == 0) {
                                                            while (uPos < vLen && (uuidData[uPos++] & 0x80))
                                                                ;
                                                        } else
                                                            break;
                                                    }
                                                } else if (vFieldNum == 3 && vLen > 0 && vPos + vLen <= valueLen) {
                                                    jsonLines.push_back(
                                                        std::string((const char*)(valueData + vPos), vLen));
                                                }
                                                vPos += vLen;
                                            } else if (vWireType == 0) {
                                                while (vPos < valueLen && (valueData[vPos++] & 0x80))
                                                    ;
                                            } else
                                                break;
                                            fieldCount++;
                                        }

                                        // Debug parsing results (limited)
                                        static std::atomic<int> nametagDebug{0};
                                        if (nametagDebug.load() < 3) {
                                            log_debug("[Apollo] Nametag parsed: uuidHigh=%llx uuidLow=%llx lines=%zu",
                                                      (unsigned long long)uuidHigh, (unsigned long long)uuidLow,
                                                      jsonLines.size());
                                            nametagDebug++;
                                        }

                                        // Debug: show a few distinct first-line previews
                                        static std::atomic<int> jsonPreviewCount{0};
                                        static std::string lastJsonPreview;
                                        if (!jsonLines.empty() && jsonPreviewCount.load() < 10) {
                                            std::string preview = jsonLines[0];
                                            if (preview.length() > 80)
                                                preview = preview.substr(0, 80) + "...";
                                            if (preview != lastJsonPreview) {
                                                // log_debug("[Apollo]   JSON[0]: %s", preview.c_str());
                                                lastJsonPreview = preview;
                                                jsonPreviewCount++;
                                            }
                                        }

                                        // Detect color from JSON lines (prefer explicit Adventure color, fallback to
                                        // format codes)
                                        std::string detectedColor = "unknown";

                                        // Try explicit Adventure color field: "color":"#RRGGBB"
                                        if (!jsonLines.empty()) {
                                            const std::string& firstLine = jsonLines[0];
                                            const std::string colorKey = "\"color\":\"#";
                                            size_t cPos = firstLine.find(colorKey);
                                            if (cPos != std::string::npos &&
                                                cPos + colorKey.size() + 6 <= firstLine.size()) {
                                                std::string hex = firstLine.substr(cPos + colorKey.size(), 6);
                                                for (auto& ch : hex)
                                                    ch = (char)std::toupper((unsigned char)ch);

                                                static const std::unordered_map<std::string, std::string> kHexToColor =
                                                    {
                                                        // Vanilla chat/motd palette
                                                        {"000000", "BLACK"},
                                                        {"0000AA", "DARK_BLUE"},
                                                        {"00AA00", "DARK_GREEN"},
                                                        {"00AAAA", "DARK_AQUA"},
                                                        {"AA0000", "DARK_RED"},
                                                        {"AA00AA", "DARK_PURPLE"},
                                                        {"FFAA00", "GOLD"},
                                                        {"AAAAAA", "GRAY"},
                                                        {"555555", "DARK_GRAY"},
                                                        {"5555FF", "BLUE"},
                                                        {"55FF55", "GREEN"},
                                                        {"55FFFF", "AQUA"},
                                                        {"FF5555", "RED"},
                                                        {"FF55FF", "LIGHT_PURPLE"},
                                                        {"FFFF55", "YELLOW"},
                                                        {"FFFFFF", "WHITE"},
                                                        // Extra hex observed in Apollo nametags
                                                        {"55FF5F", "GREEN"},  // bright green
                                                        {"FF5551", "RED"},    // bright red variant
                                                        {"AA0003", "DARK_RED"},
                                                        {"FF55FA", "LIGHT_PURPLE"},
                                                        {"AA00AC", "DARK_PURPLE"},
                                                        {"00AAAC", "DARK_AQUA"},
                                                        {"5555FE", "BLUE"},
                                                        {"FFFFFA", "WHITE"},
                                                        {"AAAAAC", "GRAY"},
                                                        {"55FFFA", "AQUA"},
                                                        {"FFAA02", "GOLD"},
                                                    };

                                                auto it = kHexToColor.find(hex);
                                                if (it != kHexToColor.end()) {
                                                    detectedColor = it->second;
                                                }
                                            }
                                        }

                                        // Fallback: Minecraft format codes (B'X) - only if Adventure color wasn't found
                                        if (detectedColor == "unknown") {
                                            for (const auto& line : jsonLines) {
                                                size_t pos = line.find(
                                                    "\xc2"
                                                    "\xa7");
                                                while (pos != std::string::npos && pos + 2 < line.size()) {
                                                    char code = (char)std::tolower((unsigned char)line[pos + 2]);
                                                    switch (code) {
                                                        case '0':
                                                            detectedColor = "BLACK";
                                                            break;
                                                        case '1':
                                                            detectedColor = "DARK_BLUE";
                                                            break;
                                                        case '2':
                                                            detectedColor = "DARK_GREEN";
                                                            break;
                                                        case '3':
                                                            detectedColor = "DARK_AQUA";
                                                            break;
                                                        case '4':
                                                            detectedColor = "DARK_RED";
                                                            break;
                                                        case '5':
                                                            detectedColor = "DARK_PURPLE";
                                                            break;
                                                        case '6':
                                                            detectedColor = "GOLD";
                                                            break;
                                                        case '7':
                                                            detectedColor = "GRAY";
                                                            break;
                                                        case '8':
                                                            detectedColor = "DARK_GRAY";
                                                            break;
                                                        case '9':
                                                            detectedColor = "BLUE";
                                                            break;
                                                        case 'a':
                                                            detectedColor = "GREEN";
                                                            break;
                                                        case 'b':
                                                            detectedColor = "AQUA";
                                                            break;
                                                        case 'c':
                                                            detectedColor = "RED";
                                                            break;
                                                        case 'd':
                                                            detectedColor = "LIGHT_PURPLE";
                                                            break;
                                                        case 'e':
                                                            detectedColor = "YELLOW";
                                                            break;
                                                        case 'f':
                                                            detectedColor = "WHITE";
                                                            break;
                                                        default:
                                                            break;
                                                    }
                                                    if (detectedColor != "unknown")
                                                        break;
                                                    pos = line.find(
                                                        "\xc2"
                                                        "\xa7",
                                                        pos + 3);
                                                }
                                                if (detectedColor != "unknown")
                                                    break;
                                            }
                                        }  // end fallback if

                                        // If still unknown, log a limited preview to diagnose color mapping
                                        if (detectedColor == "unknown" && !jsonLines.empty()) {
                                            static std::unordered_set<std::string> unknownLogged;
                                            std::string preview = jsonLines[0];
                                            if (preview.length() > 80)
                                                preview = preview.substr(0, 80) + "...";
                                            if (unknownLogged.size() < 20 &&
                                                unknownLogged.find(preview) == unknownLogged.end()) {
                                                char uuidStr[64];
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(uuidHigh >> 32), (uint16_t)(uuidHigh >> 16),
                                                         (uint16_t)uuidHigh, (uint16_t)(uuidLow >> 48),
                                                         (unsigned long long)(uuidLow & 0xFFFFFFFFFFFFULL));
                                                log_debug("[Apollo] Nametag unknown color: UUID=%s JSON[0]=%s", uuidStr,
                                                          preview.c_str());
                                                unknownLogged.insert(preview);
                                            }
                                        }

                                        // Log the parsed nametag with color
                                        if ((uuidHigh != 0 || uuidLow != 0) && !jsonLines.empty()) {
                                            // Extract player name from first JSON line
                                            std::string playerName = "?";
                                            const std::string& firstLine = jsonLines[0];
                                            size_t textPos = firstLine.find("\"text\":\"");
                                            if (textPos != std::string::npos) {
                                                size_t startPos = textPos + 8;
                                                size_t endPos = firstLine.find("\"", startPos);
                                                if (endPos != std::string::npos) {
                                                    playerName = firstLine.substr(startPos, endPos - startPos);
                                                    // Strip Minecraft color codes from the extracted name
                                                    std::string clean;
                                                    for (size_t i = 0; i < playerName.size(); ++i) {
                                                        unsigned char c = (unsigned char)playerName[i];
                                                        if (c == 0xC2 && i + 2 < playerName.size() &&
                                                            (unsigned char)playerName[i + 1] == 0xA7) {
                                                            i += 2;
                                                        } else if (c != 0xC2 && c != 0xA7) {
                                                            clean += playerName[i];
                                                        }
                                                    }
                                                    if (!clean.empty())
                                                        playerName = clean;
                                                }
                                            }

                                            static std::unordered_map<std::string, std::string> lastNametagColor;
                                            std::string uuidKey =
                                                std::to_string(uuidHigh) + ":" + std::to_string(uuidLow);
                                            auto itColor = lastNametagColor.find(uuidKey);
                                            bool shouldLog = (itColor == lastNametagColor.end()) ||
                                                             (itColor->second != detectedColor);
                                            if (shouldLog) {
                                                lastNametagColor[uuidKey] = detectedColor;
                                                log_ok("[Apollo] Nametag: \"%s\" -> %s", playerName.c_str(),
                                                       detectedColor.c_str());
                                            }

                                            // Register nametag color with FriendsManager for team detection
                                            if (friendsManager && detectedColor != "unknown") {
                                                char uuidStr[64];
                                                snprintf(uuidStr, sizeof(uuidStr), "%08x-%04x-%04x-%04x-%012llx",
                                                         (uint32_t)(uuidHigh >> 32), (uint16_t)(uuidHigh >> 16),
                                                         (uint16_t)uuidHigh, (uint16_t)(uuidLow >> 48),
                                                         (unsigned long long)(uuidLow & 0xFFFFFFFFFFFFULL));
                                                friendsManager->setApolloNametagColor(uuidStr, detectedColor);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (byteArray && bytes) {
                            env->ReleaseByteArrayElements(byteArray, bytes, JNI_ABORT);
                        }
                        if (packetBuffer) {
                            env->DeleteLocalRef(packetBuffer);
                        }
                        if (env->ExceptionCheck())
                            env->ExceptionClear();
                    }
                    env->ReleaseStringUTFChars(channelStr, channel);
                }
                env->DeleteLocalRef(channelStr);
            }
        }
    }
    // Check for S3EPacketTeams (Minecraft scoreboard teams)
    static jclass cls_S3EPacketTeams = nullptr;
    static jmethodID mid_getAction = nullptr;
    static jmethodID mid_getName = nullptr;
    static jmethodID mid_getPlayers = nullptr;
    static bool triedTeamsInit = false;

    if (!triedTeamsInit) {
        triedTeamsInit = true;
        jclass local = env->FindClass("net/minecraft/network/play/server/S3EPacketTeams");
        if (local) {
            cls_S3EPacketTeams = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            mid_getAction = env->GetMethodID(cls_S3EPacketTeams, "getAction", "()I");
            mid_getName = env->GetMethodID(cls_S3EPacketTeams, "getName", "()Ljava/lang/String;");
            mid_getPlayers = env->GetMethodID(cls_S3EPacketTeams, "getPlayers", "()Ljava/util/Collection;");
            if (env->ExceptionCheck())
                env->ExceptionClear();
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    if (cls_S3EPacketTeams && env->IsInstanceOf(packet, cls_S3EPacketTeams)) {
        static std::atomic<int> teamPacketCount{0};

        // Get action type: 0=create, 1=remove, 2=update, 3=add players, 4=remove players
        jint action = mid_getAction ? env->CallIntMethod(packet, mid_getAction) : -1;

        // Get team name
        jstring nameStr = mid_getName ? (jstring)env->CallObjectMethod(packet, mid_getName) : nullptr;
        const char* teamName = nameStr ? env->GetStringUTFChars(nameStr, nullptr) : nullptr;

        // Get players collection
        jobject playersCollection = mid_getPlayers ? env->CallObjectMethod(packet, mid_getPlayers) : nullptr;
        int playersCount = 0;
        if (playersCollection) {
            jclass cls_Collection = env->FindClass("java/util/Collection");
            jmethodID mid_size = env->GetMethodID(cls_Collection, "size", "()I");
            if (mid_size) {
                playersCount = env->CallIntMethod(playersCollection, mid_size);
            }
            env->DeleteLocalRef(cls_Collection);
            env->DeleteLocalRef(playersCollection);
        }

        // Only log meaningful team packets (not visual formatting)
        // Skip UPDATE with 0 players (these are scoreboard sidebar formatting)
        bool isRelevant = (action == 0 || action == 3 || action == 4 || playersCount > 0);

        if (isRelevant && teamPacketCount.load() < 20) {
            const char* actionStr = "";
            switch (action) {
                case 0:
                    actionStr = "CREATE";
                    break;
                case 1:
                    actionStr = "REMOVE";
                    break;
                case 2:
                    actionStr = "UPDATE";
                    break;
                case 3:
                    actionStr = "ADD_PLAYERS";
                    break;
                case 4:
                    actionStr = "REMOVE_PLAYERS";
                    break;
                default:
                    actionStr = "UNKNOWN";
                    break;
            }

            // log_ok("[Scoreboard] Team packet: action=%s team='%s' players=%d",
            //        actionStr, teamName ? teamName : "?", playersCount);

            teamPacketCount++;
        }

        if (teamName)
            env->ReleaseStringUTFChars(nameStr, teamName);
        if (nameStr)
            env->DeleteLocalRef(nameStr);
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    // Process Lunar Client nametag packets (legacy path)
    if (LunarPacketListener::IsReady()) {
        LunarPacketListener::processIncomingPacket(env, packet);
    }
}

static bool ensureHookClass(JNIEnv* env) {
    if (cls_PacketHook)
        return true;

    jclass hook = nullptr;
    if (!Java::AssignClass(kHookClassDot, hook)) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    if (!hook) {
        if (!Java::ClassLoader) {
            // log_err("[PacketInterceptor] No classloader available for PacketHook");
            return false;
        }
        hook =
            env->DefineClass(kHookClassSlash, Java::ClassLoader, reinterpret_cast<const jbyte*>(kPacketHookClassBytes),
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
                // log_err("[PacketInterceptor] Failed to define PacketHook class");
                return false;
            }
        }
    }

    // Register both native methods
    JNINativeMethod methods[2];

    // shouldSend for outgoing packets (returns boolean for blocking)
    methods[0].name = const_cast<char*>(kHookMethodName);
    methods[0].signature = const_cast<char*>(kHookMethodDesc);
    methods[0].fnPtr = reinterpret_cast<void*>(&nativeShouldSend);

    // onReceive for incoming packets (void, observer only)
    methods[1].name = const_cast<char*>(kOnReceiveMethodName);
    methods[1].signature = const_cast<char*>(kOnReceiveMethodDesc);
    methods[1].fnPtr = reinterpret_cast<void*>(&nativeOnReceive);

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
    log_ok("[PacketInterceptor] PacketHook native bridge ready (shouldSend + onReceive)");
    return true;
}

static bool installBytecodeHook(JNIEnv* env) {
    if (bytecodeHookInstalled.load())
        return true;
    if (!Java::tiEnv)
        return false;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = &onClassFileLoadHook;

    jvmtiError err = Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        // log_warn("[PacketInterceptor] Failed to set JVMTI class hook callbacks: %d", err);
        return false;
    }

    err = Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
    if (err != JVMTI_ERROR_NONE) {
        // log_warn("[PacketInterceptor] Failed to enable class hook events: %d", err);
        return false;
    }

    bytecodeHookInstalled = true;

    jclass nm = nullptr;
    if (Java::AssignClass(kTargetClassDot, nm)) {
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

    // Also retransform LCNetHandlerClient for Lunar nametag packet interception
    jclass lunarHandler = nullptr;
    if (Java::AssignClass("com.lunarclient.bukkitapi.nethandler.client.LCNetHandlerClient", lunarHandler)) {
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
        // Not an error - Lunar classes may not be available on all clients
        log_debug("[PacketInterceptor] LCNetHandlerClient not available for retransform");
    }

    // Retransform NetHandlerPlayClient for handleCustomPayload hook
    jclass netHandler = nullptr;
    if (Java::AssignClass(kIncomingTargetClassDot, netHandler)) {
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

    return true;
}

static bool installMethodEntryHook() {
    if (!Java::tiEnv)
        return false;

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.MethodEntry = [](jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread, jmethodID method) {
        // Hook should be active for Blink OR NoItemRelease
        bool shouldIntercept =
            blinkActive.load() || (::noItemRelease && ::noItemRelease->enabled && ::noItemRelease->wasUsingItem);

        if (!shouldIntercept)
            return;

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
                        // log_debug("[PacketInterceptor] Blocked sendPacket via ForceEarlyReturn");
                    }
                    if (packet) {
                        jni_env->DeleteLocalRef(packet);
                    }
                }
                if (classSig)
                    jvmti_env->Deallocate((unsigned char*)classSig);
                jni_env->DeleteLocalRef(declaringClass);
            }
        }
        if (methodName)
            jvmti_env->Deallocate((unsigned char*)methodName);
    };

    jvmtiError err = Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        // log_warn("[PacketInterceptor] Failed to set JVMTI callbacks: %d", err);
        return false;
    }

    err = Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
    if (err == JVMTI_ERROR_NONE) {
        // log_ok("[PacketInterceptor] JVMTI Method Entry hook installed");
        return true;
    }

    // log_warn("[PacketInterceptor] Failed to enable method entry events: %d", err);
    return false;
}

// ============================================================================
// Check if packet is a movement packet (C03PacketPlayer or subclass)
// ============================================================================
static bool isMovementPacket(JNIEnv* env, jobject packet) {
    if (!packet)
        return false;

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
            bool isMovement =
                (strstr(sig, "C03PacketPlayer") != nullptr) || (strstr(sig, "C04PacketPlayerPosition") != nullptr) ||
                (strstr(sig, "C05PacketPlayerLook") != nullptr) || (strstr(sig, "C06PacketPlayerPosLook") != nullptr);
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
    if (!packet)
        return "null";

    jclass packetClass = env->GetObjectClass(packet);
    if (!packetClass)
        return "unknown";

    static char nameBuf[128];
    nameBuf[0] = '\0';

    if (Java::tiEnv) {
        char* sig = nullptr;
        if (Java::tiEnv->GetClassSignature(packetClass, &sig, nullptr) == JVMTI_ERROR_NONE) {
            // Extract simple name from signature like "Lnet/minecraft/network/.../C03PacketPlayer;"
            const char* lastSlash = strrchr(sig, '/');
            if (lastSlash) {
                strncpy_s(nameBuf, sizeof(nameBuf), lastSlash + 1, _TRUNCATE);
                // Remove trailing semicolon
                size_t len = strlen(nameBuf);
                if (len > 0 && nameBuf[len - 1] == ';') {
                    nameBuf[len - 1] = '\0';
                }
            } else {
                strncpy_s(nameBuf, sizeof(nameBuf), sig, _TRUNCATE);
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
        // log_err("[PacketInterceptor] Java not initialized");
        return false;
    }

    JNIEnv* env = Java::Env;
    if (!env) {
        // log_err("[PacketInterceptor] No JNIEnv");
        return false;
    }

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    bool haveCaps = false;

    if (!Java::tiEnv) {
        // log_warn("[PacketInterceptor] JVMTI not available - limited functionality");
    } else {
        // log_ok("[PacketInterceptor] JVMTI available");
        if (Java::tiEnv->GetCapabilities(&caps) == JVMTI_ERROR_NONE) {
            haveCaps = true;
            // log_debug("[PacketInterceptor] JVMTI Capabilities:");
            // log_debug("  can_retransform_classes: %d", caps.can_retransform_classes);
            // log_debug("  can_redefine_classes: %d", caps.can_redefine_classes);
            // log_debug("  can_generate_all_class_hook_events: %d", caps.can_generate_all_class_hook_events);
            // log_debug("  can_generate_method_entry_events: %d", caps.can_generate_method_entry_events);
            // log_debug("  can_force_early_return: %d", caps.can_force_early_return);
            // log_debug("  can_access_local_variables: %d", caps.can_access_local_variables);
        }
    }

    // Cache packet classes
    if (!cachePacketClasses(env)) {
        // log_err("[PacketInterceptor] Failed to cache packet classes");
        return false;
    }

    cachePacketFields(env);

    bool interceptReady = false;
    if (Java::tiEnv && haveCaps) {
        if (caps.can_generate_all_class_hook_events && caps.can_retransform_classes) {
            if (ensureHookClass(env) && installBytecodeHook(env)) {
                interceptReady = true;
            } else {
                // log_warn("[PacketInterceptor] Bytecode hook setup failed");
            }
        } else {
            // log_warn("[PacketInterceptor] Class hook capabilities unavailable");
        }

        if (!interceptReady && caps.can_generate_method_entry_events && caps.can_force_early_return &&
            caps.can_access_local_variables) {
            if (installMethodEntryHook()) {
                interceptReady = true;
            }
        }

        if (!interceptReady) {
            // log_warn("[PacketInterceptor] No usable JVMTI interception path available");
        }
    }

    // Get NetworkManager instance
    networkManagerInstance = getNetworkManager(env);
    if (networkManagerInstance) {
        // Create global ref
        networkManagerInstance = env->NewGlobalRef(networkManagerInstance);
        // log_ok("[PacketInterceptor] Cached NetworkManager instance as GlobalRef");
    } else {
        // log_warn("[PacketInterceptor] No NetworkManager yet - will try later");
    }

    initialized = true;
    // log_ok("[PacketInterceptor] Initialized successfully");

    return true;
}

void Shutdown() {
    // log_debug("[PacketInterceptor] Shutting down...");

    blinkActive = false;
    hybridIntercept = false;
    hybridRelease = false;
    clearDesiredQueue();
    {
        std::lock_guard<std::mutex> lock(shadowMutex);
        haveLastSent = false;
        shadowInitialized = false;
    }

    // Clear queue
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        JNIEnv* env = Java::Env;
        if (env) {
            for (auto& qp : packetQueue) {
                env->DeleteGlobalRef(qp.packet);
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
        if (cls_NetworkManager)
            env->DeleteGlobalRef(cls_NetworkManager);
        if (cls_Packet)
            env->DeleteGlobalRef(cls_Packet);
        if (cls_C03PacketPlayer)
            env->DeleteGlobalRef(cls_C03PacketPlayer);
        if (cls_C04PacketPlayerPosition)
            env->DeleteGlobalRef(cls_C04PacketPlayerPosition);
        if (cls_C05PacketPlayerLook)
            env->DeleteGlobalRef(cls_C05PacketPlayerLook);
        if (cls_C06PacketPlayerPosLook)
            env->DeleteGlobalRef(cls_C06PacketPlayerPosLook);
    }

    fid_pkt_x = nullptr;
    fid_pkt_y = nullptr;
    fid_pkt_z = nullptr;
    packetFieldsReady.store(false, std::memory_order_release);
    packetFieldsLogged = false;
    packetFieldsWarned = false;

    initialized = false;
    // log_ok("[PacketInterceptor] Shutdown complete");
}

bool IsReady() {
    return initialized.load();
}

void setBlinkActive(bool active) {
    bool wasActive = blinkActive.exchange(active);

    if (active && !wasActive) {
        hybridIntercept = true;
        hybridRelease = false;
        lastHybridSend = std::chrono::steady_clock::now();
        clearQueue();
        clearDesiredQueue();
        {
            std::lock_guard<std::mutex> lock(shadowMutex);
            if (player.isValid()) {
                lastSentPos.x = player.getPosX();
                lastSentPos.y = player.getPosY();
                lastSentPos.z = player.getPosZ();
                haveLastSent = true;
                shadowPos = lastSentPos;
                shadowInitialized = true;
            } else {
                haveLastSent = false;
                shadowInitialized = false;
            }
        }

        // log_ok("[PacketInterceptor] Hybrid Blink ACTIVATED - delay %d ticks", kHybridDelayTicks);

        if (!packetFieldsReady.load()) {
            JNIEnv* env = Java::Env;
            if (env)
                cachePacketFields(env);
        }

        // Ensure we have NetworkManager instance
        if (!networkManagerInstance) {
            JNIEnv* env = Java::Env;
            if (env) {
                jobject nm = getNetworkManager(env);
                if (nm) {
                    networkManagerInstance = env->NewGlobalRef(nm);
                    // log_ok("[PacketInterceptor] Late-cached NetworkManager instance");
                }
            }
        }
    } else if (!active && wasActive) {
        hybridRelease = true;
        // log_ok("[PacketInterceptor] Hybrid Blink DEACTIVATED - releasing (queue: %zu)", getDesiredQueueCount());
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
    JNIEnv* env = Java::Env;
    if (!env || !networkManagerInstance || !mid_sendPacket) {
        // log_err("[PacketInterceptor] Cannot flush - missing dependencies");
        return;
    }

    std::vector<QueuedPacket> toSend;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (packetQueue.empty()) {
            // log_debug("[PacketInterceptor] Nothing to flush");
            return;
        }
        toSend.swap(packetQueue);
    }

    int count = (int)toSend.size();
    // log_ok("[PacketInterceptor] Flushing %d queued packets...", count);

    for (auto& qp : toSend) {
        // log_debug("[PacketInterceptor] Sending queued packet: %s", getPacketTypeName(env, qp.packet));

        // Call sendPacket on NetworkManager
        env->CallVoidMethod(networkManagerInstance, mid_sendPacket, qp.packet);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            // log_err("[PacketInterceptor] Exception while sending queued packet");
        }

        if (qp.hasPos) {
            std::lock_guard<std::mutex> lock(shadowMutex);
            lastSentPos = qp.pos;
            haveLastSent = true;
        }
        env->DeleteGlobalRef(qp.packet);
    }
    // log_ok("[PacketInterceptor] Flush complete - sent %d packets", count);
}

void clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);

    JNIEnv* env = Java::Env;
    for (auto& qp : packetQueue) {
        if (env)
            env->DeleteGlobalRef(qp.packet);
    }
    packetQueue.clear();

    // log_debug("[PacketInterceptor] Queue cleared");
}

void setDebugPacketLogging(bool enabled) {
    debugPacketLogging.store(enabled);
    log_debug("[PacketInterceptor] Packet logging %s", enabled ? "ENABLED" : "DISABLED");
}

bool isDebugPacketLogging() {
    return debugPacketLogging.load();
}

void printDebugInfo() {
    // log_debug("=== PacketInterceptor Debug ===");
    // log_debug("  Initialized: %s", initialized.load() ? "YES" : "NO");
    // log_debug("  Blink Active: %s", blinkActive.load() ? "YES" : "NO");
    // log_debug("  Hybrid Intercept: %s", hybridIntercept.load() ? "YES" : "NO");
    // log_debug("  Hybrid Release: %s", hybridRelease.load() ? "YES" : "NO");
    // log_debug("  Queued Packets: %d", getQueuedCount());
    // log_debug("  Desired Queue: %zu", getDesiredQueueCount());
    // log_debug("  Packet Fields Ready: %s", packetFieldsReady.load() ? "YES" : "NO");
    // log_debug("  NetworkManager: %p", networkManagerInstance);
    // log_debug("  sendPacket method: %p", mid_sendPacket);
    // log_debug("  C03PacketPlayer class: %p", cls_C03PacketPlayer);
}

bool getLastSentPosition(double& x, double& y, double& z) {
    std::lock_guard<std::mutex> lock(shadowMutex);
    if (!haveLastSent)
        return false;
    x = lastSentPos.x;
    y = lastSentPos.y;
    z = lastSentPos.z;
    return true;
}

void Tick() {
    if (!hybridIntercept.load())
        return;

    if (packetFieldsReady.load()) {
        if (hybridRelease.load() && player.isValid()) {
            PacketPos target{};
            target.x = player.getPosX();
            target.y = player.getPosY();
            target.z = player.getPosZ();

            bool queueEmpty = false;
            {
                std::lock_guard<std::mutex> lock(desiredMutex);
                queueEmpty = desiredQueue.empty();
            }

            std::lock_guard<std::mutex> lock(shadowMutex);
            if (!shadowInitialized) {
                shadowPos = target;
                shadowInitialized = true;
            }
            if (queueEmpty && isSafeSkip(shadowPos, target)) {
                clearDesiredQueue();
                hybridRelease = false;
                hybridIntercept = false;
                // log_ok("[PacketInterceptor] Hybrid release complete - caught up to client");
            }
        }
        return;
    }

    JNIEnv* env = Java::Env;
    if (!env)
        return;
    if (!networkManagerInstance) {
        jobject nm = getNetworkManager(env);
        if (nm) {
            networkManagerInstance = env->NewGlobalRef(nm);
            // log_ok("[PacketInterceptor] Late-cached NetworkManager instance");
        }
    }
    if (!networkManagerInstance || !mid_sendPacket)
        return;
    if (!packetFieldsReady.load()) {
        cachePacketFields(env);
    }

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHybridSend).count() < kHybridIntervalMs) {
        return;
    }

    int targetDelay = blinkActive.load() ? kHybridDelayTicks : 0;
    PacketPos lastPos{};
    bool haveLast = false;
    {
        std::lock_guard<std::mutex> lock(shadowMutex);
        haveLast = haveLastSent;
        if (haveLast)
            lastPos = lastSentPos;
    }

    QueuedPacket toSend{};
    bool haveSend = false;
    bool sentNewest = false;
    bool releaseCandidate = false;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const size_t qsize = packetQueue.size();
        if (qsize <= static_cast<size_t>(targetDelay)) {
            return;
        }

        size_t sendIndex = 0;
        if (hybridRelease.load() && haveLast && packetFieldsReady.load()) {
            for (size_t i = qsize; i-- > 0;) {
                auto& qp = packetQueue[i];
                if (!qp.hasPos) {
                    PacketPos tmp{};
                    if (readPacketPos(env, qp.packet, tmp)) {
                        qp.pos = tmp;
                        qp.hasPos = true;
                    }
                }
                if (qp.hasPos && isSafeSkip(lastPos, qp.pos)) {
                    sendIndex = i;
                    sentNewest = (i == qsize - 1);
                    break;
                }
            }
        }

        if (sendIndex > 0) {
            // log_debug("[PacketInterceptor] Hybrid skip %zu packet(s) to catch up", sendIndex);
            for (size_t i = 0; i < sendIndex; ++i) {
                env->DeleteGlobalRef(packetQueue[i].packet);
            }
            packetQueue.erase(packetQueue.begin(),
                              packetQueue.begin() + static_cast<std::vector<QueuedPacket>::difference_type>(sendIndex));
        }

        if (!packetQueue.empty()) {
            toSend = packetQueue.front();
            packetQueue.erase(packetQueue.begin());
            haveSend = true;
            releaseCandidate = (hybridRelease.load() && sentNewest && packetQueue.empty());
        }
    }

    if (!haveSend)
        return;
    lastHybridSend = now;

    env->CallVoidMethod(networkManagerInstance, mid_sendPacket, toSend.packet);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        // log_err("[PacketInterceptor] Exception while sending hybrid packet");
    }

    if (!toSend.hasPos && packetFieldsReady.load()) {
        PacketPos tmp{};
        if (readPacketPos(env, toSend.packet, tmp)) {
            toSend.pos = tmp;
            toSend.hasPos = true;
        }
    }
    if (toSend.hasPos) {
        std::lock_guard<std::mutex> lock(shadowMutex);
        lastSentPos = toSend.pos;
        haveLastSent = true;
    }
    env->DeleteGlobalRef(toSend.packet);

    if (releaseCandidate) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (packetQueue.empty()) {
            hybridRelease = false;
            hybridIntercept = false;
            // log_ok("[PacketInterceptor] Hybrid release complete - passthrough restored");
        }
    }
}

bool shouldSendPacket(JNIEnv* env, jobject packet) {
    // Debug logging for all intercepted packets
    if (debugPacketLogging.load()) {
        const char* pktName = getPacketTypeName(env, packet);
        // log_debug("[PacketInterceptor] Intercepted: %s", pktName);
    }

    // Check for Lunar Client nametag packets
    if (LunarPacketListener::IsReady()) {
        LunarPacketListener::processIncomingPacket(env, packet);
    }

    // ===== NOITEMRELEASE PACKET BLOCKING =====
    // Check for C07PacketPlayerDigging with RELEASE_USE_ITEM action
    // This check MUST happen regardless of hybridIntercept state
    {
        // Use global noItemRelease pointer (defined in noitemrelease.cpp)
        // IMPORTANT: Only block if module is enabled AND wasUsingItem is true
        if (::noItemRelease && ::noItemRelease->enabled && ::noItemRelease->wasUsingItem) {
            jclass cls = env->GetObjectClass(packet);
            if (cls) {
                const char* className = nullptr;
                // Get class name
                jclass clsCls = env->GetObjectClass(cls);
                if (clsCls) {
                    jmethodID mid = env->GetMethodID(clsCls, "getName", "()Ljava/lang/String;");
                    if (mid) {
                        jstring jname = (jstring)env->CallObjectMethod(cls, mid);
                        if (jname) {
                            const char* cstr = env->GetStringUTFChars(jname, nullptr);
                            if (cstr) {
                                // Check if it's C07PacketPlayerDigging
                                if (strstr(cstr, "C07PacketPlayerDigging") != nullptr) {
                                    bool shouldBlock = false;

                                    // Try 1.8.9+ method first: enum Action field
                                    jfieldID statusField = env->GetFieldID(
                                        cls, "status",
                                        "Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;");
                                    if (env->ExceptionCheck())
                                        env->ExceptionClear();

                                    if (statusField) {
                                        // 1.8.9+ path: enum field
                                        jobject statusObj = env->GetObjectField(packet, statusField);
                                        if (statusObj) {
                                            jclass statusCls = env->GetObjectClass(statusObj);
                                            if (statusCls) {
                                                jmethodID nameMethod =
                                                    env->GetMethodID(statusCls, "name", "()Ljava/lang/String;");
                                                if (nameMethod) {
                                                    jstring enumName =
                                                        (jstring)env->CallObjectMethod(statusObj, nameMethod);
                                                    if (enumName) {
                                                        const char* statusStr =
                                                            env->GetStringUTFChars(enumName, nullptr);
                                                        if (statusStr) {
                                                            shouldBlock = (strcmp(statusStr, "RELEASE_USE_ITEM") == 0);
                                                            env->ReleaseStringUTFChars(enumName, statusStr);
                                                        }
                                                        env->DeleteLocalRef(enumName);
                                                    }
                                                }
                                                env->DeleteLocalRef(statusCls);
                                            }
                                            env->DeleteLocalRef(statusObj);
                                        }
                                    } else {
                                        // 1.7.10 path: int field
                                        // In 1.7.10, the field is obfuscated as 'field_149508_e', not 'status'
                                        // Value 5 = RELEASE_USE_ITEM
                                        jfieldID intStatusField = env->GetFieldID(cls, "field_149508_e", "I");
                                        if (env->ExceptionCheck())
                                            env->ExceptionClear();

                                        // Fallback: try 'status' in case it's deobfuscated
                                        if (!intStatusField) {
                                            intStatusField = env->GetFieldID(cls, "status", "I");
                                            if (env->ExceptionCheck())
                                                env->ExceptionClear();
                                        }

                                        if (intStatusField) {
                                            jint statusValue = env->GetIntField(packet, intStatusField);
                                            // 5 = RELEASE_USE_ITEM in 1.7.10
                                            shouldBlock = (statusValue == 5);
                                        }
                                    }

                                    // Clean up and return if we should block
                                    env->ReleaseStringUTFChars(jname, cstr);
                                    env->DeleteLocalRef(jname);
                                    env->DeleteLocalRef(clsCls);
                                    env->DeleteLocalRef(cls);

                                    if (shouldBlock) {
                                        return false;
                                    }
                                    goto skip_cleanup;
                                }
                                env->ReleaseStringUTFChars(jname, cstr);
                            }
                            env->DeleteLocalRef(jname);
                        }
                    }
                    env->DeleteLocalRef(clsCls);
                }
            skip_cleanup:  // Jump here if we already cleaned up references
                env->DeleteLocalRef(cls);
            }
            // Clear any potential exceptions from the check
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
    }

    // ===== BLINK MOVEMENT PACKET LOGIC =====
    // Only process movement packets if Blink is active
    if (!hybridIntercept.load()) {
        return true;  // Passthrough for movement packets when Blink is off
    }

    // Check if this is a movement packet (original Blink logic)
    if (!isMovementPacket(env, packet)) {
        // Not a movement packet - let it through
        return true;
    }

    if (!packetFieldsReady.load()) {
        cachePacketFields(env);
    }

    bool hasPosPacket = false;
    if (cls_C04PacketPlayerPosition && env->IsInstanceOf(packet, cls_C04PacketPlayerPosition)) {
        hasPosPacket = true;
    } else if (cls_C06PacketPlayerPosLook && env->IsInstanceOf(packet, cls_C06PacketPlayerPosLook)) {
        hasPosPacket = true;
    }

    if (packetFieldsReady.load() && player.isValid() && hasPosPacket) {
        PacketPos desired{};
        if (!readPacketPos(env, packet, desired)) {
            desired.x = player.getPosX();
            desired.y = player.getPosY();
            desired.z = player.getPosZ();
        }

        PacketPos shadowSnapshot{};
        {
            std::lock_guard<std::mutex> lock(shadowMutex);
            shadowSnapshot = shadowPos;
        }

        PacketPos target{};
        size_t delay = blinkActive.load() ? kHybridDelayTicks : 0;
        {
            std::lock_guard<std::mutex> lock(desiredMutex);
            desiredQueue.push_back(desired);
            if (desiredQueue.size() > kHybridMaxQueue) {
                desiredQueue.pop_front();
            }

            if (desiredQueue.size() <= delay) {
                target = shadowSnapshot;
            } else {
                target = desiredQueue.front();
                desiredQueue.pop_front();
            }
        }

        PacketPos next{};
        {
            std::lock_guard<std::mutex> lock(shadowMutex);
            if (!shadowInitialized) {
                shadowPos = target;
                shadowInitialized = true;
            }
            next = stepToward(shadowPos, target, kHybridMaxSkipDist, kHybridMaxSkipY);
            shadowPos = next;
            lastSentPos = next;
            haveLastSent = true;
        }

        writePacketPos(env, packet, next);

        if (hybridRelease.load() && !blinkActive.load()) {
            bool queueEmpty = false;
            {
                std::lock_guard<std::mutex> lock(desiredMutex);
                queueEmpty = desiredQueue.empty();
            }
            PacketPos current{};
            current.x = player.getPosX();
            current.y = player.getPosY();
            current.z = player.getPosZ();
            if (queueEmpty && isSafeSkip(next, current)) {
                clearDesiredQueue();
                hybridRelease = false;
                hybridIntercept = false;
                // log_ok("[PacketInterceptor] Hybrid release complete - caught up to client");
            }
        }

        return true;
    }

    // Movement packet while hybrid is active - queue it
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobject globalRef = env->NewGlobalRef(packet);
        if (!globalRef) {
            return true;
        }
        QueuedPacket qp;
        qp.packet = globalRef;
        if (!packetFieldsReady.load()) {
            cachePacketFields(env);
        }
        if (packetFieldsReady.load()) {
            qp.hasPos = readPacketPos(env, packet, qp.pos);
        }
        packetQueue.push_back(qp);

        if (packetQueue.size() > kHybridMaxQueue) {
            env->DeleteGlobalRef(packetQueue.front().packet);
            packetQueue.erase(packetQueue.begin());
            // log_warn("[PacketInterceptor] Hybrid queue capped at %zu packets", kHybridMaxQueue);
        }

        static int debugCounter = 0;
        if (++debugCounter % 20 == 0 || packetQueue.size() <= 3) {
            // log_debug("[PacketInterceptor] Queued %s (total: %d)",
            getPacketTypeName(env, packet), (int)packetQueue.size();
        }
    }

    return false;  // Block this packet
}

}  // namespace PacketInterceptor