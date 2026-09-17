#pragma once

#include "../module.h"

#include <atomic>
#include <mutex>

class Freelook : public Module {
public:
    Freelook();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;
    bool usesHoldKeybind() const override { return settings.holdKey; }

    void onCameraBegin(JNIEnv* env);
    void onCameraReady(JNIEnv* env);

    struct Settings {
        int perspective = 1;
        bool holdKey = false;
    } settings;

private:
    jobject getPlayer(JNIEnv* env) const;
    void restoreRotation(JNIEnv* env);

    std::atomic<bool> active_{false};
    std::mutex stateMutex_;
    int previousPerspective_ = -1;
    bool initialized_ = false;
    bool swapped_ = false;
    jobject swappedPlayer_ = nullptr;
    float realYaw_ = 0.0f;
    float realPitch_ = 0.0f;
    float realPrevYaw_ = 0.0f;
    float realPrevPitch_ = 0.0f;
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.0f;
};

extern Freelook* freelook;
