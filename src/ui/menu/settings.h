#pragma once

#include <jni.h>

namespace Settings {
namespace Freelook {

extern bool enabled;
extern bool holdKey;
extern int keybind;
extern int perspective;

void setEnabled(bool value);
void update();
void onWorldChange();
void shutdown();
void onCameraBegin(JNIEnv* env);
void onCameraReady(JNIEnv* env);

}  // namespace Freelook
}  // namespace Settings
