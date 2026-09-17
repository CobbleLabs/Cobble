#include "aimbow.h"
#include "antibot.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

AimBow* aimBow = nullptr;

// Five client ticks, so a 20 Hz consumer can never sample the gap between two of this
// module's ~500 Hz writes and conclude the head is free.
static constexpr unsigned long long kBowRotClaimTailMs = 250ull;

bool AimBow::isRewritingRotation() const {
    if (!enabled)
        return false;
    const unsigned long long last = lastRotWriteMs_.load(std::memory_order_acquire);
    if (last == 0ull)
        return false;
    const unsigned long long now = GetTickCount64();
    return now >= last && (now - last) <= kBowRotClaimTailMs;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;
constexpr int kBowItemId = 261;  // Minecraft 1.8 bow

// --- vanilla arrow constants -------------------------------------------------
// ItemBow.onPlayerStoppedUsing passes `charge * 2.0` to EntityArrow, whose ctor
// hands it to setThrowableHeading as `velocity * 1.5` — so a full draw launches
// at 3.0 blocks/tick.
constexpr double kArrowSpeedFull = 3.0;
constexpr double kArrowDrag = 0.99;     // EntityArrow.onUpdate: motion *= 0.99 (in air)
constexpr double kArrowGravity = 0.05;  // ...then motionY -= 0.05, in that order
constexpr double kBowFullDrawTicks = 20.0;
constexpr double kArrowSpawnDrop = 0.10000000149011612;  // ctor lowers the spawn below eye level

// Sum of the drag series, 1/(1-0.99) = 100. Also the arrow's horizontal reach in
// blocks per block/tick of launch speed, which is why a flat shot dies near 300.
constexpr double kDragSum = 1.0 / (1.0 - kArrowDrag);
// Terminal fall speed, 0.05/0.01 = 5 blocks/tick.
constexpr double kTermFall = kArrowGravity / (1.0 - kArrowDrag);

// ItemBow's charge curve. `t` is ticks drawn; vanilla refuses to fire below 0.1
// and clamps at 1.0 after a full second.
double bowCharge(int drawTicks) {
    if (drawTicks <= 0)
        return 0.0;
    double f = (double)drawTicks / kBowFullDrawTicks;
    f = (f * f + 2.0 * f) / 3.0;
    return f > 1.0 ? 1.0 : f;
}

// Height the arrow reaches after travelling `d` blocks horizontally, launched at
// `theta` with speed `v`.
//
// Closed form of the tick recursion. With u = 1 - 0.99^n:
//     horizontal(n) = 100 * v*cos(theta) * u
//     vertical(n)   = 100 * (v*sin(theta) + 5) * u - 5n
// Inverting the first for n makes both exact and continuous, so there is no Euler
// loop and no step-size error. Returns false when d is past the arrow's
// horizontal reach at this angle, where u would leave [0, 1).
bool arcHeightAt(double v, double theta, double d, double& outY, double& outTicks) {
    static const double invLogDrag = 1.0 / std::log(kArrowDrag);
    const double ch = std::cos(theta);
    if (ch <= 1e-6)
        return false;
    const double hMax = kDragSum * v * ch;
    if (d >= hMax)
        return false;
    const double u = d / hMax;
    outTicks = std::log(1.0 - u) * invLogDrag;
    outY = kDragSum * (v * std::sin(theta) + kTermFall) * u - kTermFall * outTicks;
    return true;
}

// Flattest launch angle whose trajectory passes through (d, dy), plus the flight
// time in ticks.
//
// Height-at-d is NOT monotonic in theta: it climbs, peaks, then plunges as theta
// nears the angle where the arrow can only just still drift d blocks sideways —
// there it creeps along so slowly that gravity has hundreds of ticks to work.
// So find the peak by ternary search first (the function is unimodal), then
// bisect on the rising side, which is the flat, fast, hittable arc.
//
// On failure outTheta is the peak angle: the highest the arrow can possibly be at
// that distance. That is the best available attempt at an out-of-reach target,
// and far more useful than snapping the view somewhere arbitrary.
bool solveArc(double v, double d, double dy, double& outTheta, double& outTicks) {
    outTicks = 0.0;
    outTheta = 0.0;
    if (v <= 1e-4)
        return false;
    if (d < 1e-3) {
        outTheta = std::atan2(dy, 1e-3);
        return true;
    }
    const double reach = kDragSum * v;  // absolute ceiling, at theta = 0
    if (d >= reach)
        return false;
    // Past acos(d / reach) the arrow cannot cover d blocks sideways at all.
    const double thetaCeil = std::acos(d / reach) - 1e-4;
    double lo = std::atan2(dy, d);  // straight line — always falls short of dy
    if (lo >= thetaCeil)
        lo = -thetaCeil;

    double y = 0.0, n = 0.0;
    double a = lo, b = thetaCeil;
    for (int i = 0; i < 26; ++i) {
        const double third = (b - a) / 3.0;
        const double m1 = a + third;
        const double m2 = b - third;
        const double y1 = arcHeightAt(v, m1, d, y, n) ? y : -1e30;
        const double y2 = arcHeightAt(v, m2, d, y, n) ? y : -1e30;
        if (y1 < y2)
            a = m1;
        else
            b = m2;
    }
    const double apex = 0.5 * (a + b);
    outTheta = apex;
    if (!arcHeightAt(v, apex, d, y, n) || y < dy)
        return false;  // even the highest possible arc passes below the target

    double hi = apex;
    for (int i = 0; i < 20; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (arcHeightAt(v, mid, d, y, n) && y >= dy)
            hi = mid;
        else
            lo = mid;
    }
    if (!arcHeightAt(v, hi, d, y, n))
        return false;
    outTheta = hi;
    outTicks = n;
    return true;
}
}  // namespace

