#include "java.h"
#include "mappings/cheatbreaker_1_7_10.hpp"
#include "mappings/cheatbreaker_1_8_9.hpp"
#include "../sdk/jni/jstring_util.h"
#include "../sdk/jni/member_resolver.h"
#include "../util/logger.hpp"
#include "../util/aes_string.hpp"
#include "../util/inject_trace.hpp"

#include <algorithm>
#include <cstring>
#include <deque>

namespace {
const char* notchClassName(const std::string& mcpName) {
    // 1.8.9 joined.srg class mappings for every Minecraft class referenced by
    // Cobble.  Vanilla/OptiFine ship these short Notch names at runtime.
    struct ClassMap {
        const char* mcp;
        const char* notch;
    };
    static constexpr ClassMap kClassMap[] = {
        {"net.minecraft.block.Block", "afh"},
        {"net.minecraft.block.state.IBlockState", "alz"},
        {"net.minecraft.client.entity.EntityOtherPlayerMP", "bex"},
        {"net.minecraft.client.entity.EntityPlayerSP", "bew"},
        {"net.minecraft.client.gui.GuiScreen", "axu"},
        {"net.minecraft.client.gui.inventory.GuiContainer", "ayl"},
        {"net.minecraft.client.gui.inventory.GuiInventory", "azc"},
        {"net.minecraft.client.Minecraft", "ave"},
        {"net.minecraft.client.multiplayer.PlayerControllerMP", "bda"},
        {"net.minecraft.client.multiplayer.WorldClient", "bdb"},
        {"net.minecraft.client.network.NetHandlerPlayClient", "bcy"},
        {"net.minecraft.client.network.NetworkPlayerInfo", "bdc"},
        {"net.minecraft.client.renderer.ActiveRenderInfo", "auz"},
        {"net.minecraft.client.renderer.entity.RendererLivingEntity", "bjl"},
        {"net.minecraft.client.renderer.entity.RenderManager", "biu"},
        {"net.minecraft.client.renderer.entity.RenderPlayer", "bln"},
        {"net.minecraft.client.renderer.EntityRenderer", "bfk"},
        {"net.minecraft.client.renderer.RenderGlobal", "bfr"},
        {"net.minecraft.client.settings.GameSettings", "avh"},
        {"net.minecraft.client.settings.KeyBinding", "avb"},
        {"net.minecraft.entity.Entity", "pk"},
        {"net.minecraft.entity.EntityLivingBase", "pr"},
        {"net.minecraft.entity.item.EntityArmorStand", "um"},
        {"net.minecraft.entity.item.EntityItem", "uz"},
        {"net.minecraft.entity.player.EntityPlayer", "wn"},
        {"net.minecraft.entity.player.InventoryPlayer", "wm"},
        {"net.minecraft.inventory.Container", "xi"},
        {"net.minecraft.inventory.Slot", "yg"},
        {"net.minecraft.item.Item", "zw"},
        {"net.minecraft.item.ItemPotion", "aai"},
        {"net.minecraft.item.ItemStack", "zx"},
        {"net.minecraft.nbt.NBTTagCompound", "dn"},
        {"net.minecraft.nbt.NBTTagList", "du"},
        {"net.minecraft.network.INetHandler", "ep"},
        {"net.minecraft.network.login.client.C00PacketLoginStart", "jl"},
        {"net.minecraft.network.NetworkManager", "ek"},
        {"net.minecraft.network.Packet", "ff"},
        {"net.minecraft.network.play.INetHandlerPlayClient", "fj"},
        {"net.minecraft.network.play.client.C03PacketPlayer", "ip"},
        {"net.minecraft.network.play.client.C0DPacketCloseWindow", "il"},
        {"net.minecraft.network.play.client.C16PacketClientStatus", "ig"},
        {"net.minecraft.network.play.client.C16PacketClientStatus$EnumState", "ig$a"},
        {"net.minecraft.network.play.server.S04PacketEntityEquipment", "hn"},
        {"net.minecraft.network.play.server.S08PacketPlayerPosLook", "fi"},
        {"net.minecraft.network.play.server.S0BPacketAnimation", "fq"},
        {"net.minecraft.network.play.server.S14PacketEntity", "gv"},
        {"net.minecraft.network.play.server.S18PacketEntityTeleport", "hz"},
        {"net.minecraft.network.play.server.S1DPacketEntityEffect", "ib"},
        {"net.minecraft.network.play.server.S1EPacketRemoveEntityEffect", "hc"},
        {"net.minecraft.potion.PotionEffect", "pf"},
        {"net.minecraft.util.AxisAlignedBB", "aug"},
        {"net.minecraft.util.BlockPos", "cj"},
        {"net.minecraft.util.EnumFacing", "cq"},
        {"net.minecraft.util.IChatComponent", "eu"},
        {"net.minecraft.util.MouseHelper", "avf"},
        {"net.minecraft.util.MovingObjectPosition", "auh"},
        {"net.minecraft.util.MovingObjectPosition$MovingObjectType", "auh$a"},
        {"net.minecraft.util.Timer", "avl"},
        {"net.minecraft.util.Vec3", "aui"},
        {"net.minecraft.world.World", "adm"},
    };

    for (const auto& entry : kClassMap) {
        if (mcpName == entry.mcp)
            return entry.notch;
    }
    return nullptr;
}

constexpr const mappings::RuntimeMappings* kRenamingClients[] = {&mappings::kCheatBreaker189,
                                                                  &mappings::kCheatBreaker1710};

const char* runtimeClassName(const mappings::RuntimeMappings& table, const std::string& mcpSlashName) {
    for (const auto& entry : table.classes) {
        if (mcpSlashName == entry.mcp)
            return entry.runtime;
    }
    return nullptr;
}

const char* mcpClassName(const mappings::RuntimeMappings& table, const std::string& runtimeName) {
    for (const auto& entry : table.classes) {
        if (runtimeName == entry.runtime)
            return entry.mcp;
    }
    return nullptr;
}

// Runtime internal name of a loaded class ("Lfoo/Bar;" -> "foo/Bar").
std::string internalName(jclass cls) {
    char* sig = nullptr;
    if (Java::tiEnv->GetClassSignature(cls, &sig, nullptr) != JVMTI_ERROR_NONE || !sig)
        return {};
    std::string name(sig);
    Java::tiEnv->Deallocate(reinterpret_cast<unsigned char*>(sig));
    return name.size() > 2 ? name.substr(1, name.size() - 2) : std::string{};
}

// JNI resolves members through superclasses and superinterfaces, so the table
// lookup walks the same hierarchy and matches on the declaring MCP class.
const mappings::MemberMapping* findMember(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    const mappings::RuntimeMappings& table = *Java::Mappings;
    std::deque<jclass> pending{static_cast<jclass>(env->NewLocalRef(cls))};
    const mappings::MemberMapping* found = nullptr;

    while (!pending.empty()) {
        jclass cur = pending.front();
        pending.pop_front();
        if (!found) {
            if (const char* owner = mcpClassName(table, internalName(cur))) {
                for (const auto& member : table.members) {
                    if (std::strcmp(member.owner, owner) == 0 && std::strcmp(member.desc, sig) == 0 &&
                        (std::strcmp(member.mcpName, name) == 0 || std::strcmp(member.srgName, name) == 0)) {
                        found = &member;
                        break;
                    }
                }
            }
            if (!found) {
                if (jclass parent = env->GetSuperclass(cur))
                    pending.push_back(parent);
                jint count = 0;
                jclass* interfaces = nullptr;
                if (Java::tiEnv->GetImplementedInterfaces(cur, &count, &interfaces) == JVMTI_ERROR_NONE && interfaces) {
                    for (jint i = 0; i < count; ++i)
                        pending.push_back(interfaces[i]);
                    Java::tiEnv->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
                }
            }
        }
        env->DeleteLocalRef(cur);
    }
    return found;
}

template <typename Lookup>
auto lookupMember(JNIEnv* env, jclass cls, const char* name, const char* sig, Lookup jniLookup) {
    if (!Java::Mappings)
        return jniLookup(name, sig);
    if (const auto* member = findMember(env, cls, name, sig))
        return jniLookup(member->runtimeName, member->runtimeDesc);
    return jniLookup(name, Java::RuntimeDescriptor(sig).c_str());
}

jobject loadClass(JNIEnv* env, const std::string& name) {
    jstring className = env->NewStringUTF(name.c_str());
    if (!className)
        return nullptr;

    jobject result =
        Java::MidLoadClass ? env->CallObjectMethod(Java::ClassLoader, Java::MidLoadClass, className) : nullptr;
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!result && Java::MidFindClass)
        result = env->CallObjectMethod(Java::ClassLoader, Java::MidFindClass, className);
    env->DeleteLocalRef(className);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return result;
}
}  // namespace

