#pragma once

#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>

#include "../module.h"

// BlockHit — the sword goes up when a hit can actually land on you, and it pays for itself
// honestly. Nothing to configure, because there is nothing about it to choose.
//
// WHAT A BLOCKHIT ACTUALLY IS
// ---------------------------
// Nobody good at 1.8 holds right-click through a whole fight, and nobody taps it once per
// click either. They hold it and let go — for a sprint reset, for a rod, for a gap they want
// to close — a handful of times per exchange. Holding costs 80% of the walk speed and the
// entire sprint, so those releases are what buys the sprint knockback back, and the moment
// they come is the moment of a click. That is the shape: the sword up while a hit can land on
// you, down while it cannot, and the releases falling on the beat of the left mouse button.
// The only free numbers are how many swings pass between releases and how long one runs, and
// those are rolled fresh every time so no two are the same.
//
// This replaces four modes, a mode keybind and a state machine that read health, immunity
// windows and breakthrough history to decide whether to be holding a block. All of that was
// answering a question nobody asked. A blockhit is a rhythm, not a policy.
//
// THE PRICE IS NOT OPTIONAL, AND THAT IS THE POINT
// ------------------------------------------------
// The version before this one raised the block on the wire and then wiped the client's own
// use-item state, so vanilla never applied the slowdown: a real server-side block at full
// sprint speed. It halved the damage exactly as advertised, and Grim flagged NoSlow on the
// second tick of every hold. Grim does not read a flag — it replays your inputs. C08 with
// placedBlockDirection == 255 tells it you are using an item, and from that tick its
// PredictionEngine multiplies your movement input by 0.2 and matches the result against the
// position you send. Moving faster than every vector in that set is not a state mismatch, it
// is a physics one, and it is not reducible: there is no way to have the damage reduction
// without presenting the state that produces it, and no way to present the state and move as
// though it were not there.
//
// So this pays it. The client really enters use-item state, and EntityPlayerSP.onLivingUpdate
// really does
//
//     if (this.isUsingItem() && !this.isRiding()) {
//         this.movementInput.moveStrafe  *= 0.2F;
//         this.movementInput.moveForward *= 0.2F;
//         this.sprintToggleTimer = 0;
//     }
//
// then guards BOTH sprint-start branches with !this.isUsingItem() and finishes with
//
//     if (this.isSprinting() && this.movementInput.moveForward < 0.8F)
//         this.setSprinting(false);
//
// to us as much as to anyone else. What is automated here is not the cost — it is the timing,
// which is the part a hand gets wrong. The sword is up for the ticks where a hit can land and
// down for the ticks where it cannot, and the release hands the sprint back on the same tick
// it takes the block away.
//
// EntityPlayer.attackTargetEntityWithCurrentItem raises the knockback level on
// `if (this.isSprinting()) ++i`, and the server does that read when our C02 arrives. The C02
// for a tick is already on the wire before this hook raises anything, so the swing that opens
// a hold keeps its sprint knockback. The swings inside the hold do not, which is exactly why
// the holds are short and why they end on a beat.
//
// WHAT GOES ON THE WIRE INSTEAD
// -----------------------------
// The server never reads the keybind, and it does not read the client's item-in-use state
// either. It reads its own EntityPlayerMP.itemInUse, and only a packet sets that:
//
//   C08PacketPlayerBlockPlacement with placedBlockDirection == 255 — the in-air "use item"
//   form, exactly what PlayerControllerMP.sendUseItem builds. Server side,
//   NetHandlerPlayServer.processPlayerBlockPlacement takes the == 255 branch straight into
//   ItemInWorldManager.tryUseItem -> ItemStack.useItemRightClick ->
//   ItemSword.onItemRightClick -> setItemInUse(stack, 72000). EntityPlayer.isBlocking() is
//   true from that instant and EntityPlayer.damageEntity applies
//   damageAmount = (1.0F + damageAmount) * 0.5F to everything not unblockable — a 7 arrives
//   as a 4.
//
//   C07PacketPlayerDigging(RELEASE_USE_ITEM) — processPlayerDigging calls stopUsingItem() and
//   the state is gone. PlayerControllerMP.onStoppedUsingItem is the vanilla method that sends
//   it.
//
// sendUseItem also runs useItemRightClick locally, so the CLIENT enters use-item state in the
// same breath — which is now wanted rather than something to undo. It is also where the picture
// comes from: setItemInUse seeds itemInUse for the state and itemInUseCount = 72000 for the
// render, and ItemRenderer.renderItemInFirstPerson draws the block on getItemInUseCount() > 0.
// Nothing has to be painted.
//
// ONE TICK VANILLA WOULD HAVE BEEN SLOW, AND WE WOULD NOT
// ------------------------------------------------------
// Minecraft.runTick() reads the mouse and calls rightClickMouse() before
// World.updateEntities(), so a human's right-click is already in itemInUse by the time
// EntityPlayerSP.onLivingUpdate applies the slowdown on that same tick. This hook is inside
// EntityLivingBase.onLivingUpdate, downstream of that code, so a C08 sent from here would
// leave exactly one tick of full-speed movement in front of a block the server has already
// registered — the flag above, one tick wide, on every single hold.
//
// So the block tick applies vanilla's own lines itself: both MovementInput axes scaled by 0.2,
// then setSprinting(false) because the scaled forward is now under 0.8. Same fields, same
// values, same tick, and updateEntityActionState() has not yet copied the input, so this is
// the movement the tick really uses. Scaling both axes by one factor leaves atan2(-s, f)
// untouched, so anything that steered the walk direction earlier in this same dispatch keeps the
// heading it chose. From the server's side there is nothing left to tell apart.
//
// THE PATTERN: BLOCK WHAT CAN HURT YOU, RUN THE REST OF THE TIME
// --------------------------------------------------------------
// Now that the slowdown is real, uptime is a cost rather than a score, and the number worth
// maximising is not the fraction of the fight the sword is up — it is the fraction of the
// DAMAGING hits it is up for. Those are very different numbers, because for most of a fight
// nothing can hurt you at all. EntityLivingBase.attackEntityFrom:
//
//     if (this.hurtResistantTime > (float)this.maxHurtResistantTime / 2.0F) {  // > 10
//         if (amount <= this.lastDamage) return false;                         // discarded
//         ...
//     } else {
//         this.lastDamage = amount;
//         this.hurtResistantTime = this.maxHurtResistantTime;                  // 20
//         ...
//     }
//
// For the ten ticks after a hit lands, another hit of the same strength is thrown away before
// damageEntity runs — so before isBlocking() is ever consulted. A block held through those ten
// ticks halves nothing and pays 80% of the walk speed and the entire sprint for it.
//
// The client knows when it is inside that window without asking anyone. performHurtAnimation()
// sets hurtTime = maxHurtTime = 10, and hurtTime and hurtResistantTime count down together, so
// hurtTime > 0 means exactly "a hit right now would be discarded anyway":
//
//   hurtTime > 0   ->  nothing can hurt us. Sword down, sprint back, close or disengage.
//   hurtTime == 0  ->  the next hit lands in full. Sword up.
//
// The sword goes back up a few ticks BEFORE the window closes rather than on the tick it
// closes, because a block that arrives with the hit arrives after it. Two to five ticks of
// margin, rolled, which is also what keeps the transition off tick 10 every single time.
//
// That is the strategy. The rhythm sits on top of it: even in a vulnerable stretch the sword
// taps down every few beats, where one beat is one swing of ours — a left-click edge or an
// AutoClicker click, both visible on the tick they happen. A hole is a real sprint reset now, four
// to eight ticks, not the token tap it could afford to be when it was free.
//
// The block only ever goes UP on a beat, never out of nowhere — the one exception being the
// re-block that closes a hole it opened itself. A hit landing on us counts as a beat too, off
// hurtTime's rising edge, so somebody opening on us before we have clicked once gets blocked.
//
//   A fight is live, hurtTime is 0, a beat arrives  ->  C08. Sword up.
//   A hit lands on us                               ->  C07. Down for the immunity window.
//   N beats and nothing has landed                  ->  C07. Sprint reset.
//   hurtTime counts down into the margin            ->  C08 again, no beat required.
//   The fight ends                                  ->  C07. The sword stays down.
//
// Five numbers, rerolled after every release and never exposed:
//
//   N, the beats a hold rides out before it taps off. Three to six.
//   A floor in ticks, so a release cannot land in the tick or two right after the block went
//   up — that is the C08/C07 churn a double signal on one tick would otherwise produce.
//   A ceiling in ticks, so a hold breaks up even when the beats dry out and nothing lands.
//   The length of a beat-driven hole, four to eight ticks, with a tail: about one hole in seven
//   runs nine to fourteen, because a player who re-blocks in exactly six ticks every single
//   time, forever, is a player nobody has ever met.
//   The immunity margin: how much hurtTime is left when the sword comes back up. Two to five.
//
// An earlier version read health, immunity and breakthrough history to decide whether it was
// ALLOWED to block at all, and ended up holding one for seconds at a time — a flat line where
// a rhythm should be. This reads one field, hurtTime, for the opposite purpose: not to justify
// holding a block, but to find the ticks where holding one is pure loss.
//
// The one thing it checks before raising anything is that there is somebody to block: an
// entity under the crosshair, a target AimAssist is steering at, or a hit taken in the last two
// seconds. Without that gate a held AutoClicker would put a C08/C07 pair on the wire every
// few ticks while walking across an empty map, which is the loudest possible version of the
// one signature this technique has.
//
// THE RELEASE PUTS THE SPRINT BACK
// --------------------------------
// Vanilla's slowdown ends with sprintToggleTimer = 0, so a player who sprints by double-tapping
// W gets nothing back when the block drops — the timer that would have caught the second tap is
// gone and they have to tap again. That would put a hole in the sprint on every hole in the
// block, which is the opposite of what a hole is for. So the sprint state is latched when the
// block goes up and setSprinting(true) restores it on release, if forward is still held.
//
// The ordering works out for free. EntityPlayerSP.onUpdateWalkingPlayer runs after this hook in
// the same tick and only emits C0BPacketEntityAction when isSprinting() != serverSprintState, so
// the START_SPRINTING lands immediately behind the C07 that ended the block — the same order a
// hand produces on the tick it lets go of right-click. If W has since been released, vanilla
// drops the sprint next tick and sends the matching C0B itself, which is the one tick of carried
// sprint vanilla produces naturally anyway.
//
// WHAT IT COSTS, STATED PLAINLY
// -----------------------------
// For every tick the sword is up: 80% of the walk input and the sprint, exactly what vanilla
// charges a human. What is NOT paid, because none of it is part of the slowdown — the knockback
// level on the swing that opens a hold (its C02 is already on the wire), vanilla's four-tick
// rightClickDelayTimer (sendUseItem never touches it, only Minecraft.rightClickMouse does), and
// any chest, door or lever the crosshair happens to be resting on, because the in-air form of
// C08 never reaches onPlayerRightClick.
//
// The entire design is about not paying that 80% on the ticks where a block buys nothing back.
//
// TWO FIELDS, AND WHY NEITHER ONE NEEDS TOUCHING
// ----------------------------------------------
// The raised-sword picture and the movement penalty are not one field:
//
//   EntityPlayerSP.onLivingUpdate slows you down and drops your sprint on isUsingItem(), which
//   is `return this.itemInUse != null`.
//   ItemRenderer.renderItemInFirstPerson draws the block on getItemInUseCount() > 0, which
//   reads itemInUseCount. RenderPlayer.setModelVisibilities picks ArmPose.BLOCK off the same
//   counter.
//
// The version before this one exploited that split: it nulled itemInUse and left the counter at
// the 72000 setItemInUse had seeded — the picture without the price, and the physics mismatch
// above. On the real path nothing needs painting at all, because setItemInUse writes both halves
// and neither is disturbed afterwards; the release zeroes the counter for free, since
// onStoppedUsingItem ends in stopUsingItem() -> clearItemInUse().
//
// One knock-on of a real block, deliberate and worth keeping: ItemRenderer only animates a swing
// in the else branch of that same test, so while the sword is up your own swings do not animate.
// That is exactly what vanilla looks like when a human holds right-click and left-clicks through
// it.
//
// NoSlowDown is the one module that has to be told about this. It treats "using an item" as its
// cue to rebuild MovementInput from raw key state and call setSprinting(true), which is precisely
// the flag this redesign exists to remove — so it stands aside for an automated block and keeps
// only the case a physical right-click created.
//
// WHAT IS LEFT TO SEE
// -------------------
// No NoSlow. The state on the server and the movement the server predicts from it agree on every
// tick, because the client really is in the state and really is slowed by it. What is left is
// timing: C08/C07 pairs that fall on the beat of the left mouse button, and re-blocks that open a
// few ticks before an immunity window closes. That is a description of a good player, and a far
// quieter thing to be than a physics violation on every hold.
//
// RESIDUALS, NOT HIDDEN
// ---------------------
// The server's itemInUse holds the ItemStack captured when setItemInUse ran, and isBlocking()
// tests that stack rather than the held one — so a hotbar switch would leave us "blocking with
// a sword" while a bow is in hand. That is a free exploit and this module declines it: the
// tick the held item stops being a sword, the block is released.
//
// Our copy of the server's state is a model, not a read. On death and on world change it is
// reset rather than released, because the EntityPlayerMP that owned the itemInUse no longer
// exists; everywhere else the release really goes out.
//
// While the player physically holds right-click, or the client is in use-item state for any
// other reason — NoItemRelease, AutoPot, ThrowPots, eating — the module stands down completely
// and lets vanilla own the block, including vanilla's own C07 on release. That hand-over is
// silent: the taker's right-click already sent its own C08 earlier in the same tick, so
// releasing on top of it would only cancel the block they just started and burn vanilla's
// four-tick right-click cooldown getting it back.
//
// If the release path cannot be resolved at all, the module does nothing rather than raise a
// block it has no way to lower.
class BlockHit : public Module {
public:
    BlockHit();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        float range = 3.4f;
        int maxHurtTime = 4;
        float maxHoldTime = 3.5f;