AimBow::AimBow() : Module("AimBow") {
    keybind = 0;
}

void AimBow::onEnable() {
    chargeStartMs = 0;
    wasDrawing = false;
    hasTarget = false;
    hasOverride = false;
    resetVelocity();
}

void AimBow::onDisable() {
    wasDrawing = false;
    hasTarget = false;
    hasOverride = false;
    resetVelocity();
}

void AimBow::resetVelocity() {
    smoothVx_ = 0.0;
    smoothVy_ = 0.0;
    smoothVz_ = 0.0;
    hasSample_ = false;
    velUuid_.clear();
    lastCharge_ = 0.0f;
    lastDist_ = 0.0f;
    lastInReach_ = false;
}

float AimBow::wrapDeg(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0.0f)
        a += 360.0f;
    return a - 180.0f;
}

std::string AimBow::getDisplayInfo() {
    if (!hasTarget)
        return "";
    if (!lastInReach_)
        return "out of range";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f%% %.0fm", lastCharge_ * 100.0f, lastDist_);
    return buf;
}

void AimBow::onUpdate() {
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        wasDrawing = false;
        hasTarget = false;
        hasOverride = false;
        return;
    }

    // Armor filter keybind: toggle the filter on/off on a key press (edge).
    if (settings.armorFilterKeybind != 0) {
        bool keyDown = (GetAsyncKeyState(settings.armorFilterKeybind) & 0x8000) != 0;
        if (keyDown && !armorFilterKeyPrev_) {
            settings.armorFilterEnabled = !settings.armorFilterEnabled;
            NotificationManager::add("Bow Armor Filter", settings.armorFilterEnabled);
        }
        armorFilterKeyPrev_ = keyDown;
    }

    // Must be holding a bow.
    const int heldId = player.getHeldItemId();
    if (heldId != kBowItemId) {
        wasDrawing = false;
        hasTarget = false;
        hasOverride = false;
        static int s_lastLoggedHeld = -1;
        if (s_lastLoggedHeld != heldId) {
            s_lastLoggedHeld = heldId;
            log_ok("[AimBow] bail: not holding bow (heldId=%d, expected=%d)", heldId, kBowItemId);
        }
        return;
    }

    // "Drawing" = right mouse held (charging the bow).
    const bool drawing = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const unsigned long now = GetTickCount();
    if (drawing && !wasDrawing)
        chargeStartMs = now;
    wasDrawing = drawing;

    if (settings.onlyWhileDrawing && !drawing) {
        hasTarget = false;
        hasOverride = false;
        return;
    }

    // Draw progress, straight from the counter the server will read on release.
    // The wall-clock path is a fallback for when the field could not be resolved;
    // it tracks the same 20 TPS, just without surviving a lagged draw.
    int drawTicks = 0;
    if (drawing) {
        drawTicks = player.getItemInUseTicks();
        if (drawTicks <= 0 && chargeStartMs != 0)
            drawTicks = (int)((now - chargeStartMs) / 50UL);
    }
    const double charge = settings.liveCharge ? bowCharge(drawTicks) : 1.0;
    lastCharge_ = (float)charge;

    // Solving for a nearly-empty bow demands an absurd loft, so the crosshair
    // would fly skyward the instant you right-click and crawl back down as the
    // draw fills. Flooring the charge keeps the aim where a real shot lands:
    // above the floor the solution is exact, below it we show the floor's arc.
    const double solveCharge = (std::max)(charge, (double)settings.minSolveCharge);
    const double v = kArrowSpeedFull * solveCharge;

    // Launch origin. EntityArrow spawns at eye level minus 0.1, not at the eye.
    const double lx = player.getPosX();
    const double ly = player.getPosY() + (player.isSneaking() ? 1.54 : 1.62) - kArrowSpawnDrop;
    const double lz = player.getPosZ();
    const float curYaw = player.getYaw();
    const float curPitch = player.getPitch();

    // Track user's manual crosshair rotation
    if (wasDrawing && hasOverride) {
        float deltaYaw = wrapDeg(curYaw - lastOverrideYaw);
        float deltaPitch = curPitch - lastOverridePitch;

        manualYaw = wrapDeg(manualYaw + deltaYaw);
        manualPitch = std::clamp(manualPitch + deltaPitch, -90.0f, 90.0f);
    } else {
        manualYaw = curYaw;
        manualPitch = curPitch;
        hasOverride = false;
    }

    // Pick a target inside range and the FOV cone. The two modes score differently:
    // Automatic takes the nearest player, ClosestToCrosshair the one your crosshair
    // is already nearest to. The cone is measured in yaw AND pitch — a yaw-only
    // test lets a player directly above or below you into the cone.
    std::vector<PlayerInfo> players = mc.getPlayerList();
    if (friendsManager) {
        friendsManager->resolvePending(players);
    }
    const PlayerInfo* best = nullptr;
    double bestScore = 1e30;
    const double maxDistSq = (double)settings.range * settings.range;
    const float maxAngle = (float)settings.fov * 0.5f;

    float referenceYaw = curYaw;
    float referencePitch = curPitch;
    if (settings.mode == AimMode::ClosestToCrosshair) {
        referenceYaw = manualYaw;
        referencePitch = manualPitch;
    }

    for (const auto& p : players) {
        if (AntiBot::Filtered(p))
            continue;
        if (!settings.targetInvisible && p.isInvisible)
            continue;

        if (friendsManager) {
            // Friend verification: never target friends
            bool isFriendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool isFriendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (isFriendByUUID || isFriendByName)
                continue;

            // Teammate verification: skip if targeting teammates is disabled
            if (!settings.targetTeammates) {
                if (p.isOnLocalTeam && friendsManager->honorsVanillaTeams())
                    continue;

                bool isApolloTeam = !p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid);
                if (isApolloTeam)
                    continue;

                bool isApolloNametag = !p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid);
                if (isApolloNametag)
                    continue;

                // Scoreboard teams & color matching
                static int localTeamColor = -1;
                static unsigned long lastColorRefresh = 0;
                unsigned long nowTick = GetTickCount();
                if (nowTick - lastColorRefresh > 1000) {
                    localTeamColor = mc.getLocalPlayerTeamColor();
                    lastColorRefresh = nowTick;
                }

                ChatColor playerColor = static_cast<ChatColor>(p.teamColor);
                ChatColor localColor = static_cast<ChatColor>(localTeamColor);
                static std::string localPlayerName;
                static unsigned long lastNameRefresh = 0;
                if (localPlayerName.empty() || nowTick - lastNameRefresh > 1000) {
                    localPlayerName = mc.getLocalPlayerName();
                    lastNameRefresh = nowTick;
                }
                bool isScoreboardTeammate = false;
                if (playerColor != ChatColor::WHITE && playerColor != ChatColor::GRAY &&
                    playerColor != ChatColor::UNKNOWN) {
                    isScoreboardTeammate = friendsManager->isScoreboardTeammate(
                        localPlayerName, p.profileName.empty() ? p.name : p.profileName);
                }
                isScoreboardTeammate =
                    isScoreboardTeammate || friendsManager->isTeammate(p.uuid, playerColor, localColor);
                if (isScoreboardTeammate)
                    continue;
            }
        } else {
            if (!settings.targetTeammates && p.isOnLocalTeam)
                continue;
        }

        if (settings.onlyVisible && !mc.canEntityBeSeen(p.instance))
            continue;

        // Armor-tier ("class") filter: only aim at players wearing a checked tier.
        if (settings.armorFilterEnabled && settings.armorTierMask != 0) {
            auto tierBit = [](int id) -> int {
                if (id >= 298 && id <= 301)
                    return ARMOR_LEATHER;
                if (id >= 302 && id <= 305)
                    return ARMOR_CHAIN;
                if (id >= 306 && id <= 309)
                    return ARMOR_IRON;
                if (id >= 310 && id <= 313)
                    return ARMOR_DIAMOND;
                if (id >= 314 && id <= 317)
                    return ARMOR_GOLD;
                return 0;
            };
            bool match = false;
            bool hasAnyArmor = false;
            for (const auto& piece : p.armor) {
                if (piece.id > 0) {
                    hasAnyArmor = true;
                    int b = tierBit(piece.id);
                    if (b && (settings.armorTierMask & b)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match && (settings.armorTierMask & ARMOR_NO_ARMOR) && !hasAnyArmor)
                match = true;
            if (!match)
                continue;
        }

        const double dx = p.posX - lx;
        const double dz = p.posZ - lz;
        const double distSq = dx * dx + dz * dz;
        if (distSq > maxDistSq)
            continue;

        // Aim at a point on the body rather than the feet, so the cone test and the
        // solve agree on where we are actually pointing.
        double bodyH = 1.8;
        const double hitboxH = (double)p.hitboxMaxY - (double)p.hitboxMinY;
        if (hitboxH > 0.1 && hitboxH < 4.0)
            bodyH = hitboxH;
        const double dy = (p.posY + bodyH * (double)std::clamp(settings.aimHeight, 0.0f, 1.0f)) - ly;
        const double flat = std::sqrt(distSq);

        const float yawTo = (float)(std::atan2(-dx, dz) * kDeg);
        const float pitchTo = (float)(-std::atan2(dy, (std::max)(flat, 1e-4)) * kDeg);
        const float yawDelta = std::fabs(wrapDeg(yawTo - referenceYaw));
        const float pitchDelta = std::fabs(pitchTo - referencePitch);
        const float angle = std::sqrt(yawDelta * yawDelta + pitchDelta * pitchDelta);
        if (angle > maxAngle)
            continue;

        const double score =
            (settings.mode == AimMode::ClosestToCrosshair) ? (double)angle : std::sqrt(distSq + dy * dy);
        if (score >= bestScore)
            continue;

        bestScore = score;
        best = &p;
    }

    // Copy the target's data, then release EVERY player GlobalRef that
    // getPlayerList() allocated (one per player). These are NOT auto-freed —
    // leaking them every tick overflows the JVM global-ref table and crashes.
    bool found = best != nullptr;
    double tX = 0, tY = 0, tZ = 0, tLX = 0, tLY = 0, tLZ = 0;
    double tBodyH = 1.8;
    std::string tUuid;
    if (found) {
        tX = best->posX;
        tY = best->posY;
        tZ = best->posZ;
        tLX = best->lastPosX;
        tLY = best->lastPosY;
        tLZ = best->lastPosZ;
        tUuid = best->uuid;
        const double hitboxH = (double)best->hitboxMaxY - (double)best->hitboxMinY;
        if (hitboxH > 0.1 && hitboxH < 4.0)
            tBodyH = hitboxH;
    }
    if (JNIEnv* env = Java::Env()) {
        for (auto& pp : players) {
            if (pp.instance) {
                env->DeleteGlobalRef(pp.instance);
                pp.instance = nullptr;
            }
        }
    }

    if (!found) {
        hasTarget = false;
        hasOverride = false;
        return;
    }
    hasTarget = true;

    // Target velocity, in blocks per tick.
    //
    // pos - lastPos is exactly one tick of movement by construction, but we read it
    // ~25 times per tick, so folding it in on every call would weight one tick of
    // data 25 times and let a single zero-motion read drag the estimate to nothing.
    // Fold only when the position we read has actually advanced; between updates the
    // last estimate stands, which is what makes the lead survive to the next packet.
    if (tUuid != velUuid_) {
        velUuid_ = tUuid;
        smoothVx_ = smoothVy_ = smoothVz_ = 0.0;
        hasSample_ = false;
    }
    if (!hasSample_ || tX != sampleX_ || tY != sampleY_ || tZ != sampleZ_) {
        sampleX_ = tX;
        sampleY_ = tY;
        sampleZ_ = tZ;
        const bool first = !hasSample_;
        hasSample_ = true;
        const double rawVx = tX - tLX;
        const double rawVy = tY - tLY;
        const double rawVz = tZ - tLZ;
        // A sprinting player does ~0.28 b/t; anything past 1.5 is a teleport, a
        // server correction or the tail of a target switch, and leading on it throws
        // the crosshair across the map.
        if (first || std::fabs(rawVx) > 1.5 || std::fabs(rawVz) > 1.5 || std::fabs(rawVy) > 3.0) {
            smoothVx_ = smoothVy_ = smoothVz_ = 0.0;
        } else {
            constexpr double a = 0.4;  // ~2 tick response, still smooth over jitter
            smoothVx_ += (rawVx - smoothVx_) * a;
            smoothVy_ += (rawVy - smoothVy_) * a;
            smoothVz_ += (rawVz - smoothVz_) * a;
        }
    }
    const double vx = smoothVx_;
    const double vy = smoothVy_;
    const double vz = smoothVz_;
    const double hLead = settings.predict ? (double)settings.predictStrength : 0.0;
    const double vLead = settings.predict ? (double)settings.verticalLead : 0.0;

    // Aim point: a fraction of the way up the target's body, measured from its feet.
    // posY is the feet for a 1.8 player and the bounding box gives the real height,
    // so this is an absolute world position — no eye-level anchoring, which is what
    // used to pin the shot to our own eye height and miss anything on a different
    // level entirely.
    const double aimBase = tY + tBodyH * (double)std::clamp(settings.aimHeight, 0.0f, 1.0f);

    // Solve and lead together: the lead needs the flight time, and the flight time
    // depends on where we end up aiming. Four passes converge to well under a tick.
    double theta = 0.0;  // launch elevation (radians, + = up)
    double tof = 0.0;    // flight time in ticks
    double solvedX = tX, solvedZ = tZ;
    bool inReach = false;
    for (int iter = 0; iter < 5; ++iter) {
        const double ax = tX + vx * tof * hLead;
        const double az = tZ + vz * tof * hLead;
        // Vertical lead is clamped: a player's vertical velocity reverses within a
        // jump, so extrapolating it over a long flight is a guess that gets worse
        // with distance. Better to sit on their body than to chase a fictional apex.
        const double ay = aimBase + std::clamp(vy * tof * vLead, -3.0, 3.0);
        solvedX = ax;
        solvedZ = az;

        const double dx = ax - lx;
        const double dz = az - lz;
        const double d = std::sqrt(dx * dx + dz * dz);
        double th = 0.0, n = 0.0;
        if (!solveArc(v, d, ay - ly, th, n)) {
            // No arc passes through the target. Hold the loft that puts the arrow as
            // high as it can be at that distance — the closest possible attempt —
            // rather than snapping the view somewhere meaningless.
            theta = th;
            inReach = false;
            lastDist_ = (float)d;
            break;
        }
        theta = th;
        inReach = true;
        lastDist_ = (float)d;
        const bool converged = std::fabs(n - tof) < 0.05;
        tof = n;
        if (converged || (hLead <= 0.0 && vLead <= 0.0))
            break;
    }
    lastInReach_ = inReach;

    const double fdx = solvedX - lx;
    const double fdz = solvedZ - lz;
    float targetYaw = (float)(std::atan2(-fdx, fdz) * kDeg);
    float targetPitch = (float)(-theta * kDeg);
    targetPitch = std::clamp(targetPitch, -90.0f, 90.0f);

    // Smooth toward the solution. Ease out as the crosshair closes in so it
    // settles instead of snapping/jittering (that's the "gaga" stiffness).
    float yawErr = wrapDeg(targetYaw - curYaw);
    float pitchErr = targetPitch - curPitch;
    float base = std::clamp(settings.speed / 100.0f, 0.02f, 1.0f);
    float mag = std::max(std::fabs(yawErr), std::fabs(pitchErr));
    float ease = std::clamp(mag / 22.0f, 0.08f, 1.0f);  // wide slow-down zone -> buttery settle
    float step = base * ease;

    float newYaw = curYaw + yawErr * step;
    float newPitch = std::clamp(curPitch + pitchErr * step, -90.0f, 90.0f);

    // Set yaw/pitch AND their prev values, but set prev slightly BEHIND the new
    // value (lerp) so the render interpolates smoothly instead of teleporting
    // each tick. Setting prev == new is what made it look stiff/janky.
    player.setYaw(newYaw);
    player.setPrevYaw(std::lerp(curYaw, newYaw, 0.85f));
    player.setPitch(newPitch);
    player.setPrevPitch(std::lerp(curPitch, newPitch, 0.85f));
    lastRotWriteMs_.store(GetTickCount64(), std::memory_order_release);

    lastOverrideYaw = newYaw;
    lastOverridePitch = newPitch;
    hasOverride = true;
}