void setupClassLoader() {
    COBBLE_TRACE("setupClassLoader: enter");
    JNIEnv* env = Java::Env();
    if (!env || !Java::tiEnv) {
        COBBLE_TRACE("setupClassLoader: env or tiEnv is nullptr");
        return;
    }

    COBBLE_TRACE("setupClassLoader: using JVMTI GetLoadedClasses");
    jclass* classes = nullptr;
    jint classCount = 0;
    jvmtiError err = Java::tiEnv->GetLoadedClasses(&classCount, &classes);
    if (err != JVMTI_ERROR_NONE || !classes) {
        COBBLE_TRACE("setupClassLoader: GetLoadedClasses failed");
        return;
    }

    jobject targetLoader = nullptr;
    for (jint i = 0; i < classCount; ++i) {
        char* sigPtr = nullptr;
        if (Java::tiEnv->GetClassSignature(classes[i], &sigPtr, nullptr) == JVMTI_ERROR_NONE && sigPtr) {
            std::string sig = sigPtr;
            Java::tiEnv->Deallocate((unsigned char*)sigPtr);

            // Match net/minecraft/client/Minecraft (deobf/Lunar/Forge) or Notch obfuscated names:
            // 1.8.9 = Lave;
            // 1.7.10 = Lbao;
            // 1.12 = Lbib;
            const mappings::RuntimeMappings* renamingClient = nullptr;
            for (const auto* table : kRenamingClients) {
                const char* minecraft = runtimeClassName(*table, "net/minecraft/client/Minecraft");
                if (minecraft && sig == std::string("L") + minecraft + ";")
                    renamingClient = table;
            }
            if (sig == "Lnet/minecraft/client/Minecraft;" || sig == "Lave;" || sig == "Lbao;" || sig == "Lbib;" ||
                renamingClient) {
                jobject loader = nullptr;
                if (Java::tiEnv->GetClassLoader(classes[i], &loader) == JVMTI_ERROR_NONE && loader) {
                    targetLoader = loader;
                    Java::RuntimeObfuscated = (sig == "Lave;" || sig == "Lbao;" || sig == "Lbib;");
                    Java::Mappings = renamingClient;
                    COBBLE_TRACE("setupClassLoader: found Minecraft ClassLoader via JVMTI!");
                    break;
                }
            }
        }
    }

    // Clean up classes list
    for (jint i = 0; i < classCount; ++i) {
        env->DeleteLocalRef(classes[i]);
    }
    Java::tiEnv->Deallocate((unsigned char*)classes);

    if (targetLoader) {
        Java::ClassLoader = env->NewGlobalRef(targetLoader);
        env->DeleteLocalRef(targetLoader);

        jclass c_ClassLoader = env->FindClass(OBFUSCATE_STRING("java/lang/ClassLoader").c_str());
        if (c_ClassLoader) {
            // ClassLoader.findClass is not a general class lookup: it bypasses
            // parent delegation. Minecraft is commonly owned by a parent or
            // sibling launch loader, so use the public delegating API first.
            Java::MidLoadClass = env->GetMethodID(c_ClassLoader, OBFUSCATE_STRING("loadClass").c_str(),
                                                  OBFUSCATE_STRING("(Ljava/lang/String;)Ljava/lang/Class;").c_str());
            Java::MidFindClass = env->GetMethodID(c_ClassLoader, OBFUSCATE_STRING("findClass").c_str(),
                                                  OBFUSCATE_STRING("(Ljava/lang/String;)Ljava/lang/Class;").c_str());
            env->DeleteLocalRef(c_ClassLoader);
        }
    }

    Java::Initialized =
        (Java::ClassLoader != nullptr && (Java::MidLoadClass != nullptr || Java::MidFindClass != nullptr));
    COBBLE_TRACE(Java::Initialized ? "setupClassLoader: success" : "setupClassLoader: failed (no ClassLoader loaded)");
}

