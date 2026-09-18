#include "blockhit.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/aes_string.hpp"
#include "../../util/network/json.hpp"
#include "aimassist.h"
#include "autoclicker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

BlockHit* blockHit = nullptr;

namespace {

// How long a hit taken keeps "somebody is on us" true. 40 ticks = 2 s, about one exchange.
// This is what lets the block go up before we have clicked once, when we are the one being
// jumped, and what keeps the pattern running through the part of a fight where we are only
// taking hits.
constexpr std::uint32_t kPressureTicks = 40;

bool clearEx(JNIEnv* env) {
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

bool gameFocused() {
    const HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    // Process IDs, not window handles. Lunar and borderless-fullscreen can hand the
    // foreground to a different top-level window inside the same process, so an HWND equality
    // test reads as "unfocused" forever and silently disables the module. This is the same
    // test the keybind dispatcher already trusts.
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid != 0 && fgPid == GetCurrentProcessId();
}

}  // namespace

std::uint64_t BlockHit::nowMs() {
    return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

BlockHit::BlockHit() : Module("BlockHit"), rng_((unsigned)nowMs()) {
    keybind = 0;
    rollCycle();
}

void BlockHit::onEnable() {
    tick_ = 0;
    blockedAtTick_ = 0;
    releasedAtTick_ = 0;
    lastHitTakenTick_ = 0;
    haveTakenHit_ = false;
    prevHurtTime_ = 0;
    beatsHeld_ = 0;
    holePending_ = false;
    immuneHole_ = false;
    wasSprinting_ = false;
    prevAttackDown_ = false;
    observedAutoClick_ = autoClicker ? autoClicker->lastAutoClick : std::chrono::steady_clock::time_point{};
    attackSignals_.store(0, std::memory_order_release);
    gateReason_.store(nullptr, std::memory_order_release);
    rollCycle();
    // serverBlocking_ is NOT cleared here. If the module is toggled off and straight back on
    // inside one tick, whatever is outstanding is still outstanding, and forgetting it would
    // leave the server blocking for an hour.
}

// Deliberately no JNI and no packets. This runs on the module thread, and the only safe place
// to touch the JVM is the client tick -- which keeps being dispatched while the module is off
// precisely because needsClientTick() stays true until nothing is outstanding. The next tick
// gates as "Off" and stands down properly.
void BlockHit::onDisable() {
    attackSignals_.store(0, std::memory_order_release);
    gateReason_.store(nullptr, std::memory_order_release);
    prevAttackDown_ = false;
}

// Reset, not release. The EntityPlayerMP that owned the server-side itemInUse went with the
// world, so a C07 addressed to it is noise at best -- and at worst it arrives after the new
// player has spawned and cancels a block the user started themselves. The Java handles go
// too: a dimension change keeps the classes, but a full reconnect does not.
void BlockHit::onWorldChange() {
    serverBlocking_.store(false, std::memory_order_release);
    gateReason_.store(nullptr, std::memory_order_release);
    blockedAtTick_ = 0;
    releasedAtTick_ = 0;
    lastHitTakenTick_ = 0;
    haveTakenHit_ = false;
    prevHurtTime_ = 0;
    beatsHeld_ = 0;
    holePending_ = false;
    immuneHole_ = false;
    wasSprinting_ = false;
    apiTried_ = false;
    apiUsable_ = false;
    midOnStoppedUsing_ = nullptr;
    fMovementInput_ = nullptr;
    fInputStrafe_ = nullptr;
    fInputForward_ = nullptr;
    fSprintToggle_ = nullptr;
    fRidingEntity_ = nullptr;
    fItemInUseCount_ = nullptr;
    fakingBlock_ = false;
    lagTicksRemaining_ = 0;
    rollCycle();
}

// ---------------------------------------------------------------------------
// Module thread (~500 Hz). Latch beats only; nothing here touches the game.
// ---------------------------------------------------------------------------

void BlockHit::onUpdate() {
    if (!enabled)
        return;

    // Attack edges. Vanilla only attacks an entity on a fresh keyBindAttack press, so an edge
    // stands in one-to-one for an outgoing C02 ATTACK.
    const bool attackDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && gameFocused();
    if (attackDown && !prevAttackDown_)
        attackSignals_.fetch_add(1, std::memory_order_acq_rel);
    prevAttackDown_ = attackDown;

    if (autoClicker && autoClicker->lastAutoClick != observedAutoClick_) {
        observedAutoClick_ = autoClicker->lastAutoClick;
        attackSignals_.fetch_add(1, std::memory_order_acq_rel);
    }
}

// ---------------------------------------------------------------------------
// The pattern. Five numbers, drawn fresh after every release.
// ---------------------------------------------------------------------------

void BlockHit::rollCycle() {
    std::uniform_int_distribution<int> pick(0, 99);

    const int beats = pick(rng_);
    beatsPerHole_ = (beats < 22) ? 3u : (beats < 58 ? 4u : (beats < 84 ? 5u : 6u));

    minHoldTicks_ = 2u + (std::uint32_t)(pick(rng_) % 3);

    // Slinky Maximum hold duration with probabilistic rounding of fractional values
    const int baseHold = static_cast<int>(settings.maxHoldTime);
    const float frac = settings.maxHoldTime - static_cast<float>(baseHold);
    maxHoldTicks_ =
        static_cast<std::uint32_t>((std::max)(1, baseHold + (pick(rng_) < static_cast<int>(frac * 100.0f) ? 1 : 0)));
    minHoldTicks_ = (std::min)(minHoldTicks_, maxHoldTicks_);

    // Slinky Reblock Instantly: eliminate cooldown delay
    if (settings.lagReblock) {
        holeTicks_ = 0;
    } else {
        const int hole = pick(rng_);
        if (hole < 86)
            holeTicks_ = 4u + (std::uint32_t)(pick(rng_) % 5);
        else
            holeTicks_ = 9u + (std::uint32_t)(pick(rng_) % 6);
    }

    immuneMargin_ = 2u + (std::uint32_t)(pick(rng_) % 4);
}

// ---------------------------------------------------------------------------
// What has to be true for a cycle to start at all.
// ---------------------------------------------------------------------------

bool BlockHit::isSwordId(int itemId) {
    // wood / stone / iron / diamond / gold. ItemSword is the only vanilla item whose
    // use-action is BLOCK, so this list is exactly the set that can block.
    return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283;
}

bool BlockHit::isHoldingSword() const {
    return isSwordId(player.getHeldItemId());
}

bool BlockHit::fightLive() const {
    if (!player.isValid())
        return false;

    // Check damage condition
    const int hurt = player.getHurtTime();
    if (settings.requireDamage && hurt <= 0 && (!haveTakenHit_ || (tick_ - lastHitTakenTick_) > kPressureTicks))
        return false;

    // Reject values above Maximum hurt time
    if (hurt > settings.maxHurtTime)
        return false;

    // Manual play. Minecraft.clickMouse only turns a click into a C02 ATTACK when
    // objectMouseOver is an entity, so this is the same question vanilla already asked.
    if (mc.isTargetingEntity())
        return true;

    // AimAssist only rewrites rotation while it is actually steering at a target
    if (aimAssist && aimAssist->isRewritingRotation())
        return true;

    // Someone is hitting us
    if (haveTakenHit_ && (tick_ - lastHitTakenTick_) <= kPressureTicks)
        return true;

    // Target within Range
    double px = player.getPosX();
    double py = player.getPosY();
    double pz = player.getPosZ();
    double maxDistSq = static_cast<double>(settings.range * settings.range);

    const VisualSnapshot snap = VisualCache::GetSnapshot();
    for (const auto& target : snap.players) {
        if (target.entityId == 0)
            continue;
        double dx = target.posX - px;
        double dy = target.posY - py;
        double dz = target.posZ - pz;
        if ((dx * dx + dy * dy + dz * dz) <= maxDistSq)
            return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// The mechanism. One thing it cannot work without -- a way to send the release -- plus the
// fields vanilla's own slowdown is written through, because a block we cannot pay for is the
// physics mismatch this module was rewritten to stop presenting.
// ---------------------------------------------------------------------------

bool BlockHit::resolveApi(JNIEnv* env, jobject playerInstance) {
    if (apiTried_)
        return apiUsable_;
    if (!env || !playerInstance || !mc.instance || !JniCache::fid_playerController)
        return false;  // not ready yet; retry next tick rather than latching a failure

    jclass playerCls = env->GetObjectClass(playerInstance);
    if (clearEx(env) || !playerCls)
        return false;

    // Deobfuscated name first, then the MCP field name. Lunar, Forge and vanilla between them
    // ship both, and GetFieldID walks superclasses, so a remapped subclass costs nothing.
    // The names arrive already obfuscated: OBFUSCATE_STRING needs a literal at the call site,
    // because in a packed build it is a compile-time AES over sizeof(str), not a runtime call.
    const auto field = [&](jclass cls, const std::string& name, const std::string& srg,
                           const std::string& sig) -> jfieldID {
        jfieldID id = env->GetFieldID(cls, name.c_str(), sig.c_str());
        if (clearEx(env) || !id) {
            id = env->GetFieldID(cls, srg.c_str(), sig.c_str());
            clearEx(env);
        }
        return id;
    };

    fMovementInput_ = field(playerCls, OBFUSCATE_STRING("movementInput"), OBFUSCATE_STRING("field_71158_b"),
                            OBFUSCATE_STRING("Lnet/minecraft/util/MovementInput;"));
    fSprintToggle_ = field(playerCls, OBFUSCATE_STRING("sprintToggleTimer"), OBFUSCATE_STRING("field_71503_h"),
                           OBFUSCATE_STRING("I"));
    // GetFieldID walks superclasses, so Entity.ridingEntity resolves off the player class.
    fRidingEntity_ = field(playerCls, OBFUSCATE_STRING("ridingEntity"), OBFUSCATE_STRING("field_70154_o"),
                           OBFUSCATE_STRING("Lnet/minecraft/entity/Entity;"));
    fItemInUseCount_ =
        field(playerCls, OBFUSCATE_STRING("itemInUseCount"), OBFUSCATE_STRING("field_71072_f"), OBFUSCATE_STRING("I"));

    if (fMovementInput_) {
        jobject input = env->GetObjectField(playerInstance, fMovementInput_);
        if (!clearEx(env) && input) {
            jclass inputCls = env->GetObjectClass(input);
            if (!clearEx(env) && inputCls) {
                fInputStrafe_ = field(inputCls, OBFUSCATE_STRING("moveStrafe"), OBFUSCATE_STRING("field_78902_a"),
                                      OBFUSCATE_STRING("F"));
                fInputForward_ = field(inputCls, OBFUSCATE_STRING("moveForward"), OBFUSCATE_STRING("field_78900_b"),
                                       OBFUSCATE_STRING("F"));
                env->DeleteLocalRef(inputCls);
            }
        }
        if (input)
            env->DeleteLocalRef(input);
    }
    env->DeleteLocalRef(playerCls);

    jobject controller = env->GetObjectField(mc.instance, JniCache::fid_playerController);
    if (clearEx(env) || !controller)
        return false;  // no controller yet: still in the main menu
    jclass controllerCls = env->GetObjectClass(controller);
    if (!clearEx(env) && controllerCls) {
        const std::string playerSig = OBFUSCATE_STRING("(Lnet/minecraft/entity/player/EntityPlayer;)V");
        midOnStoppedUsing_ =
            env->GetMethodID(controllerCls, OBFUSCATE_STRING("onStoppedUsingItem").c_str(), playerSig.c_str());
        if (clearEx(env) || !midOnStoppedUsing_) {
            midOnStoppedUsing_ =
                env->GetMethodID(controllerCls, OBFUSCATE_STRING("func_78766_b").c_str(), playerSig.c_str());
            clearEx(env);
        }
        env->DeleteLocalRef(controllerCls);
    }
    env->DeleteLocalRef(controller);

    // Without the release there is no way back out of a block, and a block we cannot end is
    // strictly worse than one we never started. Without the movement input there is no way to pay
    // the slowdown, and an unpaid block is a physics mismatch on every tick of every hold -- the
    // exact defect this version exists to remove. Either one missing and the module reports
    // "No API" and does nothing at all. sprintToggleTimer is not in the test: it costs a
    // double-tap sprinter one extra tap, not a flag.
    apiUsable_ = midOnStoppedUsing_ != nullptr && fMovementInput_ != nullptr && fInputStrafe_ != nullptr &&
                 fInputForward_ != nullptr;
    apiTried_ = true;
    return apiUsable_;
}

bool BlockHit::clientUsingItem(JNIEnv* env, jobject playerInstance) const {
    if (!env || !playerInstance || !player.mid_isUsingItem)
        return false;
    const jboolean value = env->CallBooleanMethod(playerInstance, player.mid_isUsingItem);
    if (clearEx(env))
        return false;
    return value == JNI_TRUE;
}

// Vanilla's own lines, run late. EntityPlayerSP.onLivingUpdate does
//
//     if (this.isUsingItem() && !this.isRiding()) {
//         this.movementInput.moveStrafe  *= 0.2F;
//         this.movementInput.moveForward *= 0.2F;
//         this.sprintToggleTimer = 0;
//     }
//     ...
//     if (this.isSprinting() && (this.movementInput.moveForward < 0.8F || ...))
//         this.setSprinting(false);
//
// and all of it runs BEFORE this hook, which sits at the top of EntityLivingBase.onLivingUpdate
// -- reached from super.onLivingUpdate() at the end of the method above. A C08 sent from here
// would therefore land one tick in front of the cost that justifies it, and one tick of
// full-speed movement under a block the server has already registered is all a prediction engine
// needs. So the block tick runs the same lines itself, on the same fields, with the same values.
//
// It is not too late to matter. EntityPlayerSP.updateEntityActionState(), the whole body of which
// is copying movementInput into moveStrafing/moveForward, is called further down inside
// EntityLivingBase.onLivingUpdate -- after us -- and moveEntityWithHeading reads the result after
// that. This is the movement the tick really uses, which is the same reason anything that steers
// the walk direction writes to these fields and not the entity's.
//
// Exactly once per hold, on the raise tick and no other. From the next tick the client's itemInUse
// is genuinely set, so the branch above runs on its own before this hook is reached; applying it a
// second time would scale the input by 0.04 and be a mismatch in the other direction.
//
// Multiplying both axes by one factor leaves atan2(-s, f) untouched, so a heading chosen earlier in
// this same dispatch survives; only the magnitude changes.
void BlockHit::applyVanillaSlowdown(JNIEnv* env, jobject playerInstance) {
    if (!env || !playerInstance || !fMovementInput_ || !fInputStrafe_ || !fInputForward_)
        return;
    jobject input = env->GetObjectField(playerInstance, fMovementInput_);
    if (clearEx(env) || !input)
        return;

    const float strafe = env->GetFloatField(input, fInputStrafe_) * 0.2f;
    const float forward = env->GetFloatField(input, fInputForward_) * 0.2f;
    if (!clearEx(env)) {
        env->SetFloatField(input, fInputStrafe_, strafe);
        env->SetFloatField(input, fInputForward_, forward);
        clearEx(env);
    }
    env->DeleteLocalRef(input);

    // Zeroed by the same branch in vanilla. Nothing in this module depends on it -- it is the
    // double-tap sprint timer -- but leaving it running lets a tap taken during a block start a
    // sprint on a tick the server is predicting a blocked player.
    if (fSprintToggle_) {
        env->SetIntField(playerInstance, fSprintToggle_, 0);
        clearEx(env);
    }

    // The tail of the same method. 1.0 * 0.2 is 0.2, so the test is true on every tick a block is
    // up -- but it is written the way vanilla writes it, because being indistinguishable from
    // vanilla is the entire point. onUpdateWalkingPlayer runs later in this tick and turns the
    // change into C0BPacketEntityAction(STOP_SPRINTING) on its own.
    if (forward < 0.8f && player.isSprinting())
        player.setSprinting(false);
}

// Puts back exactly what vanilla took at the top of this tick, and reports whether the restored
// forward input is enough for vanilla's own sprint-start test.
//
// A hand's release goes out in Minecraft.runTick's mouse phase, before EntityPlayerSP's slowdown
// branch runs, so a human's release tick moves at FULL speed with the C07 already ahead of the
// position packet. Ours goes out from inside onLivingUpdate, after vanilla has already scaled the
// input for a block that is about to stop existing. Undoing the 0.2 makes our release tick the
// same tick a hand produces: same packet order, same movement behind it.
//
// x5 rather than a re-read of the keybinds, because 0.2f is exact enough that 0.2f * 5.0f is
// exactly 1.0f and 0.3f * 0.2f * 5.0f is exactly 0.3f -- the restored values land on the same grid
// points updatePlayerMoveState and MoveFix would have produced. Scaling whatever is there also
// means a sneak, a MoveFix rotation and anything else written this tick all survive it.
//
// `scaled` false means vanilla's branch did not run on this tick -- the player is on a mount, or an
// item switch ran EntityPlayer.onUpdate's clearItemInUse() before this hook -- and then there is
// nothing to give back. Multiplying an input vanilla never touched by five would be a five-fold
// movement input, which is a speed hack and not a fix, so that case only reads.
bool BlockHit::undoVanillaSlowdown(JNIEnv* env, jobject playerInstance, bool scaled) {
    if (!env || !playerInstance || !fMovementInput_ || !fInputStrafe_ || !fInputForward_)
        return false;
    jobject input = env->GetObjectField(playerInstance, fMovementInput_);
    if (clearEx(env) || !input)
        return false;

    const float factor = scaled ? 5.0f : 1.0f;
    const float strafe = env->GetFloatField(input, fInputStrafe_) * factor;
    const float forward = env->GetFloatField(input, fInputForward_) * factor;
    bool held = false;
    if (!clearEx(env)) {
        if (scaled) {
            env->SetFloatField(input, fInputStrafe_, strafe);
            env->SetFloatField(input, fInputForward_, forward);
        }
        held = !clearEx(env) && forward >= 0.8f;
    }
    env->DeleteLocalRef(input);
    return held;
}

// Vanilla's slowdown branch is `if (this.isUsingItem() && !this.isRiding())`, so a mount means a
// block with no cost attached -- the exact mismatch this version exists to stop presenting. Rather
// than reproduce the branch and then have to reproduce vehicle movement with it, the module simply
// declines to block while riding.
bool BlockHit::isRiding(JNIEnv* env, jobject playerInstance) const {
    if (!env || !playerInstance || !fRidingEntity_)
        return false;
    jobject mount = env->GetObjectField(playerInstance, fRidingEntity_);
    if (clearEx(env))
        return false;
    if (!mount)
        return false;
    env->DeleteLocalRef(mount);
    return true;
}

bool BlockHit::needsClientTick() const {
    // Still needed while switched off: an outstanding block is ours to lower, the release is
    // client-thread work, and nothing else will do it.
    return enabled || serverBlocking_.load(std::memory_order_acquire);
}

bool BlockHit::sendBlock(JNIEnv* env, jobject playerInstance) {
    if (!env || !playerInstance)
        return false;
    // Read before the packet, because sendUseItem runs useItemRightClick locally and the slowdown
    // this method applies a few lines down drops the sprint on the spot. The release needs to know
    // what it is putting back.
    wasSprinting_ = player.isSprinting();
    // C08PacketPlayerBlockPlacement with placedBlockDirection == 255. Server side that lands in
    // tryUseItem -> ItemSword.onItemRightClick -> setItemInUse(stack, 72000), and
    // EntityPlayer.isBlocking() is true from that instant.
    const bool reported = mc.sendUseHeldItem();
    // The reported value is not the test. sendUseItem returns whatever its "did the stack
    // change" comparison said, and a sword hands the same stack straight back, so it is false on
    // every successful block. The real confirmation is that useItemRightClick ran at all: it
    // sets the client's itemInUse, and it runs in the same method immediately after
    // addToSendQueue, so a client in use-item state means the C08 is on the wire.
    const bool took = reported || clientUsingItem(env, playerInstance);
    if (!took) {
        wasSprinting_ = false;
        return false;  // nothing went out; leave the model alone and retry on the next beat
    }
    // Nothing to paint. setItemInUse seeded itemInUseCount with 72000 on the line after the
    // packet, so ItemRenderer.renderItemInFirstPerson already has its `getItemInUseCount() > 0`
    // and RenderPlayer already has ArmPose.BLOCK. The picture is a consequence of the state now,
    // which is the whole point of this version.
    serverBlocking_.store(true, std::memory_order_release);
    blockedAtTick_ = tick_;
    // The beat that raised the block does not count against the hold: beatsPerHole_ beats have
    // to pass on top of it before a hole opens.
    beatsHeld_ = 0;
    holePending_ = false;
    immuneHole_ = false;
    // Pay for the tick vanilla could not charge us for, because its own branch ran before this
    // hook with itemInUse still null. From the next tick it charges us itself.
    applyVanillaSlowdown(env, playerInstance);
    return true;
}

void BlockHit::sendRelease(JNIEnv* env, jobject playerInstance, bool immune) {
    // State first, packet second. If the JNI call below throws or the controller has gone, the
    // model must still say "not blocking" -- retrying on the next beat is recoverable, but a
    // model stuck on true would never try again.
    serverBlocking_.store(false, std::memory_order_release);
    releasedAtTick_ = tick_;
    beatsHeld_ = 0;
    holePending_ = true;
    // A hole opened because a hit landed is timed off hurtTime, not off holeTicks_: it lasts as
    // long as the immunity that made it free.
    immuneHole_ = immune;
    // The next hole is a different hole. Drawn here rather than at the next block so its length
    // is already decided by the time the hole starts running.
    rollCycle();

    // Slinky Lag Phase: probabilistically initiate lag delay
    if (settings.lagChance > 0.0f) {
        std::uniform_int_distribution<int> pick(0, 99);
        if (pick(rng_) < static_cast<int>(settings.lagChance)) {
            lagTicksRemaining_ = static_cast<int>((std::max)(1.0f, settings.lagDuration));
        }
    }

    if (!env || !playerInstance || !midOnStoppedUsing_ || !mc.instance || !JniCache::fid_playerController) {
        wasSprinting_ = false;
        return;
    }
    jobject controller = env->GetObjectField(mc.instance, JniCache::fid_playerController);
    if (clearEx(env) || !controller) {
        wasSprinting_ = false;
        return;
    }
    // Read before the release, because stopUsingItem() nulls the itemInUse this asks about.
    // Vanilla's branch is `isUsingItem() && !isRiding()`, and both halves can be false on a
    // release tick: an item switch runs clearItemInUse() in EntityPlayer.onUpdate ahead of this
    // hook, and a mount skips the slowdown entirely. Undoing a scale that never happened would
    // hand the tick a five-fold movement input.
    const bool vanillaScaled = clientUsingItem(env, playerInstance) && !isRiding(env, playerInstance);
    // C07PacketPlayerDigging(RELEASE_USE_ITEM), then player.stopUsingItem() -> clearItemInUse(),
    // which zeroes itemInUseCount for us: the raised sword comes down on the same call that ends
    // the block, with nothing to paint either way.
    // NoItemRelease cannot swallow this: its gate only fires while physical RMB is down, and
    // this module stands down entirely in that case.
    env->CallVoidMethod(controller, midOnStoppedUsing_, playerInstance);
    clearEx(env);
    env->DeleteLocalRef(controller);

    // Vanilla already scaled this tick's input, for a block that no longer exists as of the packet
    // above. A hand's release tick moves at full speed, so ours does too.
    const bool forwardHeld = undoVanillaSlowdown(env, playerInstance, vanillaScaled);
    // The slowdown zeroed sprintToggleTimer, so a double-tap sprinter gets nothing back on their
    // own. Only with real forward input behind it: a sprint the server has no input for is a worse
    // mismatch than the one this module exists to avoid. onUpdateWalkingPlayer runs later in this
    // same tick and only emits C0BPacketEntityAction on a change, so START_SPRINTING lands
    // immediately behind the C07.
    if (wasSprinting_ && forwardHeld && !player.isSprinting())
        player.setSprinting(true);
    wasSprinting_ = false;
}

void BlockHit::standDown(JNIEnv* env, jobject playerInstance) {
    if (serverBlocking_.load(std::memory_order_acquire))
        sendRelease(env, playerInstance);
    beatsHeld_ = 0;
    // Forget the hole. A hole is a gap inside a live exchange; when the exchange is over the
    // next block should wait for a beat, not for a timer left running from the last fight.
    holePending_ = false;
    lagTicksRemaining_ = 0;
}

// The user pressed right-click, or another module put the client into use-item state. That
// action sent its own C08 in the input phase of THIS tick -- Minecraft.runTick handles the
// mouse before World.updateEntities, and our hook is inside updateEntities -- so the server's
// itemInUse is already theirs and ours no longer exists. There is nothing to release, and
// sending one anyway is actively harmful: onStoppedUsingItem finishes with
// playerIn.stopUsingItem(), which wipes the client copy they just created, so the block
// animation vanishes and vanilla will not right-click again until rightClickDelayTimer has
// counted back down from 4. That is 200 ms of delay on the first manual block of every fight.
// Drop ownership and say nothing.
//
// releasedAtTick_ is deliberately NOT stamped and the cycle is NOT rerolled. The gap exists to
// stop this module churning against itself; a hand-over is not churn, and stamping it would add
// more dead time before the block comes back after the user lets go. itemInUseCount is left
// alone for the same reason the release is: the taker's own right-click set it, vanilla will
// count it down and clear it, and zeroing it here would erase the animation of a block they
// really are holding.
void BlockHit::abandonBlock() {
    serverBlocking_.store(false, std::memory_order_release);
    beatsHeld_ = 0;
    holePending_ = false;
}

// ---------------------------------------------------------------------------
// Client thread, once per tick, from inside EntityLivingBase.onLivingUpdate. Every packet this
// module sends is sent here.
// ---------------------------------------------------------------------------

void BlockHit::onClientTick(JNIEnv* env) {
    if (!env)
        return;
    jobject self = player.instance;

    ++tick_;

    // Drain the latch first: a bail-out below must not leave stale signals to be replayed on
    // re-entry.
    std::uint32_t signals = attackSignals_.exchange(0, std::memory_order_acq_rel);
    // One tick either had a swing or it did not. A manual click and an AutoClicker click landing
    // together is one beat, not two -- counting it twice would end the cycle a swing early.
    bool beat = signals > 0;

    // Hits taken, from hurtTime's rising edge. attackEntityFrom sets it to maxHurtTime (10) on
    // every hit that actually lands and Entity.onEntityUpdate counts it back down, so a rise is
    // one incoming hit and nothing else -- no packet reading and no entity tracking. Their swing
    // is a beat of the pattern just as much as ours is.
    const int hurt = player.getHurtTime();
    const bool hitTaken = hurt > prevHurtTime_;
    if (hitTaken) {
        lastHitTakenTick_ = tick_;
        haveTakenHit_ = true;
        beat = true;
    }
    prevHurtTime_ = hurt;

    // Without a player there is nothing to address a packet to. Drop the model rather than
    // trying to release into a null entity -- on the next valid tick the server's own itemInUse
    // is gone with the EntityPlayerMP that held it.
    if (!player.isValid() || !self) {
        serverBlocking_.store(false, std::memory_order_release);
        beatsHeld_ = 0;
        holePending_ = false;
        gateReason_.store("No Player", std::memory_order_release);
        return;
    }

    // Lazily resolved, and retried until it succeeds: on the first ticks after a join the player
    // exists but playerController does not.
    resolveApi(env, self);

    // Each bail-out records WHY, so a standing-down module says so in the ArrayList instead of
    // looking like a working one.
    const bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (autoClicker && autoClicker->enabled);
    const bool rmbDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

    // If Require Right Click is active and RMB is held with a sword, suppress vanilla keyBindUseItem
    // so Minecraft's rightClickMouse() does not fight AutoBlock's block cycle
    if (settings.requireRMB && rmbDown && isHoldingSword() && mc.instance && JniCache::fid_gameSettings &&
        JniCache::fid_keyBindUseItem && JniCache::fid_pressed) {
        jobject gameSettings = env->GetObjectField(mc.instance, JniCache::fid_gameSettings);
        if (gameSettings && !clearEx(env)) {
            jobject keyBindUse = env->GetObjectField(gameSettings, JniCache::fid_keyBindUseItem);
            if (keyBindUse && !clearEx(env)) {
                env->SetBooleanField(keyBindUse, JniCache::fid_pressed, JNI_FALSE);
                clearEx(env);
                env->DeleteLocalRef(keyBindUse);
            }
            env->DeleteLocalRef(gameSettings);
        }
    }

    const bool clientUsing = clientUsingItem(env, self);
    // Ours is real now. clientUsingItem() is true for the whole of every hold BECAUSE of this
    // module, so the raw flag can no longer stand in for "somebody else owns the channel" -- read
    // straight it would gate the module Manual on the tick after its own block and hand the
    // channel over to itself. Only use-item state we did not create counts as foreign.
    const bool foreignUse = clientUsing && !serverBlocking_.load(std::memory_order_acquire);
    const bool manualBlock = (!settings.requireRMB && rmbDown) || foreignUse;

    const char* gate = nullptr;
    // Set when the channel is being taken over rather than vacated: hand it over silently
    // instead of releasing. See abandonBlock().
    bool handOver = false;
    if (!enabled)
        gate = "Off";
    else if (Menu::isOpen || mc.isInGui())
        gate = "GUI";
    else if (!gameFocused())
        gate = "Unfocused";
    else if (settings.requireLMB && !lmbDown)
        gate = "No LMB";
    else if (settings.requireRMB && !rmbDown)
        gate = "No RMB";
    else if (manualBlock) {
        gate = "Manual";
        handOver = foreignUse;
    }
    // Vanilla's slowdown branch is `isUsingItem() && !isRiding()`, so a block taken on a mount is
    // a block with no cost attached -- the exact mismatch this version exists to stop presenting.
    // Reproducing the branch would mean reproducing vehicle movement with it, so the module simply
    // declines to block from a saddle.
    else if (isRiding(env, self))
        gate = "Riding";
    // The server's itemInUse holds the stack captured when setItemInUse ran, and isBlocking()
    // tests THAT stack rather than the held one -- so a hotbar switch would leave us "blocking
    // with a sword" while a bow is in hand. That is a free exploit and this module declines it.
    else if (!isHoldingSword())
        gate = "No Sword";
    // No release path means no way out of a block. Say so and do nothing.
    else if (!apiUsable_)
        gate = "No API";

    gateReason_.store(gate, std::memory_order_release);
    if (gate) {
        if (fakingBlock_ && fItemInUseCount_ && self) {
            env->SetIntField(self, fItemInUseCount_, 0);
            fakingBlock_ = false;
        }
        if (handOver)
            abandonBlock();
        else
            standDown(env, self);
        return;
    }

    // ---- The cycle. Up while a hit can land, down and running while one cannot. ----

    const bool live = fightLive();

    // Slinky Force block animation (visual only, writes 72000 to itemInUseCount)
    if (settings.fakeBlockAnimation && !serverBlocking_.load(std::memory_order_acquire)) {
        const bool animEligible = !settings.fakeBlockRequireTarget || live;
        if (animEligible && fItemInUseCount_ && self) {
            env->SetIntField(self, fItemInUseCount_, 72000);
            fakingBlock_ = true;
        } else if (fakingBlock_ && fItemInUseCount_ && self) {
            env->SetIntField(self, fItemInUseCount_, 0);
            fakingBlock_ = false;
        }
    } else if (fakingBlock_ && fItemInUseCount_ && self && !serverBlocking_.load(std::memory_order_acquire)) {
        env->SetIntField(self, fItemInUseCount_, 0);
        fakingBlock_ = false;
    }

    // Slinky Lag phase: if lag delay is active, tick it down
    if (lagTicksRemaining_ > 0) {
        if (settings.lagFlushAttack && beat) {
            lagTicksRemaining_ = 0;
        } else {
            --lagTicksRemaining_;
            return;
        }
    }

    if (serverBlocking_.load(std::memory_order_acquire)) {
        if (beat)
            ++beatsHeld_;
        // The exchange is over. Down it goes, the sprint comes back, and it stays down.
        if (!live) {
            standDown(env, self);
            return;
        }
        const std::uint32_t held = tick_ - blockedAtTick_;
        // The floor comes first: it is the only thing standing between a double signal on one
        // tick and a C08/C07 pair with nothing between them.
        if (held < minHoldTicks_)
            return;
        // Three reasons to come down: damage taken, beats reached, or max hold duration elapsed
        if (hitTaken || beatsHeld_ >= beatsPerHole_ || held >= maxHoldTicks_)
            sendRelease(env, self, hurt > 0);
        return;
    }

    // Down. Either a hole is running, or we are waiting for the beat that opens the exchange.
    if (!live) {
        // A hole belongs to a live exchange. Drop it so the next fight opens on a swing rather
        // than on a timer left over from the last one.
        holePending_ = false;
        return;
    }
    if (holePending_) {
        if (immuneHole_) {
            // This hole is the immunity window, so it ends when the window does. hurtTime is
            // set to 10 alongside hurtResistantTime and the two count down together, so hurtTime
            // dropping into the margin means the next hit is a handful of ticks out -- long
            // enough to get the sword up in front of it and not a tick longer.
            if (hurt > static_cast<int>(immuneMargin_))
                return;
        } else if (tick_ - releasedAtTick_ < holeTicks_) {
            // A rhythm hole runs on its own clock. Long enough to be a real sprint reset, which
            // is what the hole is for now: 4 to 8 ticks of full-speed movement, not one tick of
            // animation.
            return;
        }
    } else if (!beat) {
        // Nothing has swung yet and nothing has hit us. An idle player with the module on and a
        // target under the crosshair puts nothing at all on the wire.
        return;
    } else if (hurt > static_cast<int>(immuneMargin_)) {
        // A beat arrived, but we are deep inside our own immunity window and a block raised here
        // would spend most of its life protecting against hits that are already being discarded.
        // Beats arrive every few ticks in a real exchange; the next one will land closer.
        return;
    }
    sendBlock(env, self);
}

std::string BlockHit::getDisplayInfo() {
    if (fakingBlock_)
        return "Fake";
    if (const char* gate = gateReason_.load(std::memory_order_acquire))
        return gate;
    return serverBlocking_.load(std::memory_order_acquire) ? "Block" : "Open";
}

void BlockHit::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Main",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();
            ui::SliderFloat("Range", &settings.range, 1.0f, 6.0f, "%.1f");
            ui::SliderInt("Maximum Hurt Time", &settings.maxHurtTime, 0, 10, "%d");
            ui::SliderFloat("Hold Duration", &settings.maxHoldTime, 1.0f, 10.0f, "%.1f");
            ImGui::Spacing();
            ImGui::Text("Conditions:");
            ui::Checkbox("Require Left Click", &settings.requireLMB);
            ui::Checkbox("Require Right Click", &settings.requireRMB);
            ui::Checkbox("Require Damage", &settings.requireDamage);
        },
        "Features",
        [this]() {
            ImGui::Text("Force Block Animation:");
            ui::Checkbox("Fake Animation", &settings.fakeBlockAnimation);
            if (settings.fakeBlockAnimation) {
                ui::Checkbox("Only Within Range", &settings.fakeBlockRequireTarget);
            }
            ImGui::Spacing();
            ImGui::Text("Lag Phase:");
            ui::SliderFloat("Chance", &settings.lagChance, 0.0f, 100.0f, "%.0f%%");
            ui::SliderFloat("Duration", &settings.lagDuration, 0.5f, 10.0f, "%.1f");
            ui::Checkbox("Flush On Attack", &settings.lagFlushAttack);
            ui::Checkbox("Reblock Instantly", &settings.lagReblock);
        });
}

void BlockHit::saveSettings(json::Value& node) const {
    node["range"] = settings.range;
    node["maxHurtTime"] = settings.maxHurtTime;
    node["maxHoldTime"] = settings.maxHoldTime;
    node["fakeBlockAnimation"] = settings.fakeBlockAnimation;
    node["fakeBlockRequireTarget"] = settings.fakeBlockRequireTarget;
    node["lagChance"] = settings.lagChance;
    node["lagDuration"] = settings.lagDuration;
    node["lagFlushAttack"] = settings.lagFlushAttack;
    node["lagReblock"] = settings.lagReblock;
    node["requireLMB"] = settings.requireLMB;
    node["requireRMB"] = settings.requireRMB;
    node["requireDamage"] = settings.requireDamage;
}

void BlockHit::loadSettings(const json::Value& node) {
    settings.range = node["range"].as_float(settings.range);
    settings.maxHurtTime = node["maxHurtTime"].as_int(settings.maxHurtTime);
    settings.maxHoldTime = node["maxHoldTime"].as_float(settings.maxHoldTime);
    settings.fakeBlockAnimation = node["fakeBlockAnimation"].as_bool(settings.fakeBlockAnimation);
    settings.fakeBlockRequireTarget = node["fakeBlockRequireTarget"].as_bool(settings.fakeBlockRequireTarget);
    settings.lagChance = node["lagChance"].as_float(settings.lagChance);
    settings.lagDuration = node["lagDuration"].as_float(settings.lagDuration);
    settings.lagFlushAttack = node["lagFlushAttack"].as_bool(settings.lagFlushAttack);
    settings.lagReblock = node["lagReblock"].as_bool(settings.lagReblock);
    settings.requireLMB = node["requireLMB"].as_bool(settings.requireLMB);
    settings.requireRMB = node["requireRMB"].as_bool(settings.requireRMB);
    settings.requireDamage = node["requireDamage"].as_bool(settings.requireDamage);
}
