#include "InstanceType.h"

#include "Guard.h"
#include "Utils.h"

#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace InstanceType {

std::string Of(UObject* actor) {
    if (!Guard::IsReadableObject(actor)) return Undefined;

    try {
        if (Utils::CallPalUtilityBool(actor, STR("IsOtomo"))) return Companion;

        if (actor->GetPropertyByNameInChain(STR("PlayerCameraYaw"))) return Player;

        if (Utils::CallPalUtilityBool(actor, STR("IsBaseCampPal"))) return BaseCampPal;
        if (Utils::CallPalUtilityBool(actor, STR("IsPalMonster"))) return WildPal;
        if (Utils::CallPalUtilityBool(actor, STR("IsWildNPC"))) return NPC;
    } catch (...) {
    }

    return Undefined;
}

}