void Java::Init() {
    Java::Initialized = false;
    COBBLE_TRACE("Java::Init: enter");
    log_debug("starting java init...");

    // Check if there is any Java VMs in the injected thread
    jsize count = 0;
    COBBLE_TRACE("Java::Init: calling JNI_GetCreatedJavaVMs");
    jint getVMRes = JNI_GetCreatedJavaVMs(&vm, 1, &count);

    char traceBuf[128];
    wsprintfA(traceBuf, "Java::Init: JNI_GetCreatedJavaVMs returned %d, count = %d", getVMRes, count);
    COBBLE_TRACE(traceBuf);

    if (getVMRes != JNI_OK || count == 0) {
        log_err("no java vm found");
        return;
    }
    log_ok("found java vm");

    // Get env for current thread (will attach if needed)
    COBBLE_TRACE("Java::Init: getting env");
    JNIEnv* env = Env();
    if (!env) {
        COBBLE_TRACE("Java::Init: Env() returned nullptr");
        log_err("failed to get jni env");
        return;
    }
    log_ok("attached to java");

    COBBLE_TRACE("Java::Init: getting JVMTI env");
    jint tiRes = vm->GetEnv((void**)&Java::tiEnv, JVMTI_VERSION_1_2);
    wsprintfA(traceBuf, "Java::Init: GetEnv for JVMTI returned %d, tiEnv = %p", tiRes, Java::tiEnv);
    COBBLE_TRACE(traceBuf);

    if (tiRes != JNI_OK || Java::tiEnv == nullptr) {
        log_warn("jvmti not available (not critical)");
    } else {
        // Request JVMTI capabilities - try each individually since all-at-once may fail
        auto tryAddCap = [](jvmtiEnv* ti, const char* name, auto setter) {
            jvmtiCapabilities caps;
            memset(&caps, 0, sizeof(caps));
            setter(caps);
            jvmtiError err = ti->AddCapabilities(&caps);
            if (err == JVMTI_ERROR_NONE) {
                log_debug("JVMTI cap '%s': granted", name);
                return true;
            } else {
                log_debug("JVMTI cap '%s': denied (err=%d)", name, err);
                return false;
            }
        };

        // Try to add each capability
        tryAddCap(Java::tiEnv, "retransform_classes", [](jvmtiCapabilities& c) { c.can_retransform_classes = 1; });
        tryAddCap(Java::tiEnv, "redefine_classes", [](jvmtiCapabilities& c) { c.can_redefine_classes = 1; });
        tryAddCap(Java::tiEnv, "class_hook_events",
                  [](jvmtiCapabilities& c) { c.can_generate_all_class_hook_events = 1; });
        tryAddCap(Java::tiEnv, "method_entry_events",
                  [](jvmtiCapabilities& c) { c.can_generate_method_entry_events = 1; });
        tryAddCap(Java::tiEnv, "method_exit_events",
                  [](jvmtiCapabilities& c) { c.can_generate_method_exit_events = 1; });
        tryAddCap(Java::tiEnv, "force_early_return", [](jvmtiCapabilities& c) { c.can_force_early_return = 1; });
        tryAddCap(Java::tiEnv, "access_local_vars", [](jvmtiCapabilities& c) { c.can_access_local_variables = 1; });

        // Check what we actually got
        jvmtiCapabilities gotCaps;
        memset(&gotCaps, 0, sizeof(gotCaps));
        Java::tiEnv->GetCapabilities(&gotCaps);
        log_ok("JVMTI final caps: retransform=%d redefine=%d hooks=%d entry=%d force=%d locals=%d",
               gotCaps.can_retransform_classes, gotCaps.can_redefine_classes,
               gotCaps.can_generate_all_class_hook_events, gotCaps.can_generate_method_entry_events,
               gotCaps.can_force_early_return, gotCaps.can_access_local_variables);
    }

    log_debug("setting up classloader...");
    COBBLE_TRACE("Java::Init: calling setupClassLoader");
    setupClassLoader();

    if (!Java::Initialized) {
        COBBLE_TRACE("Java::Init: setupClassLoader failed");
        log_err("classloader setup failed");
        return;
    }
    COBBLE_TRACE("Java::Init: classloader ok, getting minecraft version");
    log_ok("classloader ready");

    GetMinecraftVersion();
    COBBLE_TRACE("Java::Init: exit");
}

