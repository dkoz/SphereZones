#include "Utils.h"

#include "Guard.h"
#include "ModConfig.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <ctime>
#include <string>
#include <unordered_map>

using namespace RC;
using namespace RC::Unreal;

namespace Utils {

static uint8_t* PropertyPtr(UObject* obj, const wchar_t* name) {
    if (!obj) return nullptr;
    auto prop = obj->GetPropertyByNameInChain(name);
    if (!prop) return nullptr;
    return prop->ContainerPtrToValuePtr<uint8_t>(obj);
}

static UObject* ObjectProperty(UObject* obj, const wchar_t* name) {
    auto ptr = PropertyPtr(obj, name);
    if (!ptr) return nullptr;
    return *reinterpret_cast<UObject**>(ptr);
}

int32_t ExtractControllerId(UObject* controller) {
    if (!controller) return 0;

    try {
        UObject* playerState = ObjectProperty(controller, STR("PlayerState"));
        if (!playerState) return 0;

        auto playerIdPtr = PropertyPtr(playerState, STR("PlayerId"));
        if (!playerIdPtr) return 0;

        return *reinterpret_cast<int32_t*>(playerIdPtr);
    } catch (...) {
        return 0;
    }
}

UObject* TraceOwnerChain(UObject* component) {
    if (!component) return nullptr;

    try {
        UFunction* getOwner = component->GetFunctionByNameInChain(STR("GetOwner"));
        if (!getOwner) return nullptr;

        struct { UObject* ReturnValue; } outer{};
        component->ProcessEvent(getOwner, &outer);
        if (!outer.ReturnValue) return nullptr;

        UFunction* getOwnerAgain = outer.ReturnValue->GetFunctionByNameInChain(STR("GetOwner"));
        if (!getOwnerAgain) return nullptr;

        struct { UObject* ReturnValue; } owner{};
        outer.ReturnValue->ProcessEvent(getOwnerAgain, &owner);
        return owner.ReturnValue;
    } catch (...) {
        return nullptr;
    }
}

bool IsAdmin(UObject* controller) {
    if (!controller) return false;

    try {
        auto ptr = PropertyPtr(controller, STR("bAdmin"));
        if (!ptr) return false;
        return *ptr != 0;
    } catch (...) {
        return false;
    }
}

bool CanNotify(UObject* candidate) {
    if (!candidate) return false;
    return candidate->GetFunctionByNameInChain(STR("SendLog_ToClient")) != nullptr;
}

UObject* FindOwningController(UObject* actor) {
    if (!actor) return nullptr;

    try {

        UObject* controller = ObjectProperty(actor, STR("Controller"));
        if (CanNotify(controller)) return controller;

        UObject* current = actor;
        for (int depth = 0; depth < 4; ++depth) {
            UFunction* fn = current->GetFunctionByNameInChain(STR("GetOwner"));
            if (!fn) return nullptr;

            struct { UObject* ReturnValue; } params{};
            current->ProcessEvent(fn, &params);
            if (!params.ReturnValue) return nullptr;

            current = params.ReturnValue;
            if (CanNotify(current)) return current;

            UObject* pawnController = ObjectProperty(current, STR("Controller"));
            if (CanNotify(pawnController)) return pawnController;
        }
    } catch (...) {
    }

    return nullptr;
}

UObject* GetPawn(UObject* controller) {
    if (!controller) return nullptr;

    try {
        UFunction* fn = controller->GetFunctionByNameInChain(STR("K2_GetPawn"));
        if (!fn) return nullptr;

        struct { UObject* ReturnValue; } params{};
        controller->ProcessEvent(fn, &params);
        return params.ReturnValue;
    } catch (...) {
        return nullptr;
    }
}

bool GetActorLocation(UObject* actor, FVectorLite& out) {
    if (!actor) return false;

    try {
        UFunction* fn = actor->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
        if (!fn) return false;

        struct { FVectorLite ReturnValue; } params{};
        actor->ProcessEvent(fn, &params);
        out = params.ReturnValue;
        return true;
    } catch (...) {
        return false;
    }
}

bool GetActorRotation(UObject* actor, FRotatorLite& out) {
    if (!actor) return false;

    try {
        UFunction* fn = actor->GetFunctionByNameInChain(STR("K2_GetActorRotation"));
        if (!fn) return false;

        struct { FRotatorLite ReturnValue; } params{};
        actor->ProcessEvent(fn, &params);
        out = params.ReturnValue;
        return true;
    } catch (...) {
        return false;
    }
}

bool TeleportActor(UObject* actor, const FVectorLite& location, const FRotatorLite& rotation) {
    if (!actor) return false;

    try {
        UFunction* fn = actor->GetFunctionByNameInChain(STR("K2_TeleportTo"));
        if (!fn) return false;

        struct {
            FVectorLite DestLocation;
            FRotatorLite DestRotation;
            bool ReturnValue;
            uint8_t Pad[7];
        } params{};
        params.DestLocation = location;
        params.DestRotation = rotation;

        actor->ProcessEvent(fn, &params);
        return params.ReturnValue;
    } catch (...) {
        return false;
    }
}

bool TeleportToSafePoint(UObject* controller) {
    if (!controller) return false;

    try {
        UFunction* fn = controller->GetFunctionByNameInChain(STR("TeleportToSafePoint_ToServer"));
        if (!fn) return false;

        struct { bool bWasOutOfWorld; uint8_t Pad[7]; } params{};
        params.bWasOutOfWorld = true;

        controller->ProcessEvent(fn, &params);
        return true;
    } catch (...) {
        return false;
    }
}

int32_t GetCharacterLevel(UObject* pawn) {
    if (!pawn) return 0;

    try {
        UObject* paramComponent = ObjectProperty(pawn, STR("CharacterParameterComponent"));
        if (!paramComponent) return 0;

        UObject* individual = ObjectProperty(paramComponent, STR("IndividualParameter"));
        if (!individual) {
            UFunction* getter = paramComponent->GetFunctionByNameInChain(STR("GetIndividualParameter"));
            if (!getter) return 0;

            struct { UObject* ReturnValue; } params{};
            paramComponent->ProcessEvent(getter, &params);
            individual = params.ReturnValue;
        }
        if (!individual) return 0;

        UFunction* getLevel = individual->GetFunctionByNameInChain(STR("GetLevel"));
        if (!getLevel) return 0;

        struct { int32_t ReturnValue; uint8_t Pad[4]; } levelParams{};
        individual->ProcessEvent(getLevel, &levelParams);
        return levelParams.ReturnValue;
    } catch (...) {
        return 0;
    }
}

UObject* GetControllerByPlayerId(int32_t playerId) {
    if (playerId == 0) return nullptr;

    try {
        UObject* palUtility = GetPalUtility();
        if (!palUtility) return nullptr;

        UFunction* fn = palUtility->GetFunctionByNameInChain(STR("GetPlayerControllerByPlayerId"));
        if (!fn) return nullptr;

        struct {
            const UObject* WorldContextObject;
            int32_t PlayerId;
            uint8_t Pad_C[4];
            UObject* ReturnValue;
        } params{};
        params.WorldContextObject = palUtility;
        params.PlayerId = playerId;

        palUtility->ProcessEvent(fn, &params);
        return params.ReturnValue;
    } catch (...) {
        return nullptr;
    }
}

static bool CallControllerBool(UObject* controller, const wchar_t* funcName) {
    if (!controller) return false;

    try {
        UFunction* fn = controller->GetFunctionByNameInChain(funcName);
        if (!fn) return false;

        struct { bool ReturnValue; uint8_t Pad[7]; } params{};
        controller->ProcessEvent(fn, &params);
        return params.ReturnValue;
    } catch (...) {
        return false;
    }
}

bool IsRiding(UObject* controller) {
    return CallControllerBool(controller, STR("IsRiding"));
}

bool IsRidingFlyPal(UObject* controller) {
    return CallControllerBool(controller, STR("IsRidingFlyPal"));
}

bool Dismount(UObject* controller) {
    if (!controller) return false;

    try {

        UFunction* getOff = controller->GetFunctionByNameInChain(STR("GetOffToServer"));
        if (getOff) {
            controller->ProcessEvent(getOff, nullptr);
            if (!IsRiding(controller)) return true;
        }

        UObject* palUtility = GetPalUtility();
        if (!palUtility) return false;

        UFunction* returnPal = palUtility->GetFunctionByNameInChain(STR("ReturnOtomoPalToHolder"));
        if (!returnPal) return false;

        struct { const UObject* TargetController; } params{};
        params.TargetController = controller;
        palUtility->ProcessEvent(returnPal, &params);

        return !IsRiding(controller);
    } catch (...) {
        return false;
    }
}

UObject* FindComponentByClass(UObject* actor, const wchar_t* classPath) {
    if (!actor) return nullptr;

    try {
        UClass* componentClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, classPath);
        if (!componentClass) return nullptr;

        UFunction* fn = actor->GetFunctionByNameInChain(STR("GetComponentByClass"));
        if (!fn) return nullptr;

        struct { UClass* ComponentClass; UObject* ReturnValue; } params{};
        params.ComponentClass = componentClass;
        actor->ProcessEvent(fn, &params);
        return params.ReturnValue;
    } catch (...) {
        return nullptr;
    }
}