void AimBow::onRenderMenu() {
    const char* bowModes[] = {"Automatic", "Closest to Crosshair"};
    int currentBowMode = static_cast<int>(settings.mode);
    bool enabledValue = enabled;

    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ui::SliderFloat("Range", &settings.range, 5.0f, 120.0f, "%.0f blocks");
            ui::SliderInt("FOV", &settings.fov, 10, 180, "%d");
            ui::SliderFloat("Speed", &settings.speed, 1.0f, 100.0f, "%.0f%%");
            ui::SliderFloat("Aim Height", &settings.aimHeight, 0.0f, 1.0f, "%.2f");
            ImGui::Spacing();
            ui::Checkbox("Live Charge", &settings.liveCharge);
            if (settings.liveCharge)
                ui::SliderFloat("Min Charge", &settings.minSolveCharge, 0.0f, 1.0f, "%.2f");
        },
        "Targeting",
        [this, &currentBowMode, &bowModes]() {
            ui::Checkbox("Predict Movement", &settings.predict);
            if (settings.predict) {
                ui::SliderFloat("Strength", &settings.predictStrength, 0.0f, 2.0f, "%.2fx");
                ui::SliderFloat("Vertical Lead", &settings.verticalLead, 0.0f, 2.0f, "%.2fx");
            }
            ui::Checkbox("Only While Drawing", &settings.onlyWhileDrawing);
            ui::Checkbox("Only Visible", &settings.onlyVisible);
            ui::Checkbox("Target Invisibles", &settings.targetInvisible);
            ui::Checkbox("Target Teammates", &settings.targetTeammates);

            if (ui::Combo("Aim Mode", &currentBowMode, bowModes, IM_ARRAYSIZE(bowModes))) {
                settings.mode = static_cast<AimMode>(currentBowMode);
            }

            // Armor-tier ("class") filter — only aim at these. None checked = all.
            ImGui::Spacing();
            ui::Checkbox("Armor Filter", &settings.armorFilterEnabled);
            ui::Binder("Armor Filter Key", &settings.armorFilterKeybind);
            auto tierCheckbox = [this](const char* label, int bit) {
                bool on = (settings.armorTierMask & bit) != 0;
                if (ui::Checkbox(label, &on)) {
                    if (on)
                        settings.armorTierMask |= bit;
                    else
                        settings.armorTierMask &= ~bit;
                }
            };
            tierCheckbox("No Armor", ARMOR_NO_ARMOR);
            tierCheckbox("Leather", ARMOR_LEATHER);
            tierCheckbox("Chainmail", ARMOR_CHAIN);
            tierCheckbox("Iron", ARMOR_IRON);
            tierCheckbox("Gold", ARMOR_GOLD);
            tierCheckbox("Diamond", ARMOR_DIAMOND);
            if (settings.armorTierMask == 0)
                ImGui::TextDisabled("(targeting everyone)");
        });
}