void Java::Kill() {
    if (vm) {
        vm->DetachCurrentThread();
    }
}

bool Java::AssignClass(std::string name, jclass& out) {
    JNIEnv* env = Env();
    if (!env || !Java::ClassLoader || (!Java::MidLoadClass && !Java::MidFindClass))
        return false;

    // loadClass is required for class-loader delegation.  The findClass retry
    // retains compatibility with launchers that expose their game classes only
    // through a custom findClass implementation.
    if (Java::Mappings) {
        std::string slashName = name;
        std::replace(slashName.begin(), slashName.end(), '.', '/');
        if (const char* runtime = runtimeClassName(*Java::Mappings, slashName)) {
            std::string binaryName = runtime;
            std::replace(binaryName.begin(), binaryName.end(), '/', '.');
            name = binaryName;
        }
    }

    jobject findClass = loadClass(env, name);

    // Vanilla 1.8.9 exposes Notch class names (ave, pr, bfr, ...). The rest
    // of the SDK deliberately uses MCP names, so translate only after the MCP
    // lookup failed and only when the detected Minecraft class proves this is
    // an obfuscated runtime.
    if (!findClass && Java::RuntimeObfuscated) {
        if (const char* notchName = notchClassName(name))
            findClass = loadClass(env, notchName);
    }

    if (findClass) {
        out = (jclass)findClass;
        return true;
    }

    return false;
}

