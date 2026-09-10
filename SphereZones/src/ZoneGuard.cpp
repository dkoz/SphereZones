#include "ZoneGuard.h"

#include "Guard.h"
#include "InstanceType.h"
#include "ModConfig.h"
#include "Utils.h"
#include "ZoneStore.h"
#include "ZoneTracker.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <cstring>
#include <string>

using namespace RC;
using namespace RC::Unreal;

namespace ZoneGuard {

static constexpr size_t DAMAGE_INFO_SIZE = 0x130;
static constexpr size_t OFFSET_ATTACKER = 0x0048;
static constexpr size_t OFFSET_HIT_LOCATION = 0x0060;
static constexpr size_t OFFSET_NO_DAMAGE = 0x00D6;

static constexpr size_t OFFSET_TRANSFORM_TRANSLATION = 0x0020;

struct GuidLite {
    uint32_t A = 0;
    uint32_t B = 0;
    uint32_t C = 0;
    uint32_t D = 0;
};

static_assert(sizeof(GuidLite) == 0x10, "FGuid is four uint32 in the engine");
static_assert(sizeof(Utils::FVectorLite) == 0x18, "FVector is three doubles in UE5");

static UObject* ReadAttacker(uint8_t* damageInfo) {
    return *reinterpret_cast<UObject**>(damageInfo + OFFSET_ATTACKER);
}

static Utils::FVectorLite ReadHitLocation(uint8_t* damageInfo) {
    return *reinterpret_cast<Utils::FVectorLite*>(damageInfo + OFFSET_HIT_LOCATION);
}

static void SetNoDamage(uint8_t* damageInfo) {
    *reinterpret_cast<bool*>(damageInfo + OFFSET_NO_DAMAGE) = true;
}

static void Notify(UObject* controller, const std::string& templateText, const std::string& zoneName,
                   const char* channel) {
    auto& cfg = ModConfig::Get();
    if (!cfg.ShowBlockMessage) return;
    if (!controller) return;

    int32_t playerId = Utils::ExtractControllerId(controller);
    if (!Utils::ShouldNotify(playerId, channel)) return;

    Utils::NotifyPlayer(controller, Utils::ReplaceAll(templateText, "{zone}", zoneName));
}

static void DebugLine(const wchar_t* label, const std::string& attacker, const std::string& target,
                      const std::string& zoneName, bool blocked) {
    if (!ModConfig::Get().DebugLogging) return;

    Output::send(STR("[SphereZones] {}: attacker={} target={} zone={} blocked={}\n"),
                 label,
                 std::wstring(attacker.begin(), attacker.end()),
                 std::wstring(target.begin(), target.end()),
                 std::wstring(zoneName.begin(), zoneName.end()),
                 blocked);
}

void OnCharacterDamage(UObject* controller, void* Parms, DamageTarget targetKind) {
    if (!Parms) return;

    auto& store = Zones::Store::Get();
    if (!store.IsLoaded()) return;

    auto* damageInfo = static_cast<uint8_t*>(Parms);
    UObject** defenderSlot = reinterpret_cast<UObject**>(damageInfo + DAMAGE_INFO_SIZE);

    auto hit = ReadHitLocation(damageInfo);
    const Zones::Zone& zone = store.ZoneAt(hit.X, hit.Y);
    if (!zone.HasPermissions) return;

    UObject* attackerActor = ReadAttacker(damageInfo);
    UObject* defenderActor = *defenderSlot;

    std::string attacker = InstanceType::Of(attackerActor);
    std::string target = InstanceType::Of(defenderActor);

    if (target == InstanceType::Undefined && targetKind != DamageTarget::NPC) {
        target = InstanceType::Player;
    }

    bool allowed = Zones::Store::IsDamageAllowed(zone, attacker, target);

    const wchar_t* label = targetKind == DamageTarget::EnemyPlayer ? STR("ToEnemyPlayer")
                           : targetKind == DamageTarget::SelfPlayer ? STR("ToSelfPlayer")
                                                                    : STR("ToNPC");
    DebugLine(label, attacker, target, zone.Name, !allowed);

    if (allowed) return;

    SetNoDamage(damageInfo);

    if (targetKind == DamageTarget::NPC) {

        *defenderSlot = nullptr;
    }

    UObject* notifyTarget = Utils::CanNotify(controller) ? controller
                                                        : Utils::FindOwningController(attackerActor);
    Notify(notifyTarget, ModConfig::Get().DamageBlockMessage, zone.Name, "damage");
}

static UObject* GetMapObjectManager(UObject* worldContext) {
    UObject* palUtility = Utils::GetPalUtility();
    if (!palUtility) return nullptr;

    UFunction* fn = palUtility->GetFunctionByNameInChain(STR("GetMapObjectManager"));
    if (!fn) return nullptr;

    struct { const UObject* WorldContextObject; UObject* ReturnValue; } params{};
    params.WorldContextObject = worldContext;
    palUtility->ProcessEvent(fn, &params);
    return params.ReturnValue;
}

static UObject* FindMapObjectModel(UObject* worldContext, const GuidLite& instanceId) {
    UObject* manager = GetMapObjectManager(worldContext);
    if (!manager) return nullptr;

    UFunction* fn = manager->GetFunctionByNameInChain(STR("FindModel"));
    if (!fn) fn = manager->GetFunctionByNameInChain(STR("FindConcreteModel"));
    if (!fn) return nullptr;

    struct { GuidLite InstanceId; UObject* ReturnValue; } params{};
    params.InstanceId = instanceId;
    manager->ProcessEvent(fn, &params);
    return params.ReturnValue;
}

static bool HasBuildObjectId(UObject* model) {
    auto prop = model->GetPropertyByNameInChain(STR("BuildObjectId"));
    if (!prop) return false;

    auto ptr = prop->ContainerPtrToValuePtr<uint8_t>(model);
    if (!ptr) return false;

    return *reinterpret_cast<uint32_t*>(ptr) != 0;
}

static bool GetModelLocation(UObject* model, Utils::FVectorLite& out) {
    auto prop = model->GetPropertyByNameInChain(STR("InitialTransformCache"));
    if (!prop) return false;

    auto ptr = prop->ContainerPtrToValuePtr<uint8_t>(model);
    if (!ptr) return false;

    out = *reinterpret_cast<Utils::FVectorLite*>(ptr + OFFSET_TRANSFORM_TRANSLATION);
    return true;
}

void OnMapObjectDamage(UObject* component, void* Parms) {
    if (!Parms || !component) return;

    auto& store = Zones::Store::Get();
    if (!store.IsLoaded()) return;

    auto* base = static_cast<uint8_t*>(Parms);
    auto* instanceId = reinterpret_cast<GuidLite*>(base);
    auto* damageInfo = base + sizeof(GuidLite);

    auto hit = ReadHitLocation(damageInfo);
    const Zones::Zone& zone = store.ZoneAt(hit.X, hit.Y);
    if (!zone.HasPermissions) return;

    UObject* controller = Utils::TraceOwnerChain(component);

    UObject* model = FindMapObjectModel(controller ? controller : component, *instanceId);
    if (!Guard::IsReadableObject(model)) return;

    if (!HasBuildObjectId(model)) return;

    UObject* attackerActor = ReadAttacker(damageInfo);
    std::string attacker = InstanceType::Of(attackerActor);

    bool allowed = Zones::Store::IsDamageAllowed(zone, attacker, InstanceType::Structure);
    DebugLine(STR("MapObjectDamage"), attacker, InstanceType::Structure, zone.Name, !allowed);

    if (allowed) return;

    SetNoDamage(damageInfo);
    *instanceId = GuidLite{};

    if (!controller) controller = Utils::FindOwningController(attackerActor);
    Notify(controller, ModConfig::Get().StructureDamageBlockMessage, zone.Name, "structure");
}

static UFunction* g_BuildFunc = nullptr;
static UFunction* g_DismantleFunc = nullptr;
static UnrealScriptFunction g_OriginalBuild = nullptr;
static UnrealScriptFunction g_OriginalDismantle = nullptr;

static bool ShouldBlockBuild(UObject* component, uint8_t* parms) {
    auto& store = Zones::Store::Get();
    if (!store.IsLoaded()) return false;
    if (!component || !parms) return false;

    UObject* controller = Utils::TraceOwnerChain(component);
    if (Utils::IsAdmin(controller)) return false;

    struct BuildParams {
        FName BuildObjectId;
        Utils::FVectorLite Location;
    };

    static_assert(offsetof(BuildParams, Location) == 0x8, "RequestBuild_ToServer Location is at 0x8");

    auto* params = reinterpret_cast<BuildParams*>(parms);

    const Zones::Zone& zone = store.ZoneAt(params->Location.X, params->Location.Y);
    bool allowed = Zones::Store::IsWorldActionAllowed(zone, "Build");
    DebugLine(STR("RequestBuild"), InstanceType::Player, "Build", zone.Name, !allowed);

    if (allowed) return false;

    Notify(controller, ModConfig::Get().BuildBlockMessage, zone.Name, "build");
    return true;
}

static bool ShouldBlockDismantle(UObject* component, uint8_t* parms) {
    auto& store = Zones::Store::Get();
    if (!store.IsLoaded()) return false;
    if (!component || !parms) return false;

    UObject* controller = Utils::TraceOwnerChain(component);
    if (Utils::IsAdmin(controller)) return false;

    auto* instanceId = reinterpret_cast<GuidLite*>(parms);

    UObject* model = FindMapObjectModel(controller ? controller : component, *instanceId);
    if (!Guard::IsReadableObject(model)) return false;

    Utils::FVectorLite location{};
    if (!GetModelLocation(model, location)) return false;

    const Zones::Zone& zone = store.ZoneAt(location.X, location.Y);
    bool allowed = Zones::Store::IsWorldActionAllowed(zone, "Dismantle");
    DebugLine(STR("RequestDismantle"), InstanceType::Player, "Dismantle", zone.Name, !allowed);

    if (allowed) return false;

    Notify(controller, ModConfig::Get().DismantleBlockMessage, zone.Name, "dismantle");
    return true;
}

static bool ShouldBlockSignEdit(UObject* signboard, uint8_t* parms) {
    auto& store = Zones::Store::Get();
    if (!store.IsLoaded()) return false;
    if (!signboard || !parms) return false;

    int32_t playerId = *reinterpret_cast<int32_t*>(parms);
    UObject* controller = Utils::GetControllerByPlayerId(playerId);
    if (Utils::IsAdmin(controller)) return false;

    Utils::FVectorLite location{};
    if (!GetModelLocation(signboard, location)) return false;

    const Zones::Zone& zone = store.ZoneAt(location.X, location.Y);
    bool allowed = Zones::Store::IsWorldActionAllowed(zone, "EditSign");
    DebugLine(STR("SignEdit"), InstanceType::Player, "EditSign", zone.Name, !allowed);

    if (allowed) return false;

    Notify(controller, ModConfig::Get().SignEditBlockMessage, zone.Name, "sign");
    return true;
}

template <typename F>
static void Decide(const wchar_t* label, F&& decide, bool& outBlock) {
    try {
        Guard::Run(label, [&] { outBlock = decide(); });
    } catch (...) {
        Output::send(STR("[SphereZones] {} decision threw; action allowed through\n"), label);
    }
}

static void HookedRequestBuild(UObject* Context, FFrame& TheStack, void* RESULT_DECL) {
    bool block = false;

    Decide(STR("RequestBuild_ToServer"), [&] {
        uint8_t* parms = TheStack.Locals();
        if (!Guard::IsReadable(parms, 0x20)) return false;
        return ShouldBlockBuild(Context, parms);
    }, block);

    if (block) return;

    if (g_OriginalBuild) g_OriginalBuild(Context, TheStack, RESULT_DECL);
}

static void HookedRequestDismantle(UObject* Context, FFrame& TheStack, void* RESULT_DECL) {
    bool block = false;

    Decide(STR("RequestDismantleObject_ToServer"), [&] {
        uint8_t* parms = TheStack.Locals();
        if (!Guard::IsReadable(parms, 0x10)) return false;
        return ShouldBlockDismantle(Context, parms);
    }, block);

    if (block) return;

    if (g_OriginalDismantle) g_OriginalDismantle(Context, TheStack, RESULT_DECL);
}

static UFunction* g_SignEditFunc = nullptr;
static UFunction* g_SignUpdateFunc = nullptr;
static UnrealScriptFunction g_OriginalSignEdit = nullptr;
static UnrealScriptFunction g_OriginalSignUpdate = nullptr;

static void HookedSignEditText(UObject* Context, FFrame& TheStack, void* RESULT_DECL) {
    bool block = false;

    Decide(STR("Signboard RequestEditText"), [&] {
        uint8_t* parms = TheStack.Locals();
        if (!Guard::IsReadable(parms, 0x8)) return false;
        return ShouldBlockSignEdit(Context, parms);
    }, block);

    if (block) return;

    if (g_OriginalSignEdit) g_OriginalSignEdit(Context, TheStack, RESULT_DECL);
}

static void HookedSignUpdateText(UObject* Context, FFrame& TheStack, void* RESULT_DECL) {
    bool block = false;

    Decide(STR("Signboard RequestUpdateText"), [&] {
        uint8_t* parms = TheStack.Locals();
        if (!Guard::IsReadable(parms, 0x18)) return false;
        return ShouldBlockSignEdit(Context, parms);
    }, block);

    if (block) return;

    if (g_OriginalSignUpdate) g_OriginalSignUpdate(Context, TheStack, RESULT_DECL);
}

static void HookNative(const wchar_t* path, UFunction*& outFunc, UnrealScriptFunction& outOriginal,
                       UnrealScriptFunction replacement, const wchar_t* label) {
    outFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
    if (!outFunc) {
        Output::send(STR("[SphereZones] WARNING: could not find {}, that restriction is inactive\n"), label);
        return;
    }

    outOriginal = outFunc->GetFuncPtr();
    outFunc->SetFuncPtr(replacement);
    Output::send(STR("[SphereZones] {} native pointer replaced\n"), label);
}

static UFunction* g_FnToEnemyPlayer = nullptr;
static UFunction* g_FnToSelfPlayer = nullptr;
static UFunction* g_FnToNPC = nullptr;
static UFunction* g_FnMapObjectDamage = nullptr;

static UFunction* g_FnServerMove = nullptr;
static UFunction* g_FnServerMovePacked = nullptr;
static UFunction* g_FnServerMoveDual = nullptr;
static UFunction* g_FnServerMoveNoBase = nullptr;

bool IsWatchedFunction(UFunction* Function) {
    if (!Function) return false;
    return Function == g_FnServerMovePacked
        || Function == g_FnServerMove
        || Function == g_FnServerMoveDual
        || Function == g_FnServerMoveNoBase
        || Function == g_FnToEnemyPlayer
        || Function == g_FnToNPC
        || Function == g_FnToSelfPlayer
        || Function == g_FnMapObjectDamage;
}

void OnProcessEvent(UObject* Context, UFunction* Function, void* Parms) {

    if (Function == g_FnServerMovePacked || Function == g_FnServerMove
        || Function == g_FnServerMoveDual || Function == g_FnServerMoveNoBase) {
        ZoneTracker::OnPlayerMoved(Context);
    } else if (Function == g_FnToEnemyPlayer) {
        OnCharacterDamage(Context, Parms, DamageTarget::EnemyPlayer);
    } else if (Function == g_FnToNPC) {
        OnCharacterDamage(Context, Parms, DamageTarget::NPC);
    } else if (Function == g_FnToSelfPlayer) {
        OnCharacterDamage(Context, Parms, DamageTarget::SelfPlayer);
    } else if (Function == g_FnMapObjectDamage) {
        OnMapObjectDamage(Context, Parms);
    }
}

static UFunction* ResolveFunction(const wchar_t* path, const wchar_t* label) {
    auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
    if (!fn) {
        Output::send(STR("[SphereZones] WARNING: could not resolve {}, that restriction is inactive\n"), label);
    }
    return fn;
}

void ResolveDispatchTargets() {
    g_FnToEnemyPlayer = ResolveFunction(

        STR("/Script/Pal.PalPlayerController:DamageReactionComponent_ProcessDamage_ToServer_ToEnemyPlayer"),
        STR("ToEnemyPlayer damage"));
    g_FnToSelfPlayer = ResolveFunction(
        STR("/Script/Pal.PalPlayerController:DamageReactionComponent_ProcessDamage_ToServer_ToSelfPlayer"),
        STR("ToSelfPlayer damage"));
    g_FnToNPC = ResolveFunction(
        STR("/Script/Pal.PalPlayerController:DamageReactionComponent_ProcessDamage_ToServer_ToNPC"),
        STR("ToNPC damage"));
    g_FnMapObjectDamage = ResolveFunction(
        STR("/Script/Pal.PalNetworkMapObjectComponent:RequestDamageMapObject_ToServer"),
        STR("map object damage"));

    g_FnServerMove = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.Character:ServerMove"));
    g_FnServerMovePacked = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.Character:ServerMovePacked"));
    g_FnServerMoveDual = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.Character:ServerMoveDual"));
    g_FnServerMoveNoBase = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.Character:ServerMoveNoBase"));

    if (!g_FnServerMove && !g_FnServerMovePacked && !g_FnServerMoveDual && !g_FnServerMoveNoBase) {
        Output::send(STR("[SphereZones] WARNING: no movement RPC resolved; zone entry, locks, statuses and mount rules are inactive\n"));
    } else {
        Output::send(STR("[SphereZones] Movement tracking on: ServerMove={} Packed={} Dual={} NoBase={}\n"),
                     g_FnServerMove != nullptr, g_FnServerMovePacked != nullptr,
                     g_FnServerMoveDual != nullptr, g_FnServerMoveNoBase != nullptr);
    }
}

void InstallActionHooks() {
    ResolveDispatchTargets();

    g_BuildFunc = UObjectGlobals::StaticFindObject<UFunction*>(
        nullptr, nullptr, STR("/Script/Pal.PalNetworkPlayerComponent:RequestBuild_ToServer"));
    if (g_BuildFunc) {
        g_OriginalBuild = g_BuildFunc->GetFuncPtr();
        g_BuildFunc->SetFuncPtr(&HookedRequestBuild);
        Output::send(STR("[SphereZones] RequestBuild_ToServer native pointer replaced\n"));
    } else {
        Output::send(STR("[SphereZones] WARNING: could not find RequestBuild_ToServer, building is unrestricted\n"));
    }

    g_DismantleFunc = UObjectGlobals::StaticFindObject<UFunction*>(
        nullptr, nullptr, STR("/Script/Pal.PalNetworkMapObjectComponent:RequestDismantleObject_ToServer"));
    if (g_DismantleFunc) {
        g_OriginalDismantle = g_DismantleFunc->GetFuncPtr();
        g_DismantleFunc->SetFuncPtr(&HookedRequestDismantle);
        Output::send(STR("[SphereZones] RequestDismantleObject_ToServer native pointer replaced\n"));
    } else {
        Output::send(STR("[SphereZones] WARNING: could not find RequestDismantleObject_ToServer, dismantling is unrestricted\n"));
    }

    HookNative(STR("/Script/Pal.PalMapObjectSignboardModel:RequestEditText"),
               g_SignEditFunc, g_OriginalSignEdit, &HookedSignEditText, STR("Signboard RequestEditText"));
    HookNative(STR("/Script/Pal.PalMapObjectSignboardModel:RequestUpdateText"),
               g_SignUpdateFunc, g_OriginalSignUpdate, &HookedSignUpdateText, STR("Signboard RequestUpdateText"));
}

}
