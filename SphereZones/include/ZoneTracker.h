#pragma once

#include <Unreal/UObject.hpp>

#include <cstdint>

namespace ZoneTracker {

void OnPlayerMoved(RC::Unreal::UObject* character);

void ForgetPlayer(int32_t playerId);

void Reset();

}