std::string Java::RuntimeDescriptor(const std::string& descriptor) {
    std::string translated;
    translated.reserve(descriptor.size());

    for (size_t i = 0; i < descriptor.size();) {
        if (descriptor[i] != 'L') {
            translated.push_back(descriptor[i++]);
            continue;
        }

        const size_t end = descriptor.find(';', i);
        if (end == std::string::npos) {
            translated.append(descriptor, i, std::string::npos);
            break;
        }

        std::string className = descriptor.substr(i + 1, end - i - 1);
        const char* runtimeName = nullptr;
        if (Java::Mappings) {
            runtimeName = runtimeClassName(*Java::Mappings, className);
        } else {
            std::replace(className.begin(), className.end(), '/', '.');
            runtimeName = notchClassName(className);
        }
        if (runtimeName) {
            translated.push_back('L');
            translated.append(runtimeName);
            translated.push_back(';');
        } else {
            translated.append(descriptor, i, end - i + 1);
        }
        i = end + 1;
    }

    return translated;
}

jfieldID Java::GetFieldID(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    return lookupMember(env, cls, name, sig, [&](const char* n, const char* s) { return env->GetFieldID(cls, n, s); });
}

jfieldID Java::GetStaticFieldID(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    return lookupMember(env, cls, name, sig,
                        [&](const char* n, const char* s) { return env->GetStaticFieldID(cls, n, s); });
}

jmethodID Java::GetMethodID(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    return lookupMember(env, cls, name, sig, [&](const char* n, const char* s) { return env->GetMethodID(cls, n, s); });
}

jmethodID Java::GetStaticMethodID(JNIEnv* env, jclass cls, const char* name, const char* sig) {
    return lookupMember(env, cls, name, sig,
                        [&](const char* n, const char* s) { return env->GetStaticMethodID(cls, n, s); });
}

bool Java::IsRuntimeMember(const char* mcpOwner, const char* mcpName, const std::string& name,
                           const std::string& desc) {
    if (!Java::Mappings)
        return false;
    for (const auto& member : Java::Mappings->members) {
        if (std::strcmp(member.owner, mcpOwner) == 0 && std::strcmp(member.mcpName, mcpName) == 0 &&
            name == member.runtimeName && desc == member.runtimeDesc)
            return true;
    }
    return false;
}

bool Java::IsRuntimeClass(const char* internalName, const char* mcpSlashName) {
    if (!internalName)
        return false;
    if (std::strcmp(internalName, mcpSlashName) == 0)
        return true;
    const char* runtime = Java::Mappings ? runtimeClassName(*Java::Mappings, mcpSlashName) : nullptr;
    return runtime && std::strcmp(internalName, runtime) == 0;
}