        bool fakeBlockAnimation = false;
        bool fakeBlockRequireTarget = true;

        float lagChance = 0.0f;
        float lagDuration = 2.5f;
        bool lagFlushAttack = false;
        bool lagReblock = false;

        bool requireLMB = true;
        bool requireRMB = false;
        bool requireDamage = false;
    } settings;

    // Client thread, dispatched from the SlowDownGate hook inside
    // EntityLivingBase.onLivingUpdate. Every packet this module sends is sent here.
    void onClientTick(JNIEnv* env);
    // Still needed while disabled when a block is outstanding: the release is client-thread work
    // and nothing else will do it.
    bool needsClientTick() const;

    // True when the server currently thinks we are blocking, because of us.
    bool isServerBlocking() const { return serverBlocking_.load(std::memory_order_acquire); }
    void notifyAttack() { attackSignals_.fetch_add(1, std::memory_order_acq_rel); }

private:
    // ---- Client-thread state. Touched only inside onClientTick. ----
    std::uint32_t tick_ = 0;
    std::uint32_t blockedAtTick_ = 0;
    std::uint32_t releasedAtTick_ = 0;
    std::uint32_t lastHitTakenTick_ = 0;
    bool haveTakenHit_ = false;
    int prevHurtTime_ = 0;
    // Beats counted since the current block went up. The beat that raised it does not count.
    std::uint32_t beatsHeld_ = 0;
    // True between a release and the re-block that closes the hole. Cleared when the fight
    // ends, so the next exchange opens on a beat instead of on a leftover timer.
    bool holePending_ = false;
    // The hole was opened by a hit landing on us, so the re-block waits on hurtTime counting
    // down to the margin rather than on the hole's own clock.
    bool immuneHole_ = false;
    // The sprint the player had when the block went up. Vanilla's slowdown finishes with
    // sprintToggleTimer = 0, so a double-tap sprinter gets nothing back on release unless the
    // release hands it back.
    bool wasSprinting_ = false;

    // ---- The cycle shape. Rerolled on every release, so it is never twice the same. ----
    std::uint32_t beatsPerHole_ = 4;   // swings a hold rides out before it taps off
    std::uint32_t minHoldTicks_ = 3;   // anti-churn floor: no release right after a raise
    std::uint32_t maxHoldTicks_ = 26;  // the beats dried up; break the hold anyway
    std::uint32_t holeTicks_ = 6;      // ticks the sword stays down for a real sprint reset
    std::uint32_t immuneMargin_ = 3;   // hurtTime left when the sword comes back up
    std::mt19937 rng_;

    // ---- Java handles. Resolved once, on the client thread. ----
    bool apiTried_ = false;
    bool apiUsable_ = false;                 // false means do nothing: a block with no release is worse
    jmethodID midOnStoppedUsing_ = nullptr;  // PlayerControllerMP.onStoppedUsingItem
    // Vanilla's own slowdown targets. EntityPlayerSP.onLivingUpdate has already run its copy for
    // this tick before we are called, so the block tick applies it here instead.
    jfieldID fMovementInput_ = nullptr;  // EntityPlayerSP.movementInput / field_71158_b
    jfieldID fInputStrafe_ = nullptr;    // MovementInput.moveStrafe     / field_78902_a
    jfieldID fInputForward_ = nullptr;   // MovementInput.moveForward    / field_78900_b
    jfieldID fSprintToggle_ = nullptr;   // EntityPlayerSP.sprintToggleTimer / field_71503_h
    // Vanilla's slowdown is guarded by !isRiding(), so a mount would leave the block unpaid for.
    // Rather than reproduce that branch, the module simply does not block while riding.
    jfieldID fRidingEntity_ = nullptr;    // Entity.ridingEntity / field_70154_o
    jfieldID fItemInUseCount_ = nullptr;  // EntityPlayer.itemInUseCount for fake animation
    bool fakingBlock_ = false;
    int lagTicksRemaining_ = 0;

    // ---- Module-thread state. ----
    bool prevAttackDown_ = false;
    std::chrono::steady_clock::time_point observedAutoClick_{};

    // ---- Published across threads. ----
    // Left-click edges and AutoClicker clicks the client tick has not consumed yet.
    std::atomic<std::uint32_t> attackSignals_{0};
    // Our model of EntityPlayerMP.itemInUse holding one of OUR C08s.
    std::atomic<bool> serverBlocking_{false};
    // Always a string literal, so publishing the bare pointer is safe.
    std::atomic<const char*> gateReason_{nullptr};

    static std::uint64_t nowMs();
    static bool isSwordId(int id);
    bool isHoldingSword() const;
    // Somebody to block: an entity under the crosshair, a target AimAssist is steering at, or a hit
    // taken recently. Without this a held AutoClicker would tap out C08/C07 pairs on an empty
    // map for as long as the button was down.
    bool fightLive() const;

    // Draws the next cycle's five numbers.
    void rollCycle();

    // Resolves the one thing this cannot work without -- a way to send the release -- plus the
    // fields the slowdown and the picture are written through.
    bool resolveApi(JNIEnv* env, jobject playerInstance);
    bool clientUsingItem(JNIEnv* env, jobject playerInstance) const;
    bool isRiding(JNIEnv* env, jobject playerInstance) const;
    // The lines EntityPlayerSP.onLivingUpdate would have run this tick if the right-click had
    // come from a hand: both input axes scaled by 0.2, sprintToggleTimer zeroed, and the sprint
    // dropped because the scaled forward is now under 0.8. Only ever called on the tick the block
    // is RAISED -- from the next tick on the client's itemInUse is genuinely set, so vanilla
    // applies its own copy before this hook is reached and doing it again would scale by 0.04.
    void applyVanillaSlowdown(JNIEnv* env, jobject playerInstance);
    // Puts back what vanilla already took on the release tick, and reports whether the restored
    // forward input is enough for vanilla's own sprint-start test. A hand's C07 goes out in the
    // mouse phase, before the slowdown branch runs, so a human's release tick moves at full speed
    // with the C07 already ahead of the position packet. Undoing the 0.2 reproduces that exactly.
    // `scaled` is false on the rare release where vanilla's branch did not run this tick -- a
    // mount, or an item switch that cleared itemInUse before us -- and then this only reads the
    // forward input, because multiplying an unscaled input by five is a speed hack.
    bool undoVanillaSlowdown(JNIEnv* env, jobject playerInstance, bool scaled);
    // C08 in-air use. The client really enters use-item state and keeps it.
    bool sendBlock(JNIEnv* env, jobject playerInstance);
    // C07 RELEASE_USE_ITEM, the slowdown undone for the tick, and the sprint handed back.
    // `immune` marks a hole opened because a hit landed: the re-block then waits on hurtTime
    // counting down to the margin rather than on the hole's own clock.
    void sendRelease(JNIEnv* env, jobject playerInstance, bool immune = false);

    // Releases an outstanding block and forgets the cycle.
    void standDown(JNIEnv* env, jobject playerInstance);
    // Drops ownership WITHOUT sending anything, for when the use-item channel is being taken
    // over rather than vacated. PlayerControllerMP.onStoppedUsingItem ends with
    // playerIn.stopUsingItem(), so releasing here would wipe the client use-item state the
    // taker has just created — and vanilla then refuses to right-click again until
    // rightClickDelayTimer counts back down from 4, which is 200 ms of "the block takes a
    // moment to appear". The taker's own C08 already owns the server's itemInUse, and their
    // own release will end it.
    void abandonBlock();
};

extern BlockHit* blockHit;