bool SetRideDisabled(UObject* pawn, bool disabled) {
    if (!pawn) return false;

    try {
        UObject* rider = FindComponentByClass(pawn, STR("/Script/Pal.PalRiderComponent"));
        if (!rider) return false;

        UFunction* fn = rider->GetFunctionByNameInChain(STR("SetDisableRide"));
        if (!fn) return false;

        struct { FName FlagName; bool bIsDisable; uint8_t Pad[3]; } params{};
        params.FlagName = FName(STR("SphereZones"));
        params.bIsDisable = disabled;

        rider->ProcessEvent(fn, &params);
        return true;
    } catch (...) {
        return false;
    }
}

void NotifyPlayer(UObject* controller, const std::string& message) {
    if (!controller) return;

    try {
        UFunction* sendLog = controller->GetFunctionByNameInChain(STR("SendLog_ToClient"));
        if (!sendLog) return;

        struct {
            uint8_t Priority;
            uint8_t TextCategory;
            uint8_t Pad1[2];
            FName TextId;
            uint8_t Pad2[4];
            uint8_t AdditionalData[0x70];
        } params{};

        params.Priority = 1;
        params.TextCategory = 0;
        params.TextId = FName(Widen(message));

        controller->ProcessEvent(sendLog, &params);
    } catch (...) {
    }
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;

    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

bool ShouldNotify(int32_t playerId, const char* channel) {
    if (playerId == 0) return true;

    static std::unordered_map<std::string, std::time_t> cooldowns;

    auto cooldown = static_cast<std::time_t>(ModConfig::Get().NotifyCooldownSeconds);
    if (cooldown <= 0) return true;

    std::string key = std::to_string(playerId) + ":" + (channel ? channel : "");
    auto now = std::time(nullptr);

    auto it = cooldowns.find(key);
    if (it != cooldowns.end() && (now - it->second) < cooldown) return false;

    cooldowns[key] = now;
    return true;
}

UObject* GetPalUtility() {
    return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Pal.Default__PalUtility"));
}

bool CallPalUtilityBool(UObject* actor, const wchar_t* funcName) {
    if (!actor) return false;

    try {
        UObject* palUtility = GetPalUtility();
        if (!palUtility) return false;

        UFunction* fn = palUtility->GetFunctionByNameInChain(funcName);
        if (!fn) return false;

        struct { const UObject* Actor; bool ReturnValue; uint8_t Pad[7]; } params{};
        params.Actor = actor;
        palUtility->ProcessEvent(fn, &params);
        return params.ReturnValue;
    } catch (...) {
        return false;
    }
}

}
