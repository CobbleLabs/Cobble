#pragma once

#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>

#include "../module.h"

class SilentAura : public Module {
public:
    enum class TargetMode { DISTANCE, YAW, ARMOR, THREAT, HEALTH };
    enum class TargetArea { CENTER, CLOSEST };

    struct Settings {
        float aimSpeed = 7.0f;
        float extraSwingDistance = 1.0f;
        float minCps = 6.0f;
        float maxCps = 13.0f;
        int maxAngle = 120;
        TargetMode targetMode = TargetMode::DISTANCE;
        TargetArea targetArea = TargetArea::CENTER;
        bool switchTargets = false;
        bool requireMouseDown = false;
        bool weaponOnly = false;
        bool breakBlocks = false;
        bool targetInvisibles = true;
    } settings;

    SilentAura();
    ~SilentAura() override;

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    bool isRewritingRotation() const;
    bool shouldSendOutgoingPacket(JNIEnv* env, jobject packet);
    void onOutgoingPacket(JNIEnv* env, jobject packet);
    void observeAcceptedOutgoingPacket(JNIEnv* env, jobject packet, bool invalidateAttack);
    void invalidatePublishedRotation();

private:
    struct TargetState {
        jobject entity = nullptr;
        int entityId = 0;
        double posX = 0.0;
        double posY = 0.0;
        double posZ = 0.0;
        double aimX = 0.0;
        double aimY = 0.0;
        double aimZ = 0.0;
    } target_;

    void clearTarget(JNIEnv* env);
    bool selectTarget(JNIEnv* env);
    bool updateRotation(std::chrono::steady_clock::time_point now);
    void tryAttack(JNIEnv* env, std::chrono::steady_clock::time_point now, bool aimReady);
    bool publishedRayHitsTarget(JNIEnv* env, float yaw, float pitch);
    void scheduleNextAttack(std::chrono::steady_clock::time_point now);
    bool isHoldingWeapon() const;

    std::recursive_mutex stateMutex_;
    std::mutex publicationMutex_;
    std::mt19937 rng_;
    std::chrono::steady_clock::time_point nextScanAt_{};
    std::chrono::steady_clock::time_point nextAttackAt_{};
    std::chrono::steady_clock::time_point controllerUpdatedAt_{};
    std::chrono::steady_clock::time_point packetPreparedAt_{};

    float managedYaw_ = 0.0f;
    float managedPitch_ = 0.0f;
    double yawResidual_ = 0.0;
    double pitchResidual_ = 0.0;
    bool managedRotationInitialized_ = false;
    std::atomic<bool> controllerRebaseRequested_{false};
    std::uint64_t preparedPacketSerial_ = 0;
    std::uint64_t consumedPacketSerial_ = 0;

    std::atomic<float> serverYaw_{0.0f};
    std::atomic<float> serverPitch_{0.0f};
    std::atomic<std::uint64_t> rotationSequence_{0};
    std::atomic<std::uint64_t> rotationUpdatedAtMs_{0};
    std::atomic<bool> rotationActive_{false};
    std::atomic<float> publishedYaw_{0.0f};
    std::atomic<float> publishedPitch_{0.0f};
    std::atomic<bool> publishedRotationValid_{false};
    std::atomic<float> wireYaw_{0.0f};
    std::atomic<float> wirePitch_{0.0f};
    std::atomic<bool> wireRotationKnown_{false};
    std::atomic<std::uint64_t> publicationSequence_{0};
    std::atomic<std::uint64_t> rotationPacketSerial_{0};
};

extern SilentAura* silentAura;
