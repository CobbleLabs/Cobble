#include "worldToScreen.h"

#include <limits>
#include <cstdio>

// Debug flag - set to true to log NDC values
static bool debugOnce = false;

Vector4 CWorldToScreen::Multiply(Vector4 v, Matrix m) {
	return Vector4{
		v.x * m.m00 + v.y * m.m10 + v.z * m.m20 + v.w * m.m30,
		v.x * m.m01 + v.y * m.m11 + v.z * m.m21 + v.w * m.m31,
		v.x * m.m02 + v.y * m.m12 + v.z * m.m22 + v.w * m.m32,
		v.x * m.m03 + v.y * m.m13 + v.z * m.m23 + v.w * m.m33
	};
}

bool CWorldToScreen::WorldToScreen(Vector3 point, Matrix modelView, Matrix projection, int screenWidth, int screenHeight, Vector2 &screenPos) {
	// csp = Clip Space Position
	Vector4 csp = Multiply(
		Multiply(
			Vector4 {point.x, point.y, point.z, 1.0f},
			modelView
		),
		projection
	);

	// Avoid division by zero
	if (csp.w == 0.0f) return false;
	
	// ndc = Normalized Device Coordinate
	Vector3 ndc{
		csp.x / csp.w,
		csp.y / csp.w,
		csp.z / csp.w
	};

	// Debug first few calls
	if (!debugOnce) {
		debugOnce = true;
		char buf[128];
		snprintf(buf, sizeof(buf), "[W2S] point=(%.1f,%.1f,%.1f) ndc=(%.2f,%.2f,%.2f) w=%.2f", 
			point.x, point.y, point.z, ndc.x, ndc.y, ndc.z, csp.w);
		printf("%s\n", buf);
		fflush(stdout);
	}

	// In OpenGL NDC: x,y in [-1,1], z in [-1,1] for visible objects
	// csp.w > 0 means in front of camera
	// Check if point is in front of camera and on screen
	if (csp.w > 0.0f) {
		screenPos = Vector2{
			((ndc.x + 1.0f) / 2.0f) * screenWidth,
			((1.0f - ndc.y) / 2.0f) * screenHeight,
		};
		
		// Check if on screen
		if (screenPos.x >= 0 && screenPos.x <= screenWidth &&
			screenPos.y >= 0 && screenPos.y <= screenHeight) {
			return true;
		}
	}

	return false;
}

void CWorldToScreen::ResetDebug() {
	debugOnce = false;
}