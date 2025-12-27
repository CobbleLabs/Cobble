#pragma once

#include "../jni/java.h"
#include "../util/math/geometry.h"
#include "../util/logger.hpp"
#include <vector>
#include <string>

// Render state data - updated each frame from the render hook
struct RenderData
{
	static inline bool initialized = false;
	static inline bool updated = false;
	static inline bool initFailed = false;
	
	// Matrices for WorldToScreen projection
	static inline Matrix modelView;
	static inline Matrix projection;
	
	// Screen dimensions
	static inline int screenWidth = 0;
	static inline int screenHeight = 0;
	
	// Cached JNI IDs
	static inline jclass activeRenderInfoClass = nullptr;
	static inline jfieldID projectionField = nullptr;
	static inline jfieldID modelViewField = nullptr;
	
	static inline jclass floatBufferClass = nullptr;
	static inline jmethodID floatBufferGet = nullptr;
	
	// Initialize JNI references
	static bool Init()
	{
		if (initialized) return true;
		if (initFailed) return false;
		if (!Java::Env) {
			initFailed = true;
			return false;
		}
		
		// ActiveRenderInfo - use Java::AssignClass like other SDK classes
		if (!Java::AssignClass("net.minecraft.client.renderer.ActiveRenderInfo", activeRenderInfoClass)) {
			log_err("RenderData: ActiveRenderInfo class not found");
			initFailed = true;
			return false;
		}
		
		// Try uppercase first (1.8.9), then lowercase (1.7.10)
		projectionField = Java::Env->GetStaticFieldID(activeRenderInfoClass, "PROJECTION", "Ljava/nio/FloatBuffer;");
		if (!projectionField) {
			Java::Env->ExceptionClear();
			projectionField = Java::Env->GetStaticFieldID(activeRenderInfoClass, "projection", "Ljava/nio/FloatBuffer;");
		}
		
		modelViewField = Java::Env->GetStaticFieldID(activeRenderInfoClass, "MODELVIEW", "Ljava/nio/FloatBuffer;");
		if (!modelViewField) {
			Java::Env->ExceptionClear();
			modelViewField = Java::Env->GetStaticFieldID(activeRenderInfoClass, "modelview", "Ljava/nio/FloatBuffer;");
		}
		
		if (!projectionField || !modelViewField) {
			Java::Env->ExceptionClear();
			initFailed = true;
			return false;
		}
		
		// FloatBuffer
		jclass fbLocal = Java::Env->FindClass("java/nio/FloatBuffer");
		if (!fbLocal) {
			initFailed = true;
			return false;
		}
		floatBufferClass = (jclass)Java::Env->NewGlobalRef(fbLocal);
		Java::Env->DeleteLocalRef(fbLocal);
		
		floatBufferGet = Java::Env->GetMethodID(floatBufferClass, "get", "(I)F");
		if (!floatBufferGet) {
			initFailed = true;
			return false;
		}
		
		initialized = true;
		log_ok("RenderData initialized");
		return true;
	}
	
	// Read matrix from FloatBuffer
	static Matrix ReadMatrix(jobject floatBuffer)
	{
		Matrix m{};
		if (!floatBuffer || !floatBufferGet) return m;
		
		m.m00 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 0);
		m.m01 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 1);
		m.m02 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 2);
		m.m03 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 3);
		m.m10 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 4);
		m.m11 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 5);
		m.m12 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 6);
		m.m13 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 7);
		m.m20 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 8);
		m.m21 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 9);
		m.m22 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 10);
		m.m23 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 11);
		m.m30 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 12);
		m.m31 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 13);
		m.m32 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 14);
		m.m33 = Java::Env->CallFloatMethod(floatBuffer, floatBufferGet, 15);
		
		return m;
	}
	
	// Check if matrices look valid (not all zeros)
	static bool IsMatrixValid(const Matrix& m)
	{
		return m.m00 != 0.0f || m.m11 != 0.0f || m.m22 != 0.0f || m.m33 != 0.0f;
	}
	
	// Update render data
	static void Update()
	{
		if (!initialized && !Init()) return;
		
		// Read projection matrix
		jobject projBuffer = Java::Env->GetStaticObjectField(activeRenderInfoClass, projectionField);
		if (projBuffer) {
			Matrix newProj = ReadMatrix(projBuffer);
			if (IsMatrixValid(newProj)) {
				projection = newProj;
			}
			Java::Env->DeleteLocalRef(projBuffer);
		}
		
		// Read modelview matrix
		jobject mvBuffer = Java::Env->GetStaticObjectField(activeRenderInfoClass, modelViewField);
		if (mvBuffer) {
			Matrix newMv = ReadMatrix(mvBuffer);
			if (IsMatrixValid(newMv)) {
				modelView = newMv;
			}
			Java::Env->DeleteLocalRef(mvBuffer);
		}
		
		// Only mark as updated if we have valid matrices
		updated = IsMatrixValid(projection) && IsMatrixValid(modelView);
	}
	
	// Reset state (call on world change or re-enable)
	static void Reset()
	{
		updated = false;
		modelView = Matrix{};
		projection = Matrix{};
	}
	
	// Cleanup
	static void Destroy()
	{
		if (activeRenderInfoClass) {
			Java::Env->DeleteGlobalRef(activeRenderInfoClass);
			activeRenderInfoClass = nullptr;
		}
		if (floatBufferClass) {
			Java::Env->DeleteGlobalRef(floatBufferClass);
			floatBufferClass = nullptr;
		}
		initialized = false;
		updated = false;
		initFailed = false;
	}
};
