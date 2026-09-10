#pragma once

#include <Unreal/UObject.hpp>

#include <cstdint>
#include <string>

namespace Utils {

struct FVectorLite {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

int32_t ExtractControllerId(RC::Unreal::UObject* controller);

RC::Unreal::UObject* TraceOwnerChain(RC::Unreal::UObject* component);

bool IsAdmin(RC::Unreal::UObject* controller);

bool CanNotify(RC::Unreal::UObject* candidate);

RC::Unreal::UObject* FindOwningController(RC::Unreal::UObject* actor);

struct FRotatorLite {
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
};

RC::Unreal::UObject* GetPawn(RC::Unreal::UObject* controller);
bool GetActorLocation(RC::Unreal::UObject* actor, FVectorLite& out);
bool GetActorRotation(RC::Unreal::UObject* actor, FRotatorLite& out);

bool TeleportActor(RC::Unreal::UObject* actor, const FVectorLite& location, const FRotatorLite& rotation);

bool TeleportToSafePoint(RC::Unreal::UObject* controller);

int32_t GetCharacterLevel(RC::Unreal::UObject* pawn);

RC::Unreal::UObject* GetControllerByPlayerId(int32_t playerId);

bool IsRiding(RC::Unreal::UObject* controller);
bool IsRidingFlyPal(RC::Unreal::UObject* controller);

bool Dismount(RC::Unreal::UObject* controller);

bool SetRideDisabled(RC::Unreal::UObject* pawn, bool disabled);

RC::Unreal::UObject* FindComponentByClass(RC::Unreal::UObject* actor, const wchar_t* classPath);

void NotifyPlayer(RC::Unreal::UObject* controller, const std::string& message);

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to);
std::wstring Widen(const std::string& value);

bool ShouldNotify(int32_t playerId, const char* channel);

RC::Unreal::UObject* GetPalUtility();
bool CallPalUtilityBool(RC::Unreal::UObject* actor, const wchar_t* funcName);

}