void Java::GetMinecraftVersion() {
    Java::Version = MinecraftVersion::UNDEFINED;

    // Renaming clients report their own brand as the launched version.
    if (Java::Mappings) {
        Java::Version = Java::Mappings->version;
        log_ok("detected cheatbreaker %s",
               Java::Version == MinecraftVersion::MC_1_8_9 ? "1.8.9" : "1.7.10");
        return;
    }

    JNIEnv* env = Env();
    if (!env)
        return;

    jclass minecraftClass = nullptr;
    if (!AssignClass(OBFUSCATE_STRING("net.minecraft.client.Minecraft"), minecraftClass))
        return;

    jmethodID getMinecraft = env->GetStaticMethodID(minecraftClass, OBFUSCATE_STRING("getMinecraft").c_str(),
                                                    OBFUSCATE_STRING("()Lnet/minecraft/client/Minecraft;").c_str());
    if (!getMinecraft) {
        env->ExceptionClear();
        getMinecraft = env->GetStaticMethodID(minecraftClass, OBFUSCATE_STRING("func_71410_x").c_str(),
                                              OBFUSCATE_STRING("()Lnet/minecraft/client/Minecraft;").c_str());
    }
    if (!getMinecraft && Java::RuntimeObfuscated) {
        env->ExceptionClear();
        getMinecraft = env->GetStaticMethodID(minecraftClass, "A", "()Lave;");
        if (!getMinecraft) {
            env->ExceptionClear();
            getMinecraft = env->GetStaticMethodID(minecraftClass, "A", "()Lbao;");
        }
    }
    if (!getMinecraft) {
        env->ExceptionClear();
        getMinecraft =
            jni_resolver::findUniqueMethodBySignature(minecraftClass, "()Lnet/minecraft/client/Minecraft;", true);
    }
    if (!getMinecraft) {
        env->ExceptionClear();
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    jobject minecraft = env->CallStaticObjectMethod(minecraftClass, getMinecraft);
    if (!minecraft) {
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    jfieldID launchedVersionField = env->GetFieldID(minecraftClass, OBFUSCATE_STRING("launchedVersion").c_str(),
                                                    OBFUSCATE_STRING("Ljava/lang/String;").c_str());
    if (!launchedVersionField) {
        env->ExceptionClear();
        launchedVersionField = env->GetFieldID(minecraftClass, OBFUSCATE_STRING("field_110446_Y").c_str(),
                                               OBFUSCATE_STRING("Ljava/lang/String;").c_str());
    }
    if (!launchedVersionField) {
        env->ExceptionClear();
        // Some Badlion builds remove the version field. BlockPos is present
        // in 1.8.9 and absent in 1.7.10, so the class split is deterministic.
        jclass blockPos = nullptr;
        if (AssignClass(OBFUSCATE_STRING("net.minecraft.util.BlockPos"), blockPos)) {
            Java::Version = MinecraftVersion::MC_1_8_9;
            log_ok("detected minecraft 1.8.9 (class fallback)");
            env->DeleteLocalRef(blockPos);
        } else {
            if (blockPos)
                env->DeleteLocalRef(blockPos);
            Java::Version = MinecraftVersion::MC_1_7_10;
            log_ok("detected minecraft 1.7.10 (class fallback)");
        }
        env->DeleteLocalRef(minecraft);
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    jstring versionStr = (jstring)env->GetObjectField(minecraft, launchedVersionField);
    if (!versionStr) {
        env->DeleteLocalRef(minecraft);
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    std::string version = jstringToString(versionStr);

    if (version.find("1.8.9") == 0) {
        Java::Version = MinecraftVersion::MC_1_8_9;
        log_ok("detected minecraft 1.8.9");
    } else if (version.find("1.7.10") == 0) {
        Java::Version = MinecraftVersion::MC_1_7_10;
        log_ok("detected minecraft 1.7.10");
    } else {
        log_warn("unknown minecraft version: %s", version.c_str());
    }

    env->DeleteLocalRef(versionStr);
    env->DeleteLocalRef(minecraft);
    env->DeleteLocalRef(minecraftClass);
}

JNIEnv* Java::Env() {
    if (!vm)
        return nullptr;

    JNIEnv* env = nullptr;
    jint status = vm->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    } else if (status != JNI_OK) {
        return nullptr;
    }

    return env;
}
