#pragma once

#include <Unreal/UObject.hpp>

#include <string>

namespace InstanceType {

inline constexpr const char* Undefined = "Undefined";
inline constexpr const char* Player = "Player";
inline constexpr const char* Companion = "Companion";
inline constexpr const char* BaseCampPal = "BaseCampPal";
inline constexpr const char* WildPal = "WildPal";
inline constexpr const char* NPC = "NPC";
inline constexpr const char* Structure = "Structure";

std::string Of(RC::Unreal::UObject* actor);

}