void AimBow::saveSettings(json::Value& node) const {
    node["mode"] = static_cast<int>(settings.mode);
    node["range"] = settings.range;
    node["fov"] = settings.fov;
    node["speed"] = settings.speed;
    node["predict"] = settings.predict;
    node["predictStrength"] = settings.predictStrength;
    node["verticalLead"] = settings.verticalLead;
    node["aimHeight"] = settings.aimHeight;
    node["liveCharge"] = settings.liveCharge;
    node["minSolveCharge"] = settings.minSolveCharge;
    node["targetInvisible"] = settings.targetInvisible;
    node["targetTeammates"] = settings.targetTeammates;
    node["onlyWhileDrawing"] = settings.onlyWhileDrawing;
    node["onlyVisible"] = settings.onlyVisible;
    node["armorTierMask"] = settings.armorTierMask;
    node["armorFilterEnabled"] = settings.armorFilterEnabled;
    node["armorFilterKeybind"] = settings.armorFilterKeybind;
}

void AimBow::loadSettings(const json::Value& node) {
    if (node.has("mode")) {
        settings.mode = static_cast<AimMode>(std::clamp(node["mode"].as_int(static_cast<int>(settings.mode)), 0, 1));
    }
    settings.range = std::clamp(node["range"].as_float(settings.range), 5.0f, 120.0f);
    settings.fov = std::clamp(node["fov"].as_int(settings.fov), 10, 180);
    settings.speed = std::clamp(node["speed"].as_float(settings.speed), 1.0f, 100.0f);
    settings.predict = node["predict"].as_bool(settings.predict);
    settings.predictStrength = std::clamp(node["predictStrength"].as_float(settings.predictStrength), 0.0f, 2.0f);
    settings.verticalLead = std::clamp(node["verticalLead"].as_float(settings.verticalLead), 0.0f, 2.0f);
    settings.aimHeight = std::clamp(node["aimHeight"].as_float(settings.aimHeight), 0.0f, 1.0f);
    settings.liveCharge = node["liveCharge"].as_bool(settings.liveCharge);
    settings.minSolveCharge = std::clamp(node["minSolveCharge"].as_float(settings.minSolveCharge), 0.0f, 1.0f);
    settings.targetInvisible = node["targetInvisible"].as_bool(settings.targetInvisible);
    settings.targetTeammates = node["targetTeammates"].as_bool(settings.targetTeammates);
    settings.onlyWhileDrawing = node["onlyWhileDrawing"].as_bool(settings.onlyWhileDrawing);
    settings.onlyVisible = node["onlyVisible"].as_bool(settings.onlyVisible);
    settings.armorTierMask = node["armorTierMask"].as_int(settings.armorTierMask);
    settings.armorFilterEnabled = node["armorFilterEnabled"].as_bool(settings.armorFilterEnabled);
    settings.armorFilterKeybind = node["armorFilterKeybind"].as_int(settings.armorFilterKeybind);
}
