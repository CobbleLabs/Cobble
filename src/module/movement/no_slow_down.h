#pragma once

#include "../module.h"
#include "../../jni/java.h"

class NoSlowDown : public Module {
public:
    NoSlowDown();

    void onUpdate() override;
    void onRenderMenu() override;

private:
    bool initializeFields();
    bool isKeyPhysicallyPressed(jobject gameSettings, jfieldID keyBindField);
    bool isPlayerUsingItem();

    jfieldID movementInputField = nullptr;
    jfieldID moveForwardField = nullptr;
    jfieldID moveStrafeField = nullptr;
    jfieldID entityMoveForwardField = nullptr;
    jfieldID entityMoveStrafeField = nullptr;
    jfieldID itemInUseCountField = nullptr;
};

extern NoSlowDown* noSlowDown;
