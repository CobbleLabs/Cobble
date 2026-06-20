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

    jfieldID movementInputField = nullptr;
    jfieldID moveForwardField = nullptr;
    jfieldID moveStrafeField = nullptr;
};

extern NoSlowDown* noSlowDown;
