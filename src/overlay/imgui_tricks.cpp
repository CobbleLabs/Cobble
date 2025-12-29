#include "imgui_tricks.hpp"

namespace ImTricks {

	namespace Animations {

		int FastIntLerp(const char* identifier, bool state, int min, int max, int speed) {
			static std::unordered_map<const char*, int> valuesMapInt;
			auto value = valuesMapInt.find(identifier);

			if (value == valuesMapInt.end()) {
				valuesMapInt.insert({ identifier, min });
				value = valuesMapInt.find(identifier);
			}

			const float frameRateSpeed = speed * (1.f - ImGui::GetIO().DeltaTime);

			if (state) {
				if (value->second < max)
					value->second += (int)frameRateSpeed;
			}
			else {
				if (value->second > min)
					value->second -= (int)frameRateSpeed;
			}

			value->second = std::clamp(value->second, min, max);
			return value->second;
		}

		float FastFloatLerp(std::string identifier, bool state, float min, float max, float speed) {
			static std::unordered_map<std::string, float> valuesMapFloat;
			auto value = valuesMapFloat.find(identifier);

			if (value == valuesMapFloat.end()) {
				valuesMapFloat.insert({ identifier, min });
				value = valuesMapFloat.find(identifier);
			}

			const float frameRateSpeed = speed * (1.f - ImGui::GetIO().DeltaTime);

			if (state) {
				if (value->second < max)
					value->second += frameRateSpeed;
			}
			else {
				if (value->second > min)
					value->second -= frameRateSpeed;
			}

			value->second = std::clamp(value->second, min, max);
			return value->second;
		}

		ImVec2 FastImVec2Lerp(const char* identifier, bool state, ImVec2 min, ImVec2 max, float speed) {
			static std::unordered_map<const char*, ImVec2> valuesMapVec2;
			auto value = valuesMapVec2.find(identifier);

			if (value == valuesMapVec2.end()) {
				valuesMapVec2.insert({ identifier, min });
				value = valuesMapVec2.find(identifier);
			}

			const float frameRateSpeed = speed * (1.f - ImGui::GetIO().DeltaTime);

			if (state) {
				if (value->second.x < max.x)
					value->second.x += frameRateSpeed;
				if (value->second.y < max.y)
					value->second.y += frameRateSpeed;
			}
			else {
				if (value->second.x > min.x)
					value->second.x -= frameRateSpeed;
				if (value->second.y > min.y)
					value->second.y -= frameRateSpeed;
			}

			value->second.x = std::clamp(value->second.x, min.x, max.x);
			value->second.y = std::clamp(value->second.y, min.y, max.y);
			return value->second;
		}

		ImColor FastColorLerp(ImColor start, ImColor end, float stage) {
			ImVec4 lerp = ImLerp(
				ImVec4(start.Value.x, start.Value.y, start.Value.z, start.Value.w),
				ImVec4(end.Value.x, end.Value.y, end.Value.z, end.Value.w),
				stage);
			return ImGui::ColorConvertFloat4ToU32(lerp);
		}

		ImTrickyColor FastColorLerp(ImTrickyColor start, ImTrickyColor end, float stage) {
			ImVec4 lerp = ImLerp(
				ImVec4(start[0], start[1], start[2], start[3]),
				ImVec4(end[0], end[1], end[2], end[3]),
				stage);
			return ImTrickyColor(lerp.x, lerp.y, lerp.z, lerp.w);
		}
	}
}
