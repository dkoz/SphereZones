#pragma once

#include <Unreal/UObject.hpp>

namespace ZoneGuard {

enum class DamageTarget {
    EnemyPlayer,
    SelfPlayer,
    NPC,
};

void OnCharacterDamage(RC::Unreal::UObject* controller, void* Parms, DamageTarget target);
void OnMapObjectDamage(RC::Unreal::UObject* component, void* Parms);

void OnProcessEvent(RC::Unreal::UObject* Context, RC::Unreal::UFunction* Function, void* Parms);

bool IsWatchedFunction(RC::Unreal::UFunction* Function);

void InstallActionHooks();

}
